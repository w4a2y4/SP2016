#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"
#include "hash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h> //header for inotify
#include <unistd.h>
#include <dirent.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);
int tree_walk( char* home, char* prefix, char* dir , csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] );
int cpy_file(csiebox_client* client, const char* tmppath, const char* longpath  );
int cpy_hlink(csiebox_client* client, const char* tmppath, const char* longpath, char* target );
int inotify(csiebox_client* client, int fd, inopare* h);
int tree_walk2( char* prefix, csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] );
int cpy_file2(csiebox_client* client, const char* cpath);
int cpy_hlink2(csiebox_client* client, const char* cpath, char* target );


char client_home[100];

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
  fprintf(stderr, "===========login success===========\n");

  int inofd = inotify_init();
  if (inofd < 0) perror("inotify_init");

  //sync data
  printf("home:%s\n", client_home);
  if( chdir( client_home ) == -1 ){
    fprintf(stderr, "chdir fail\n");
    return 0;
  };
  // getcwd(cwd, sizeof(cwd));
  // fprintf(stderr, "chdir to %s\n", cwd);
  lnkpare hlist[100];
  memset(hlist, 0, sizeof(hlist));
  // tree_walk( ".", ".", "." , client, hlist, inofd, myhash);
  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
  fprintf(stderr, "now at %s\n", cwd); 
  tree_walk2( ".", client, hlist, inofd, myhash);

  fprintf(stderr, "=========sync data success=========\n");

  inotify(client, inofd, myhash );

  // csiebox_protocol_header endreq;
  // memset(&endreq, 0, sizeof(endreq));
  // endreq.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  // endreq.op = CSIEBOX_PROTOCOL_OP_SYNC_END;

  //start inotify
  
  
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
        strncpy(client_home, val, vallen);
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

int tree_walk( char* home, char* prefix, char* dir, csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] ){

  DIR * dp = opendir(dir);
  struct dirent * entry;
  struct stat statbuf;
  int i;

  char cwd[1024];
  getcwd(cwd, sizeof(cwd));

  if( chdir( home ) == -1 ){
    fprintf(stderr, "chdir fail(home)\n");
    return 0;
  }

  fprintf(stderr, "HOME: %s\n", home);

  char cmd[1024];
  puts(cwd);
  sprintf(cmd, "ls %s", prefix);
  system(cmd);

  // int wd = inotify_add_watch(inofd, prefix, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
  // for( int ii=0; ii<200; ii++ ) {
  //   if( myhash[ii].wd == 0 ) {
  //     myhash[ii].wd = wd;
  //     sprintf(myhash[ii].path, "%s", prefix);
  //     fprintf(stderr, "inotify: %d %s\n", wd, prefix);
  //     break;
  //   }
  // }

  if( chdir( cwd ) == -1 ){
    fprintf(stderr, "chdir fail(cwd)\n");
    return 0;
  }
  if( chdir( dir ) == -1 ){
    fprintf(stderr, "chdir fail(dir)\n");
    return 0;
  }

  while ( (entry = readdir(dp) ) != NULL) {  
    lstat(entry->d_name, &statbuf);  
    char prefix2[strlen(prefix)+strlen(entry->d_name)+2];
    sprintf( prefix2, "%s/%s", prefix, entry->d_name); 

    if (S_ISDIR(statbuf.st_mode)) {  
      if (strcmp(entry->d_name, ".") == 0 || 
        strcmp(entry->d_name, "..") == 0 )    
        continue;     
      printf("DIR %s\n", prefix2); 

      char newhome[1024];
      sprintf(newhome, "%s..", home);
      cpy_file(client, entry->d_name, prefix2); 
      tree_walk(newhome, prefix2, entry->d_name, client, hlist, inofd, myhash );  
    } 
    else if ( S_ISREG(statbuf.st_mode) ) {
      printf("REG %s\n", prefix2);  
      if( statbuf.st_nlink > 1 ) {
        //check if hardlink already exists
        int exist = 0, i=0;
        for( i=0; i<100; i++ ) {
          if( hlist[i].inode == statbuf.st_ino ) {
            cpy_hlink( client, entry->d_name, prefix2, hlist[i].oldpath );
            exist = 1;
            break;
          }
          if( hlist[i].inode == 0 ) break;
        }
        if( !exist ) {
          hlist[i].inode = statbuf.st_ino;
          sprintf(hlist[i].oldpath, "%s%s", client->arg.user, prefix2+1);
          cpy_file(client, entry->d_name, prefix2);
        }
      }
      else {
        cpy_file(client, entry->d_name, prefix2); 
      }
    }
    else if ( S_ISLNK(statbuf.st_mode) ) {
      printf("LNK %s\n", prefix2);  
      cpy_file(client, entry->d_name, prefix2); 
    }

  } 
  chdir("..");
  closedir(dp);
  return 0;
}

