#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"
#include "hash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/inotify.h> //header for inotify
#include <unistd.h>
#include <dirent.h>
#include <utime.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);
int inotify(csiebox_client* client, int fd, inopare* h);
int tree_walk( char* prefix, csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] );
int cpy_file(csiebox_client* client, const char* cpath);
int cpy_hlink(csiebox_client* client, const char* cpath, char* target );
int little_tree_walk( char* prefix, int inofd, inopare myhash[200] );
static void handle_request(int conn_fd);
int server_available( int conn_fd );

int countEntriesInDir(const char* dirname) {
  int n=0;
  struct dirent* d;
  DIR* dir = opendir(dirname);
  if (dir == NULL) return 0;
  while((d = readdir(dir))!=NULL) n++;
  closedir(dir);
  return n;
}

//read config file, and connect to server
void csiebox_client_init( csiebox_client** client, int argc, char** argv) {
  csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
  if (!tmp) {
    fprintf(stderr, "client malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_client));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = client_start(tmp->arg.name, tmp->arg.server);
  if (fd < 0) {
    fprintf(stderr, "connect fail\n");
    free(tmp);
    return;
  }
  tmp->conn_fd = fd;
  *client = tmp;
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {

  inopare myhash[200];
  memset(myhash, 0, sizeof(myhash));

  //login
  if (!login(client)) {
    fprintf(stderr, "login fail\n");
    return 0;
  }
  fprintf(stderr, "===========login success===========\n\n");

  csiebox_protocol_header req;
  memset(&req, 0, sizeof(req));
  req.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.req.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
  send_message( client->conn_fd, &req, sizeof(req) );

  int inofd = inotify_init();
  if (inofd < 0) perror("inotify_init");

  //sync data
  while( !server_available(client->conn_fd) ) {
    sleep(3);
  }

  if( chdir( client->arg.path ) == -1 ){
    fprintf(stderr, "chdir fail\n");
    return 0;
  };

  fprintf(stderr, "num of dir: %d\n", countEntriesInDir(".") );

  if( countEntriesInDir(".") > 2 ) {
    fprintf(stderr, "==========sync data start==========\n");
    lnkpare hlist[100];
    memset(hlist, 0, sizeof(hlist));
    tree_walk( ".", client, hlist, inofd, myhash);
    fprintf(stderr, "=========sync data success=========\n\n");

    memset(&req, 0, sizeof(req));
    req.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.req.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
    send_message( client->conn_fd, &req, sizeof(req) );

  }
  else{
    
    fprintf(stderr, "========download data start========\n");
    csiebox_protocol_header downloadreq;
    memset(&downloadreq, 0, sizeof(downloadreq));
    downloadreq.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    downloadreq.req.op = CSIEBOX_PROTOCOL_OP_DOWNLOAD;
    send_message( client->conn_fd, &downloadreq, sizeof(downloadreq) );
    int ulen = strlen( client->arg.user );
    send_message( client->conn_fd, &ulen, sizeof(int) );
    send_message( client->conn_fd, &(client->arg.user), ulen );

    handle_request( client->conn_fd );
    little_tree_walk( ".", inofd, myhash);
    fprintf(stderr, "=========download data end=========\n\n");

    memset(&req, 0, sizeof(req));
    req.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.req.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
    send_message( client->conn_fd, &req, sizeof(req) );

  }

  // fprintf(stderr, "===========start Inotify===========\n");
  // inotify(client, inofd, myhash);
  while( !server_available(client->conn_fd) ) {
    sleep(3);
  }
  memset(&req, 0, sizeof(req));
  req.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.req.op = CSIEBOX_PROTOCOL_OP_LOGOUT;
  if( !send_message( client->conn_fd, &req, sizeof(req) ) )
    fprintf(stderr, "logout req failed\n");
  
  return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
  csiebox_client* tmp = *client;
  *client = 0;
  if (!tmp) {
    return;
  }
  close(tmp->conn_fd);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
  if (argc != 2) {
    return 0;
  }
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 5;
  int accept_config[5] = {0, 0, 0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("name", key) == 0) {
      if (vallen <= sizeof(client->arg.name)) {
        strncpy(client->arg.name, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("server", key) == 0) {
      if (vallen <= sizeof(client->arg.server)) {
        strncpy(client->arg.server, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("user", key) == 0) {
      if (vallen <= sizeof(client->arg.user)) {
        strncpy(client->arg.user, val, vallen);
        accept_config[2] = 1;
      }
    } else if (strcmp("passwd", key) == 0) {
      if (vallen <= sizeof(client->arg.passwd)) {
        strncpy(client->arg.passwd, val, vallen);
        accept_config[3] = 1;
      }
    } else if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(client->arg.path)) {
        strncpy(client->arg.path, val, vallen);
        accept_config[4] = 1;
      }
    }
  }
  free(key);
  free(val);
  fclose(file);
  int i, test = 1;
  for (i = 0; i < accept_config_total; ++i) {
    test = test & accept_config[i];
  }
  if (!test) {
    fprintf(stderr, "config error\n");
    return 0;
  }
  return 1;
}
static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
int tree_walk( char* prefix, csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] ){

  DIR * dp = opendir(prefix);
  struct dirent * entry;
  struct stat statbuf;
  int i;

  int wd = inotify_add_watch(inofd, prefix, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
  for( int ii=0; ii<200; ii++ ) {
    if( myhash[ii].wd == 0 ) {
      myhash[ii].wd = wd;
      sprintf(myhash[ii].path, "%s", prefix);
      // fprintf(stderr, "inotify<%d> %s\n", wd, prefix);
      break;
    }
  }

  while ( (entry = readdir(dp) ) != NULL) {  
    char cpath[1024];
    sprintf(cpath ,"%s/%s", prefix, entry->d_name);
    lstat(cpath, &statbuf);  

    if (S_ISDIR(statbuf.st_mode)) {  
      if (strcmp(entry->d_name, ".") == 0 || 
        strcmp(entry->d_name, "..") == 0 )    
        continue;     
      printf("DIR %s\n", cpath); 
      cpy_file(client, cpath); 
      tree_walk( cpath, client, hlist, inofd, myhash );  
    } 
    else if ( S_ISREG(statbuf.st_mode) ) {
      printf("REG %s\n", cpath);  
      if( statbuf.st_nlink > 1 ) {
        //check if hardlink already exists
        int exist = 0, i=0;
        for( i=0; i<100; i++ ) {
          if( hlist[i].inode == statbuf.st_ino ) {
            cpy_hlink( client, cpath, hlist[i].oldpath );
            exist = 1;
            break;
          }
          if( hlist[i].inode == 0 ) break;
        }
        if( !exist ) {
          hlist[i].inode = statbuf.st_ino;
          sprintf(hlist[i].oldpath, "%s%s", client->arg.user, cpath+1);
          while(1) {
            if( cpy_file(client, cpath) == 0 ) break; 
            sleep(3);
          }
        }
      }
      else {
        while(1) {
          if( cpy_file(client, cpath) == 0 ) break; 
          sleep(3);
        }
      }
    }
    else if ( S_ISLNK(statbuf.st_mode) ) {
      printf("LNK %s\n", cpath);  
      while(1) {
        if( cpy_file(client, cpath) == 0 ) break; 
        sleep(3);
      }
    }

  } 

  closedir(dp);
  return 0;
}
int cpy_file(csiebox_client* client, const char* cpath){

  // if( !server_available(client->conn_fd) ) {
  //   fprintf(stderr, "Server busy\n");
  //   return -1;
  // }

  char spath[1024];
  sprintf( spath, "%s%s", client->arg.user, cpath+1); 

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

  if ( !send_message(client->conn_fd, &req, sizeof(req))) fprintf(stderr, "send fail\n"); 

  // send data
  if ( !send_message(client->conn_fd, spath, strlen(spath) ) ) {
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
    send_message(client->conn_fd, &bufflen, sizeof(int) );
    send_message(client->conn_fd, &buff, bufflen );

  }
  else if( S_ISREG(req.message.body.stat.st_mode) ) {
    csiebox_protocol_status ok;
    if( !recv_message( client->conn_fd, &ok, sizeof(csiebox_protocol_status)) ) 
      fprintf(stderr, "recv error\n" );

    if( ok == CSIEBOX_PROTOCOL_STATUS_MORE )  { 
      // fprintf(stderr, "\tMORE\n");
      // send data
      send_message(client->conn_fd, &req.message.body.stat.st_size, sizeof(off_t) );
      FILE * fp = fopen( cpath, "r" );
      int fd = fileno(fp);

      if( flock( fd, LOCK_SH ) )
        fprintf(stderr, "Failed to lock file\t");

      char buff[1024];
      for( int i=0; i<(req.message.body.stat.st_size/1024); i++ ) {
        fread( &buff, 1024, 1, fp );
        send_message( client->conn_fd, &buff, 1024);
      }
      fread( &buff, req.message.body.stat.st_size%1024, 1, fp );
      send_message( client->conn_fd, &buff, req.message.body.stat.st_size%1024 );

      if( flock( fd, LOCK_UN ) )
        fprintf(stderr, "Failed to unlock file\t");

      fclose(fp);
    }
    else if( ok == CSIEBOX_PROTOCOL_STATUS_FAIL ) {  
      fprintf(stderr, "Server blocked\n");
      return -1;
    }
    // else fprintf(stderr, "\tERRRRRRR\n");

  }

  else fprintf(stderr, "\tERR\n");

  return 0;
}
int cpy_hlink(csiebox_client* client, const char* cpath, char* target ) {
  
  // if( !server_available(client->conn_fd) ) {
  //   fprintf(stderr, "Server busy\n");
  //   return -1;
  // }

  char src[1024];
  sprintf( src, "%s%s", client->arg.user, cpath+1); 
  csiebox_protocol_hardlink req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.srclen = strlen(src);
  req.message.body.targetlen = strlen(target);

  if ( !send_message(client->conn_fd, &req, sizeof(req))) 
    fprintf(stderr, "send fail\n"); 
  send_message(client->conn_fd, src, strlen(src) );
  send_message(client->conn_fd, target, strlen(target) );
  // fprintf(stderr, "*** Hlink: %s -> %s ***\n", src, target);
  // fprintf(stderr, "*** Size:  %d -> %d ***\n", req.message.body.srclen, req.message.body.targetlen);

  return 0;
}
int inotify(csiebox_client* client, int fd, inopare* h) {

  int length, i = 0;
  char buffer[EVENT_BUF_LEN];
  memset(buffer, 0, EVENT_BUF_LEN);

  while(1) {

    if( !server_available(client->conn_fd) ) {
      sleep(3);
      continue;
    }

    while ((length = read(fd, buffer, EVENT_BUF_LEN)) > 0) {
      // puts("AN EVENT");
      i = 0;
      while (i < length) {
        struct inotify_event* event = (struct inotify_event*)&buffer[i];
  
        char path[1024], cpath[1024];
        for( int j=0; j<200; j++ ) {
          if( h[j].wd == event->wd ) {
            sprintf(cpath, ".%s/%s", (h[j].path)+1, event->name );
            sprintf(path, "%s%s/%s",client->arg.user ,(h[j].path)+1, event->name );
            break;
          }
        }
  
        printf("event: (%d, %d, %s, %s)\ntype: ", event->wd, strlen(event->name), event->name, path);
        
        if (event->mask & IN_ISDIR) {
          printf("[dir]  ");
        } else {
          printf("[file] ");
        }
  
        if (event->mask & IN_CREATE) {
          printf("create \n");
          cpy_file(client, cpath);
          if( event->mask & IN_ISDIR ) {
            int wd = inotify_add_watch(fd, cpath, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
            for( int ii=0; ii<200; ii++ ) 
              if( h[ii].wd == 0 ) {
                h[ii].wd = wd;
                sprintf(h[ii].path, "%s", cpath);
                // fprintf(stderr, "inotify<%d> %s\n", wd, cpath);
                break;
              }
          }
        }
        else if (event->mask & IN_DELETE) {
          printf("delete \n");
  
          csiebox_protocol_rm req;
          memset(&req, 0, sizeof(req));
          req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
          req.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
          req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
          req.message.body.pathlen = strlen(path);
          send_message( client->conn_fd, &req, sizeof(req) );
          send_message( client->conn_fd, path, strlen(path) );
        }
        else if (event->mask & IN_ATTRIB) {
          printf("attrib \n");
          cpy_file(client, cpath);
        }
        else if (event->mask & IN_MODIFY) {
          printf("modify \n");
          cpy_file(client, cpath);
        }
  
        i += EVENT_SIZE + event->len;
      }
      memset(buffer, 0, EVENT_BUF_LEN);
  
      csiebox_protocol_header req;
      memset(&req, 0, sizeof(req));
      req.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
      req.req.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
      send_message( client->conn_fd, &req, sizeof(req) );
  
    }

    break;
  }

  //inotify_rm_watch(fd, wd);
  close(fd);
  return 0;
}
int little_tree_walk( char* prefix, int inofd, inopare myhash[200] ){

  DIR * dp = opendir(prefix);
  struct dirent * entry;
  struct stat statbuf;
  int i;

  int wd = inotify_add_watch(inofd, prefix, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
  for( int ii=0; ii<200; ii++ ) {
    if( myhash[ii].wd == 0 ) {
      myhash[ii].wd = wd;
      sprintf(myhash[ii].path, "%s", prefix);
      fprintf(stderr, "inotify<%d> %s\n", wd, prefix);
      break;
    }
  }

  while ( (entry = readdir(dp) ) != NULL) {  
    char cpath[1024];
    sprintf(cpath ,"%s/%s", prefix, entry->d_name);
    lstat(cpath, &statbuf);  

    if (S_ISDIR(statbuf.st_mode)) {  
      if (strcmp(entry->d_name, ".") == 0 || 
        strcmp(entry->d_name, "..") == 0 )    
        continue;     
      little_tree_walk( cpath, inofd, myhash );  
    } 

  } 

  closedir(dp);
  return 0;
}
static void handle_request(int conn_fd) {

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  //WHILE (X)
  int gogo = 1;

  while ( gogo ) {
    recv_message(conn_fd, &header, sizeof(header));
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      continue;
    }
    switch (header.req.op) {

      case CSIEBOX_PROTOCOL_OP_SYNC_META:
        fprintf(stderr, "sync meta\n");
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
            FILE * fp;
            if( !(fp = fopen(path, "w+") ) ) fprintf(stderr, "open failed\n");
            struct stat st;
            stat(path, &st);
            if( meta.message.body.stat.st_mode != st.st_mode ||
                meta.message.body.stat.st_uid  != st.st_uid  ||
                meta.message.body.stat.st_gid  != st.st_gid)
              chmod( path, meta.message.body.stat.st_mode );

            //check hash
            csiebox_protocol_status ok = CSIEBOX_PROTOCOL_STATUS_OK;
            uint8_t tmphash[MD5_DIGEST_LENGTH];
            memset(&tmphash, 0, sizeof(tmphash));
            if ( !md5_file(path, tmphash) ) puts("hash err");
            if( memcmp(&tmphash, &meta.message.body.hash, sizeof(tmphash) ) ) 
              ok = CSIEBOX_PROTOCOL_STATUS_MORE;
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
              fclose(fp);

            }    
            struct utimbuf timbuf;
            memset(&timbuf, 0, sizeof(timbuf));
            timbuf.actime = meta.message.body.stat.st_atime;
            timbuf.modtime = meta.message.body.stat.st_mtime;
            // fprintf(stderr, "before:%d  after:%d\n", meta.message.body.stat.st_mtime, timbuf.modtime);
            if( utime(path, &timbuf) != 0 ) fprintf(stderr, "utime err (%s)\n", path);
            // stat(path, &st);
            // fprintf(stderr, "final: %d\n", st.st_mtime);        
          }

        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\n");
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
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "sync end\n");
        csiebox_protocol_header end;
        gogo = 0;
      break;

      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
        break;
    }
  }
}

int server_available( int conn_fd ) {

  csiebox_protocol_status status = CSIEBOX_PROTOCOL_STATUS_MORE;

  //Send request to check if server is available
  if( !send_message( conn_fd, &status, sizeof(status) ) ) {
    fprintf(stderr, "send status failed\n");
    return 0;
  }
  //Recv from server
  if( !recv_message( conn_fd, &status, sizeof(status) ) ) {
    fprintf(stderr, "recv status failed\n");
    return 0;
  }

  fprintf(stderr, "%d status: %d\n", conn_fd, status);
  if( status == CSIEBOX_PROTOCOL_STATUS_OK ) return 1; //server is available
  else fprintf(stderr, "Server busy!\n");
  return 0;

}

  // char cwd[1024];
  // getcwd(cwd, sizeof(cwd));
  // fprintf(stderr, "now at %s\n", cwd); 

