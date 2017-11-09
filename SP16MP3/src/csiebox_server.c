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
#include <sys/file.h>
#include <signal.h>

csiebox_server *SERVER;
fd_set *rd, *mas;
int *fdmax; 

static int parse_arg(csiebox_server* server, int argc, char** argv);
int handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);

int tree_walk( char* prefix, int conn_fd, lnkpare hlist[100]);
int cpy_file(int conn_fd, const char* cpath);
int cpy_hlink(int conn_fd, const char* cpath, char* target );

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

void sig_handler( int signo ) {

  if( signo == SIGUSR1) {
    fprintf(stderr, "=====SIGUSR1=====\n");

    fprintf(stderr, "\nread:\t");
    for( int fd_ok=0; fd_ok<=*fdmax; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, rd) );
    fprintf(stderr, "\nmaster:\t");
    for( int fd_ok=0; fd_ok<=*fdmax; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, mas) );

    fprintf(stderr, "\n=================\n");

    // char cfg_path[PATH_MAX];
    // sprintf( cfg_path, "%s/csiebox_server.pid", SERVER->arg.run_path );
    // int cfg_fd = open(cfg_path, O_RDONLY);

    // int spid=0; char bufchar;
    // int rt = read(cfg_fd, &bufchar, sizeof(char));
    // while( rt == 1 && bufchar != '\n' ) {
    //   spid *= 10;
    //   spid += bufchar - '0';
    //   // fprintf(stderr, "%c %d | ", bufchar, spid);
    //   rt = read(cfg_fd, &bufchar, sizeof(char));
    // }
    // fprintf(stderr, "pid = %d\n", spid);
    // close(cfg_fd);

    // char fifofile[PATH_MAX];
    // sprintf( fifofile, "%s/csiebox_server.%d", SERVER->arg.run_path, spid );
    // int fifo = open(fifofile, O_WRONLY);

    // uint32_t tnum = 0;
    // // for( int i=0; i< SERVER->arg.thread_num; i++ )
    // //   if( tst[i] ) tnum++;
    // tnum = htonl(tnum);
    // write(fifo, &tnum, sizeof(uint32_t));
    // close(fifo);
  }
  else if ( signo == SIGINT ) {
    fprintf(stderr, "SIGINT\n");
    exit(0);
  }
  else if ( signo == SIGTERM ) {
    fprintf(stderr, "SIGTERM\n");
    exit(0);
  }
  else {
    fprintf(stderr, "SIGNAL = %d\n", signo);
  }

  return;
}

int daemon_init(csiebox_server* server ) {

  FILE *fp= NULL;
  pid_t process_id = 0;
  pid_t sid = 0;

  // Create child process
  process_id = fork();
  // Indication of fork() failure
  if (process_id < 0) {
    printf("fork failed!\n");
    // Return failure in exit status
    exit(1);
  }
  
  // PARENT PROCESS. Need to kill it.
  if (process_id > 0) {

    //write new pid to 

    printf("process_id of child process %d \n", process_id);
    // return success in exit status
    exit(0);
  }

  //unmask the file mode
  umask(0);
  //set new session
  sid = setsid();
  if(sid < 0) {
   // Return failure
    exit(1); 
  }
  // Change the current working directory to root, required root permission, or tmp
  if ( chdir("/tmp") < 0 )
    fprintf(STDERR_FILENO, "Cannot switch to tmp directory");
  /*
  * Attach file descriptors 0, 1, and 2 to /dev/null.
  */
  fd0 = open("/dev/null", O_RDWR); //STDIN
  fd1 = dup(0); // STDOUT
  fd2 = dup(0); // STDERR

  // Open a log file in write mode.
  fp = fopen ("Log.txt", "w+");
  while (1) {
    //Dont block context switches, let the process sleep for some time
    sleep(1);
    fprintf(fp, "Logging info...\n");
    fflush(fp);
    // Start your dropbox client and server here.
    csiebox_server* box = 0;
    csiebox_server_init(&box, argc, argv);
    if (box) {
      csiebox_server_run(box);
    }
    csiebox_server_destroy(&box);

  }
  fclose(fp);
  return (0); 
}

//read config file, and start to listen
void csiebox_server_init(
  csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)
      malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;


  char cfg_path[PATH_MAX];
  sprintf( cfg_path, "%s/csiebox_server.pid", tmp->arg.run_path );
  fprintf(stderr, "cfg_path: %s\n", cfg_path);
  int cfg_fd = open(cfg_path, O_RDONLY);
  if( !cfg_fd ) 
    fprintf(stderr, "Open csiebox_server.pid failed\n");

  int spid=0; char bufchar;
  int rt = read(cfg_fd, &bufchar, sizeof(char));
  while( rt == 1 && bufchar != '\n' ) {
    spid *= 10;
    spid += bufchar - '0';
    // fprintf(stderr, "%c %d | ", bufchar, spid);
    rt = read(cfg_fd, &bufchar, sizeof(char));
  }
  
  fprintf(stderr, "pid = %d\n", spid);
  close(cfg_fd);

  char fifofile[PATH_MAX];
  sprintf( fifofile, "%s/csiebox_server.%d", tmp->arg.run_path, spid );
  mkfifo(fifofile, 0666);

  struct sigaction act;
  act.sa_handler = sig_handler;
  act.sa_flags = SA_NODEFER;
  sigaction( SIGUSR1, &act, NULL );
  sigaction( SIGTERM, &act, NULL );
  sigaction( SIGINT, &act, NULL );


  SERVER =  tmp;
  *server = tmp;
}

