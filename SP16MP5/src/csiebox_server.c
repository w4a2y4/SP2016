#include "csiebox_server.h"
#include "csiebox_common.h"
#include "csiebox_server_handle_request.h"
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

void* handle_requests_loop(void* data);
static int parse_arg(csiebox_server* server, int argc, char** argv);

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

void* handle_requests_loop(void* data) {

  int rc;                     /* return code of pthreads functions.  */
  int tid = *((int*)data);    /* thread identifying number           */
  printf("%d:: Starting thread\n", tid); fflush(stdout);
  tst[tid] = 0;
  rc = pthread_mutex_lock(&cond_mutex); //grab cond_mutex first

  while (1) {
    tst[tid] = 0; //the thread is waiting
    rc = pthread_cond_wait(&cond, &cond_mutex);
    tst[tid] = 1; //the thread is running
    if ( request.ready_to_read ) { // a request is pending 
        printf("%d:: handle request\n", tid); fflush(stderr);
        //lock the request to modify its content
        rc = pthread_mutex_lock(&req_mutex); 
        csiebox_server* s = request.server;
        int fd = request.conn_fd;
        request.ready_to_read = 0;
        rc = pthread_mutex_unlock(&req_mutex);
        if( handle_request(s, fd) ) {
          fprintf(stderr, "LOGOUT\n");  fflush(stderr);
          FD_CLR( fd, &master); //stop listening from the fd
        }
        FD_CLR( fd, &ing );
    }
    else {
      fprintf(stderr, "%d:: request not ready\n", tid);
    }
  }
}

void sig_handler( int signo ) {

  if( signo == SIGUSR1) {
    fprintf(stderr, "SIGUSR1\n");

    // char cfg_path[PATH_MAX];
    // sprintf( cfg_path, "%s/csiebox_server.pid", SERVER->arg.run_path );
    // int cfg_fd = open(cfg_path, O_RDONLY);


    // int spid=0; char bufchar;
    // int rt = read(cfg_fd, &bufchar, sizeof(char));
    // while( rt == 1 && bufchar != '\n' ) {
    //   spid *= 10;
    //   spid += bufchar - '0';
    //   fprintf(stderr, "%c %d | ", bufchar, spid);
    //   rt = read(cfg_fd, &bufchar, sizeof(char));
    // }
    // fprintf(stderr, "\npid = %d\n", spid);
    // close(cfg_fd);

    // char fifofile[PATH_MAX];
    // sprintf( fifofile, "%s/csiebox_server.%d", SERVER->arg.run_path, spid );
    // int fifo = open(fifofile, O_WRONLY);

    // uint32_t tnum = 0;
    // for( int i=0; i< SERVER->arg.thread_num; i++ )
    //   if( tst[i] ) tnum++;
    // tnum = htonl(tnum);
    // write(fifo, &tnum, sizeof(uint32_t));
    // close(fifo);
  }
  else if ( signo == SIGINT ) {
    fprintf(stderr, "SIGINT\n");
  }
  else if ( signo == SIGTERM ) {
    fprintf(stderr, "SIGTERM\n");
  }
  else {
    fprintf(stderr, "SIGNAL = %d\n", signo);
  }


}

//read config file, and start to listen
void csiebox_server_init( csiebox_server** server, int argc, char** argv) {
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

  //=============================================
  pthread_cond_init( &cond, NULL );
  pthread_mutex_init( &cond_mutex, NULL );
  pthread_mutex_init( &req_mutex, NULL );
  request.ready_to_read = 0;

  int        thr_id[tmp->arg.thread_num];      /* thread IDs            */
  pthread_t  p_threads[tmp->arg.thread_num];   /* thread's structures   */
  
  /* create the request-handling threads */
  for (int i=0; i<tmp->arg.thread_num; i++) {
    thr_id[i] = i;
    pthread_create(&p_threads[i], NULL, handle_requests_loop, (void*)&thr_id[i]);
    sleep(1);
  }

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
    fprintf(stderr, "%c %d | ", bufchar, spid);
    rt = read(cfg_fd, &bufchar, sizeof(char));
  }
  
  fprintf(stderr, "\npid = %d\n", spid);
  close(cfg_fd);

  char fifofile[PATH_MAX];
  sprintf( fifofile, "%s/csiebox_server.%d", tmp->arg.run_path, spid );
  mkfifo(fifofile, 0666);

  struct sigaction act;
  act.sa_handler = sig_handler;
  sigaction( SIGUSR1, &act, NULL );
  sigaction( SIGTERM, &act, NULL );
  sigaction( SIGINT, &act, NULL );

  tmp->pool.threads = p_threads;
  tmp->pool.tid = thr_id;

  *server = tmp;
  SERVER = tmp;
  return;
}