int tree_walk2( char* prefix, csiebox_client* client, lnkpare hlist[100], int inofd, inopare myhash[200] ){

  DIR * dp = opendir(prefix);
  struct dirent * entry;
  struct stat statbuf;
  int i;

  // int wd = inotify_add_watch(inofd, prefix, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
  // for( int ii=0; ii<200; ii++ ) {
  //   if( myhash[ii].wd == 0 ) {
  //     myhash[ii].wd = wd;
  //     sprintf(myhash[ii].path, "%s", prefix);
  //     fprintf(stderr, "inotify: %d %s\n", wd, prefix);
  //     break;
  //   }
  // }

  while ( (entry = readdir(dp) ) != NULL) {  
    char cpath[1024];
    sprintf(cpath ,"%s/%s", prefix, entry->d_name);
    lstat(cpath, &statbuf);  
    // char prefix2[strlen(prefix)+strlen(entry->d_name)+2];
    // sprintf( prefix2, "%s/%s", prefix, entry->d_name); 

    if (S_ISDIR(statbuf.st_mode)) {  
      if (strcmp(entry->d_name, ".") == 0 || 
        strcmp(entry->d_name, "..") == 0 )    
        continue;     
      printf("DIR %s\n", cpath); 

      // cpy_file(client, entry->d_name, prefix2); 
      tree_walk2( cpath, client, hlist, inofd, myhash );  
    } 
    else if ( S_ISREG(statbuf.st_mode) ) {
      printf("REG %s\n", cpath);  
      if( statbuf.st_nlink > 1 ) {
        //check if hardlink already exists
        int exist = 0, i=0;
        for( i=0; i<100; i++ ) {
          if( hlist[i].inode == statbuf.st_ino ) {
            cpy_hlink2( client, cpath, hlist[i].oldpath );
            exist = 1;
            break;
          }
          if( hlist[i].inode == 0 ) break;
        }
        if( !exist ) {
          hlist[i].inode = statbuf.st_ino;
          sprintf(hlist[i].oldpath, "%s%s", client->arg.user, cpath+1);
          cpy_file2(client, cpath);
        }
      }
      else {
        cpy_file2(client, cpath); 
      }
    }
    else if ( S_ISLNK(statbuf.st_mode) ) {
      printf("LNK %s\n", cpath);  
      cpy_file2(client, cpath); 
    }

  } 

  closedir(dp);
  return 0;
}

int cpy_file(csiebox_client* client, const char* tmppath, const char* longpath ){

  char path[1024];
  sprintf( path, "%s%s", client->arg.user, longpath+1); 

  //first send meta request
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.pathlen = strlen(path);
  // printf("longpath : %s\n", longpsath);
  if( lstat(tmppath, &req.message.body.stat) ) puts("lstat fail");
  // printf("mode : %d\n", req.message.body.stat.st_mode);

  if( S_ISREG( req.message.body.stat.st_mode ) )
    if ( !md5_file(tmppath, req.message.body.hash) ) puts("hash fail");

  if ( !send_message(client->conn_fd, &req, sizeof(req))) fprintf(stderr, "send fail\n"); 

  // send data
  if ( !send_message(client->conn_fd, path, strlen(path)*sizeof(char) ) ) {
    fprintf(stderr, "send fail\n"); 
    return 0;
  }

  if( S_ISDIR(req.message.body.stat.st_mode) ) return 0;
  else if( S_ISLNK(req.message.body.stat.st_mode) ) {

    char buff[req.message.body.stat.st_size];
    char * spath;
    readlink(tmppath, buff, req.message.body.stat.st_size);

    if( ( spath =  strstr(buff, client->arg.path) ) != NULL ) {
      spath += strlen( client->arg.path ) + 1;
      char newpath[1024];
      sprintf(newpath, "%s/%s", client->arg.user, spath);
      // fprintf(stderr, "!!  %s\n", newpath);
      int newpathlen = strlen(newpath);
      send_message(client->conn_fd, &newpathlen, sizeof(int) );
      send_message(client->conn_fd, &newpath, newpathlen );
    }
    else {
      // fprintf(stderr, "%s\n", buff);
      int bufflen = strlen(buff);
      send_message(client->conn_fd, &bufflen, sizeof(int) );
      send_message(client->conn_fd, &buff, bufflen );
    }

    }
  else if( S_ISREG(req.message.body.stat.st_mode) ) {
    csiebox_protocol_status ok;
    if( !recv_message( client->conn_fd, &ok, sizeof(csiebox_protocol_status)) ) 
      fprintf(stderr, "recv error\n" );

    if( ok == CSIEBOX_PROTOCOL_STATUS_MORE )  { 
      // fprintf(stderr, "\tMORE\n");
      // send data
      send_message(client->conn_fd, &req.message.body.stat.st_size, sizeof(off_t) );
      FILE * fp = fopen( tmppath, "r" );
      char buff[1024];
      for( int i=0; i<(req.message.body.stat.st_size/1024); i++ ) {
        fread( &buff, 1024, 1, fp );
        send_message( client->conn_fd, &buff, 1024);
      }
      fread( &buff, req.message.body.stat.st_size%1024, 1, fp );
      send_message( client->conn_fd, &buff, req.message.body.stat.st_size%1024 );
      fclose(fp);
    }
    // else if( ok == CSIEBOX_PROTOCOL_STATUS_OK )  
    //   fprintf(stderr, "\tOK\n");
    // else fprintf(stderr, "\tERRRRRRR\n");

  }

  else fprintf(stderr, "\tERR\n");

  return 0;
}

