#include "csiebox_server.h"
#include "csiebox_common.h"
#include "connect.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <dirent.h>
#include <utime.h>
#include <pthread.h>
#include <sys/file.h>
#include <signal.h>

int handle_request(csiebox_server* server, int conn_fd);
static void login( csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
int tree_walk( char* prefix, int conn_fd, lnkpare hlist[100]);
int cpy_file(int conn_fd, const char* cpath);
int cpy_hlink(int conn_fd, const char* cpath, char* target );

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

//this is where the server handle requests, you should write your code here
int handle_request(csiebox_server* server, int conn_fd) {
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  //WHILE (X)
  while (recv_message(conn_fd, &header, sizeof(header))) {
  // if (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      // continue;
      return 0;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_LOGIN:
        fprintf(stderr, "===============login===============\n");
        csiebox_protocol_login req;
        if (complete_message_with_header(conn_fd, &header, &req)) {
          login(server, conn_fd, &req);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_META:
        fprintf(stderr, "sync meta\t"); fflush(stderr);
        csiebox_protocol_meta meta;
        if (complete_message_with_header(conn_fd, &header, &meta)) {

          // fprintf(stderr, "datalen: %d\n", meta.message.body.pathlen);
          char path[1024];
          memset(&path, 0, sizeof(path));
          recv_message( conn_fd, &path, meta.message.body.pathlen * sizeof(char) );
          fprintf(stderr, "*** File/Dir:  %s\n", path);

          // check dir or file
          if( S_ISDIR(meta.message.body.stat.st_mode) ) {
            DIR *dp;
            if( !(dp = opendir(path)) ) //if nonexist, create the directory
              mkdir( path, (meta.message.body.stat.st_mode & 07777) );
              // send_message( conn_fd, &ok, sizeof(csiebox_protocol_status)  );
          } 
          else if( S_ISLNK(meta.message.body.stat.st_mode) ) {
            char buff[1024];
            memset(&buff, 0, sizeof(buff));
            int bufflen = 0;
            recv_message(conn_fd, &bufflen, sizeof(int) );
            recv_message(conn_fd, &buff, bufflen );
            buff[bufflen] = '\0';
            fprintf(stderr, "bufflen:%d buff:%s\n", bufflen, buff);
            symlink( buff, path);
          }
          else if( S_ISREG(meta.message.body.stat.st_mode) ) {
            // puts("REG");
            FILE * fp;

            if( !(fp = fopen(path, "w+") ) ) fprintf(stderr, "open failed\n");
            int fd = fileno(fp);

            csiebox_protocol_status ok = CSIEBOX_PROTOCOL_STATUS_OK;
            if( flock( fd, LOCK_EX ) ) {
              fprintf(stderr, "Failed to lock file\t");
              fflush(stderr);
              ok = CSIEBOX_PROTOCOL_STATUS_FAIL;
            }
            
            else {

              // fprintf(stderr, "LOCK!\t");
              fflush(stderr);
              struct stat st;
              stat(path, &st);
              if( meta.message.body.stat.st_mode != st.st_mode ||
                  meta.message.body.stat.st_uid  != st.st_uid  ||
                  meta.message.body.stat.st_gid  != st.st_gid)
                chmod( path, meta.message.body.stat.st_mode );

              //check hash
              uint8_t tmphash[MD5_DIGEST_LENGTH];
              memset(&tmphash, 0, sizeof(tmphash));
              if ( !md5_file(path, tmphash) ) puts("hash err");
              if( memcmp(&tmphash, &meta.message.body.hash, sizeof(tmphash) ) ) 
                ok = CSIEBOX_PROTOCOL_STATUS_MORE;

            }

            send_message( conn_fd, &ok, sizeof(csiebox_protocol_status)  );

            if( ok == CSIEBOX_PROTOCOL_STATUS_MORE ) {
              //get datalen
              off_t datalen;
              recv_message( conn_fd, &datalen, sizeof(off_t) );
              // fprintf(stderr, "datalen: %d\n", datalen);
              //get data
              char buff[1024];
              for( int i=0; i<(datalen/1024); i++ ) {
                recv_message( conn_fd, &buff, 1024);
                fwrite( buff, 1024, 1, fp );
              }
              recv_message( conn_fd, &buff, datalen%1024);
              if( fwrite( buff, datalen%1024, 1, fp ) != 1 ) puts("write err");
            }   

            fclose(fp);

            struct utimbuf timbuf;
            memset(&timbuf, 0, sizeof(timbuf));
            timbuf.actime = meta.message.body.stat.st_atime;
            timbuf.modtime = meta.message.body.stat.st_mtime;
            if( utime(path, &timbuf) != 0 ) fprintf(stderr, "utime err (%s)\n", path); 

            //=================================================  
            // else if( flock( fd, LOCK_UN ) )
            //   fprintf(stderr, "Failed to unlock file\n");
            // else fprintf(stderr, "UNLOCK!\n");
            
            //=================================================

          }
          // chdir( ".." );

        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\t");
        csiebox_protocol_hardlink hardlink;
        if (complete_message_with_header(conn_fd, &header, &hardlink)) {
          // fprintf(stderr, "*** Size:  %d -> %d ***\n", hardlink.message.body.srclen, hardlink.message.body.targetlen);
          char src[1024], target[1024];
          memset( &src, 0, sizeof(src) );
          memset( &target, 0, sizeof(target) );
          recv_message(conn_fd, src, hardlink.message.body.srclen);
          recv_message(conn_fd, target, hardlink.message.body.targetlen);
          fprintf(stderr, "*** Hardlink:  %s -> %s\n", src, target);
          if( !link(target, src) ) fprintf(stderr, "Hlink failed\n");
        }
        break;
      case CSIEBOX_PROTOCOL_OP_RM:
        fprintf(stderr, "rm\n");
        csiebox_protocol_rm rm;
        if (complete_message_with_header(conn_fd, &header, &rm)) {
          char path[1024];
          memset(path, 0, sizeof(path));
          recv_message(conn_fd, path, rm.message.body.pathlen);
          fprintf(stderr, "%d REMOVE %s\n", rm.message.body.pathlen, path);
          if( remove(path) ) fprintf(stderr, "remove err\n");
        }
        break;
      case CSIEBOX_PROTOCOL_OP_DOWNLOAD:
        fprintf(stderr, "download request\t");
        csiebox_protocol_header download;

        int ulen;
        char uname[1024];
        recv_message( conn_fd, &ulen, sizeof(int) );
        recv_message( conn_fd, uname, ulen);
        uname[ulen] = '\0';

        fprintf(stderr, "ulen:%d uname:%s\n", ulen, uname);
        // if( chdir( server->arg.path ) == -1 ) fprintf(stderr, "chdir fail(sdir)\n");
        if( chdir( uname ) == -1 )  fprintf(stderr, "chdir fail(user)\n");

        lnkpare hlist[100];
        memset(hlist, 0, sizeof(hlist));
        tree_walk( ".", conn_fd, hlist);

        csiebox_protocol_header endreq;
        memset(&endreq, 0, sizeof(endreq));
        endreq.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        endreq.req.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
        send_message( conn_fd, &endreq, sizeof(endreq) );
        if( chdir( ".." ) == -1 )  fprintf(stderr, "chdir fail(..)\n");

        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "---------end of connection---------\n");
        return 0;
      break;
      case CSIEBOX_PROTOCOL_OP_LOGOUT:
        fprintf(stderr, "===============logout==============\n");
        logout(server, conn_fd);
        return 1;
      break;
      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
      break;
    }
  }
  // else {
  
  // }
  fprintf(stderr, "---------end of connection---------\n");
  return 0;
}

//handle the login request from client
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
  int succ = 1;
  csiebox_client_info* info =
    (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
  memset(info, 0, sizeof(csiebox_client_info));
  if (!get_account_info(server, login->message.body.user, &(info->account))) {
    fprintf(stderr, "cannot find account\n");
    succ = 0;
  }
  if (succ &&
      memcmp(login->message.body.passwd_hash,
             info->account.passwd_hash,
             MD5_DIGEST_LENGTH) != 0) {
    fprintf(stderr, "passwd miss match\n");
    succ = 0;
  }

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    if (server->client[conn_fd]) {
      free(server->client[conn_fd]);
    }
    info->conn_fd = conn_fd;
    server->client[conn_fd] = info;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
    char* homedir = get_user_homedir(server, info);
    mkdir(homedir, DIR_S_FLAG);
    free(homedir);
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));
}