int csiebox_server_run(csiebox_server* server) {
  // int conn_fd, conn_len;
  struct sockaddr_in addr;

  if ( chdir( server->arg.path ) == -1 ) fprintf(stderr, "chdir fail(server)\n");

  FD_ZERO(&master);
  FD_ZERO(&ing);
  FD_SET(server->listen_fd, &master);
  int fd_max = server->listen_fd;

  while (1) {
    memset(&addr, 0, sizeof(addr));
    // sleep(2);

    // waiting client connect
    fd_set read_fds = master;   
    sleep(1); 
    if ( select(fd_max+1, &read_fds, NULL, NULL, NULL) == -1 ) {
      fprintf(stderr, "\nselect failed\n");
      return 0;
    }

    for( int fd_ok=0; fd_ok<=fd_max; fd_ok++ ) {

      if ( FD_ISSET(fd_ok, &read_fds) && !FD_ISSET(fd_ok, &ing) ) {

        if( fd_ok == server->listen_fd ) {  //new connection
          int conn_fd, conn_len=0;
          // waiting client to connect
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
          handle_request(server, conn_fd);
        }

        else {

          //========================================

          //recv client's request to check if server's available
          csiebox_protocol_status status;
          if( !recv_message( fd_ok, &status, sizeof(status) ) ) {
            fprintf(stderr, "recv status failed\n");
            continue;
          }
          if( status != CSIEBOX_PROTOCOL_STATUS_MORE ){
            fprintf(stderr, "wrong message\n");
            status = CSIEBOX_PROTOCOL_STATUS_FAIL;
            send_message( fd_ok, &status, sizeof(status) );
            continue;
          }

          int rc = pthread_mutex_lock(&req_mutex);
          sleep(1);

          int busy = 1;
          for( int cnt=0; cnt<server->arg.thread_num; cnt++ )
            if( tst[cnt] == 0 ) {
              busy = 0;
              break;
            }
          if( busy ) {
            status = CSIEBOX_PROTOCOL_STATUS_FAIL;
            send_message( fd_ok, &status, sizeof(status) );
            rc = pthread_mutex_unlock(&req_mutex);
            sleep(1);
          }

          else {
            status = CSIEBOX_PROTOCOL_STATUS_OK;
            FD_SET( fd_ok, &ing );

            send_message( fd_ok, &status, sizeof(status) );
            request.conn_fd = fd_ok;
            request.server = server;
            request.ready_to_read = 1;
  
            rc = pthread_mutex_unlock(&req_mutex);
            rc = pthread_cond_signal(&cond);
            sleep(1);
          }
          //========================================


        }

      }

    }

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

// int daemon_init(csiebox_server* server ) {

//   FILE *fp= NULL;
//   pid_t process_id = 0;
//   pid_t sid = 0;

//   // Create child process
//   process_id = fork();
//   // Indication of fork() failure
//   if (process_id < 0) {
//     printf("fork failed!\n");
//     // Return failure in exit status
//     exit(1);
//   }
  
//   // PARENT PROCESS. Need to kill it.
//   if (process_id > 0) {

//     //write new pid to 

//     printf("process_id of child process %d \n", process_id);
//     // return success in exit status
//     exit(0);
//   }

//   //unmask the file mode
//   umask(0);
//   //set new session
//   sid = setsid();
//   if(sid < 0) {
//    // Return failure
//     exit(1); 
//   }
//   // Change the current working directory to root, required root permission, or tmp
//   if ( chdir("/tmp") < 0 )
//     fprintf(STDERR_FILENO, "Cannot switch to tmp directory");
//   /*
//   * Attach file descriptors 0, 1, and 2 to /dev/null.
//   */
//   fd0 = open("/dev/null", O_RDWR); //STDIN
//   fd1 = dup(0); // STDOUT
//   fd2 = dup(0); // STDERR

//   // Open a log file in write mode.
//   fp = fopen ("Log.txt", "w+");
//   while (1) {
//     //Dont block context switches, let the process sleep for some time
//     sleep(1);
//     fprintf(fp, "Logging info...\n");
//     fflush(fp);
//     // Start your dropbox client and server here.
//     csiebox_server* box = 0;
//     csiebox_server_init(&box, argc, argv);
//     if (box) {
//       csiebox_server_run(box);
//     }
//     csiebox_server_destroy(&box);

//   }
//   fclose(fp);
//   return (0); 
// }

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
  if (argc != 2 && argc != 3 ) {
    return 0;
  }
  // if (argc == 3) {
  //   if( argv[2] != "-d" ) return 0;
  // }
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
    } else if (strcmp("run_path", key) == 0) {
      if (vallen <= sizeof(server->arg.run_path)) {
        strncpy(server->arg.run_path, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("thread_num", key) == 0) {
      if (vallen == 1) {  //thread_num = 0~9
        server->arg.thread_num = val[0] - '0';
        // printf("thread! %d\n", server->arg.thread_num);
        accept_config[1] = 1;
      }
      if (vallen == 2) {  //thread_num = 10~19
        server->arg.thread_num = val[1] - '0' + 10;
        // printf("thread! %d\n", server->arg.thread_num);
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

  // if( argc == 3 ) {
  //   daemon_init(server);
  // }

  return 1;
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

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