int cpy_file2(csiebox_client* client, const char* cpath){

  char spath[1024];
  sprintf( spath, "%s%s", client->arg.user, cpath+1); 

  //first send meta request
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.pathlen = strlen(spath);
  // printf("longpath : %s\n", longpsath);
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
    // char * spath;
    readlink(cpath, buff, req.message.body.stat.st_size);

    // if( ( spath =  strstr(buff, client->arg.path) ) != NULL ) {
    //   spath += strlen( client->arg.path ) + 1;
    //   char newpath[1024];
    //   sprintf(newpath, "%s/%s", client->arg.user, spath);
    //   // fprintf(stderr, "!!  %s\n", newpath);
    //   int newpathlen = strlen(newpath);
    //   send_message(client->conn_fd, &newpathlen, sizeof(int) );
    //   send_message(client->conn_fd, &newpath, newpathlen );
    // }
    // else {
      // fprintf(stderr, "%s\n", buff);
      int bufflen = strlen(buff);
      send_message(client->conn_fd, &bufflen, sizeof(int) );
      send_message(client->conn_fd, &buff, bufflen );
    // }

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
      char buff[1024];
      for( int i=0; i<(req.message.body.stat.st_size/1024); i++ ) {
        fread( &buff, 1024, 1, fp );
        send_message( client->conn_fd, &buff, 1024);
      }
      fread( &buff, req.message.body.stat.st_size%1024, 1, fp );
      send_message( client->conn_fd, &buff, req.message.body.stat.st_size%1024 );
      fclose(fp);
    }
    // else if( ok == CSIEBOX_PROTOCOL_STATUS_OK )  
    //   fprintf(stderr, "\tOK\n");
    // else fprintf(stderr, "\tERRRRRRR\n");

  }

  else fprintf(stderr, "\tERR\n");

  return 0;
}

int cpy_hlink(csiebox_client* client, const char* tmppath, const char* longpath, char* target ) {
  
  char src[1024];
  sprintf( src, "%s%s", client->arg.user, longpath+1); 
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

int cpy_hlink2(csiebox_client* client, const char* cpath, char* target ) {
  
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

  while ((length = read(fd, buffer, EVENT_BUF_LEN)) > 0) {
    i = 0;
    while (i < length) {
      struct inotify_event* event = (struct inotify_event*)&buffer[i];
      char* path;
      for( int j=0; j<200; j++ ) {
        if( h[i].wd == event->wd ) {
          sprintf(path, "%s/%s",h[i].path, event->name );
          break;
        }
      }
      printf("event: (%d, %d, %s, %s)\ntype: ", event->wd, strlen(event->name), event->name, path);
      if (event->mask & IN_CREATE) {
        printf("create ");
      }
      else if (event->mask & IN_DELETE) {
        printf("delete ");

        // csiebox_protocol_rm req;
        // memset(&req, 0, sizeof(req));
        // req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        // req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_RM;
        // req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
        // req.message.body.pathlen = strlen(path);
        // send_message( client->conn_fd, &req, sizeof(req) );


      }
      else if (event->mask & IN_ATTRIB) {

        // printf("attrib ");
        // csiebox_protocol_meta req;
        // memset(&req, 0, sizeof(req));
        // req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        // req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
        // req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
        // req.message.body.pathlen = strlen(path);
        // send_message( client->conn_fd, &req, sizeof(req) );

      }
      else if (event->mask & IN_MODIFY) {
        printf("modify ");
      }
      if (event->mask & IN_ISDIR) {
        printf("dir\n");
      } else {
        printf("file\n");
      }
      i += EVENT_SIZE + event->len;
    }
    memset(buffer, 0, EVENT_BUF_LEN);
  }

  //inotify_rm_watch(fd, wd);
  close(fd);
  return 0;
}


  // char cwd[1024];
  // getcwd(cwd, sizeof(cwd));
  // fprintf(stderr, "now at %s\n", cwd); 