static void logout(csiebox_server* server, int conn_fd) {
  free(server->client[conn_fd]);
  server->client[conn_fd] = 0;
  close(conn_fd);
}

int tree_walk( char* prefix, int conn_fd, lnkpare hlist[100]){

  DIR * dp = opendir(prefix);
  struct dirent * entry;
  struct stat statbuf;
  int i;

  while ( (entry = readdir(dp) ) != NULL) {  
    char cpath[1024];
    sprintf(cpath ,"%s/%s", prefix, entry->d_name);
    lstat(cpath, &statbuf);  

    if (S_ISDIR(statbuf.st_mode)) {  
      if (strcmp(entry->d_name, ".") == 0 || 
        strcmp(entry->d_name, "..") == 0 )    
        continue;     
      printf("DIR %s\n", cpath); 
      cpy_file(conn_fd, cpath); 
      tree_walk( cpath, conn_fd, hlist);  
    } 
    else if ( S_ISREG(statbuf.st_mode) ) {
      printf("REG %s\n", cpath);  
      if( statbuf.st_nlink > 1 ) {
        //check if hardlink already exists
        int exist = 0, i=0;
        for( i=0; i<100; i++ ) {
          if( hlist[i].inode == statbuf.st_ino ) {
            cpy_hlink( conn_fd, cpath, hlist[i].oldpath );
            exist = 1;
            break;
          }
          if( hlist[i].inode == 0 ) break;
        }
        if( !exist ) {
          hlist[i].inode = statbuf.st_ino;
          sprintf(hlist[i].oldpath, "%s", cpath+2);
          cpy_file(conn_fd, cpath);
        }
      }
      else {
        cpy_file(conn_fd, cpath); 
      }
    }
    else if ( S_ISLNK(statbuf.st_mode) ) {
      printf("LNK %s\n", cpath);  
      cpy_file(conn_fd, cpath); 
    }

  } 

  closedir(dp);
  return 0;
}
int cpy_file(int conn_fd, const char* cpath){

  char spath[1024];
  sprintf( spath, "%s", cpath+2); 

  // fprintf(stderr, "%s\n", cpath);

  //first send meta request
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.pathlen = strlen(spath);
  if( lstat(cpath, &req.message.body.stat) ) puts("lstat fail");
  // printf("mode : %d\n", req.message.body.stat.st_mode);

  if( S_ISREG( req.message.body.stat.st_mode ) )
    if ( !md5_file(cpath, req.message.body.hash) ) puts("hash fail");

  if ( !send_message(conn_fd, &req, sizeof(req))) fprintf(stderr, "send fail\n"); 

  // send data
  if ( !send_message(conn_fd, spath, strlen(spath) ) ) {
    fprintf(stderr, "send fail\n"); 
    return 0;
  }

  if( S_ISDIR(req.message.body.stat.st_mode) ) return 0;
  else if( S_ISLNK(req.message.body.stat.st_mode) ) {

    char buff[req.message.body.stat.st_size];
    readlink(cpath, buff, req.message.body.stat.st_size);
    buff[req.message.body.stat.st_size] = '\0';

    // fprintf(stderr, "%s -> %s\n", spath, buff);
    int bufflen = strlen(buff);
    // fprintf(stderr, "bufflen %d\n", bufflen);
    send_message(conn_fd, &bufflen, sizeof(int) );
    send_message(conn_fd, &buff, bufflen );

  }
  else if( S_ISREG(req.message.body.stat.st_mode) ) {
    csiebox_protocol_status ok;
    if( !recv_message( conn_fd, &ok, sizeof(csiebox_protocol_status)) ) 
      fprintf(stderr, "recv error\n" );

    if( ok == CSIEBOX_PROTOCOL_STATUS_MORE )  { 
      // fprintf(stderr, "\tMORE\n");
      // send data
      send_message(conn_fd, &req.message.body.stat.st_size, sizeof(off_t) );
      FILE * fp = fopen( cpath, "r" );
      char buff[1024];
      for( int i=0; i<(req.message.body.stat.st_size/1024); i++ ) {
        fread( &buff, 1024, 1, fp );
        send_message( conn_fd, &buff, 1024);
      }
      fread( &buff, req.message.body.stat.st_size%1024, 1, fp );
      send_message( conn_fd, &buff, req.message.body.stat.st_size%1024 );
      fclose(fp);
    }
    // else if( ok == CSIEBOX_PROTOCOL_STATUS_OK )  
    //   fprintf(stderr, "\tOK\n");
    // else fprintf(stderr, "\tERRRRRRR\n");

  }

  else fprintf(stderr, "\tERR\n");

  return 0;
}
int cpy_hlink(int conn_fd, const char* cpath, char* target ) {
  
  char src[1024];
  sprintf( src, "%s", cpath+2); 

  csiebox_protocol_hardlink req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.srclen = strlen(src);
  req.message.body.targetlen = strlen(target);

  if ( !send_message(conn_fd, &req, sizeof(req))) 
    fprintf(stderr, "send fail\n"); 
  send_message(conn_fd, src, strlen(src) );
  send_message(conn_fd, target, strlen(target) );
  // fprintf(stderr, "*** Hlink: %s -> %s ***\n", src, target);
  // fprintf(stderr, "*** Size:  %d -> %d ***\n", req.message.body.srclen, req.message.body.targetlen);

  return 0;
}