int csiebox_server_run(csiebox_server* server) {
  // int conn_fd, conn_len;
  struct sockaddr_in addr;

  if ( chdir( server->arg.path ) == -1 ) fprintf(stderr, "chdir fail(server)\n");

  fd_set master;
  mas = &master;
  FD_ZERO(&master);
  FD_SET(server->listen_fd, &master);
  int fd_max = server->listen_fd;
  fdmax = &fd_max;

  while (1) {
    memset(&addr, 0, sizeof(addr));

    // waiting client connect
    fd_set read_fds = master;
    rd = &read_fds;

    fprintf(stderr, "\nread:\t");
    for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, &read_fds) );
    fprintf(stderr, "\nmaster:\t");
    for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, &master) );
    
    if ( select(fd_max+1, &read_fds, NULL, NULL, NULL) == -1 ) {
      fprintf(stderr, "\nselect failed\n");
      return 0;
    }

    for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) {
      // fprintf(stderr, "%d  ", fd_ok);
      if ( FD_ISSET(fd_ok, &read_fds) ) {

        if( fd_ok == server->listen_fd ) {
          int conn_fd, conn_len=0;
          conn_fd = accept(
            server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
          if (conn_fd < 0) {
            if (errno == ENFILE) {
                fprintf(stderr, "out of file descriptor table\n");
                continue;
              } else if (errno == EAGAIN || errno == EINTR) {
                continue;
              } else {
                fprintf(stderr, "accept err\n");
                fprintf(stderr, "code: %s\n", strerror(errno));
                break;
              }
          }

          FD_SET(conn_fd, &master);
          if (conn_fd > fd_max) fd_max = conn_fd;
          fprintf(stderr, "new connection: %d\n", conn_fd);

        }
        else {
          fprintf(stderr, "handle req (%d)\n", fd_ok);
          // handle request from connected socket fd
          if( handle_request(server, fd_ok) ) {
            FD_CLR( fd_ok, &master);
          }
        }

      }

    }

    // fprintf(stderr, "read:\t");
    // for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, &read_fds) );
    // fprintf(stderr, "\nmaster:\t");
    // for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) fprintf(stderr, "%d ", FD_ISSET(fd_ok, &master) );


  }
  return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  close(tmp->listen_fd);
  int i = getdtablesize() - 1;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    return 0;
  }
  if ( argc == 3 && argv[2]!="-d" )
    return 0;
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 2;
  int accept_config[2] = {0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(server->arg.path)) {
        strncpy(server->arg.path, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("account_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.account_path, val, vallen);
        accept_config[1] = 1;
      }
    }else if (strcmp("run_path", key) == 0) {
      if (vallen <= sizeof(server->arg.run_path)) {
        strncpy(server->arg.run_path, val, vallen);
        accept_config[1] = 1;
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

//this is where the server handle requests, you should write your code here
int handle_request(csiebox_server* server, int conn_fd) {
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  //WHILE (X)
  // while (recv_message(conn_fd, &header, sizeof(header))) {
  if (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      // continue;
      return 0;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_LOGIN:
        fprintf(stderr, "login\n");
        csiebox_protocol_login req;
        if (complete_message_with_header(conn_fd, &header, &req)) {
          login(server, conn_fd, &req);
        }
        break;
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
          // chdir( server->arg.path );
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
          // chdir( ".." );

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
        fprintf(stderr, "download request\n");
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
      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
        break;
    }
  }
  else {
    logout(server, conn_fd);
    return 1;
  }
  fprintf(stderr, "=========end of connection=========\n");
  return 0;
}

//open account file to get account information
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info) {
  FILE* file = fopen(server->arg.account_path, "r");
  if (!file) {
    return 0;
  }
  size_t buflen = 100;
  char* buf = (char*)malloc(sizeof(char) * buflen);
  memset(buf, 0, buflen);
  ssize_t len;
  int ret = 0;
  int line = 0;
  while ((len = getline(&buf, &buflen, file) - 1) > 0) {
    ++line;
    buf[len] = '\0';
    char* u = strtok(buf, ",");
    if (!u) {
      fprintf(stderr, "illegal form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "illegal form in account file, line %d\n", line);
        continue;
      }
      md5(passwd, strlen(passwd), info->passwd_hash);
      ret = 1;
      break;
    }
  }
  free(buf);
  fclose(file);
  return ret;
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

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
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

