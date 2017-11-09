#ifndef _CSIEBOX_SERVER_
#define _CSIEBOX_SERVER_

#ifdef __cplusplus
extern "C" {
#endif

#include "csiebox_common.h"
#include <limits.h>

typedef struct {
  char user[USER_LEN_MAX];
  char passwd_hash[MD5_DIGEST_LENGTH];
} csiebox_account_info;

typedef struct {
  csiebox_account_info account;
  int conn_fd;
} csiebox_client_info;

typedef struct {
    pthread_t *threads;
    int *tid;
}threadpool_t;

typedef struct {
  struct {
    char path[PATH_MAX];
    char account_path[PATH_MAX];
    char run_path[PATH_MAX];
    int thread_num;
  } arg;
  int listen_fd;
  threadpool_t pool;
  csiebox_client_info** client;
} csiebox_server;

typedef struct {
  int conn_fd;
  csiebox_server *server;
  int ready_to_read;
}req;

void csiebox_server_init(
  csiebox_server** server, int argc, char** argv);
int csiebox_server_run(csiebox_server* server);
void csiebox_server_destroy(csiebox_server** server);
static int get_account_info( csiebox_server* server,  const char* user, csiebox_account_info* info);
static char* get_user_homedir( csiebox_server* server, csiebox_client_info* info);

req request;
pthread_mutex_t req_mutex;
pthread_mutex_t cond_mutex;
pthread_cond_t cond;
fd_set master;
fd_set ing;
int tst[100];
csiebox_server *SERVER;

#ifdef __cplusplus
}
#endif

#endif
