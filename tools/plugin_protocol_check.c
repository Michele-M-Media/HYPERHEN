/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise production remote helpers through a local mock loader.
 */
#include <elfldr_remote.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"protocol check line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static const char *expected,*response;static int socket_fd,ready[2];
static void *server(void *unused) {
  (void)unused;
  socket_fd=socket(AF_INET,SOCK_STREAM,0);CHECK(socket_fd>=0);
  CHECK(!setsockopt(socket_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int)));
  struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons(ONION_ELFLDR_PORT);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  CHECK(!bind(socket_fd,(struct sockaddr *)&addr,sizeof(addr)));CHECK(!listen(socket_fd,1));
  CHECK(write(ready[1],"1",1)==1);
  int client=accept(socket_fd,NULL,NULL);CHECK(client>=0);char line[2048];size_t used=0;
  while(used<sizeof(line)-1){ssize_t n=read(client,line+used,1);CHECK(n==1);if(line[used++]=='\n')break;}
  line[used]=0;CHECK(!strcmp(line,expected));
  for(size_t i=0;response[i];++i)CHECK(write(client,response+i,1)==1);
  close(client);close(socket_fd);return NULL;
}
static void run(const char *path,const char *want,const char *reply,pid_t result,int plugin) {
  expected=want;response=reply;CHECK(!pipe(ready));pthread_t thread;CHECK(!pthread_create(&thread,NULL,server,NULL));
  char token;CHECK(read(ready[0],&token,1)==1);close(ready[0]);close(ready[1]);
  pid_t pid=plugin?elfldr_remote_onion_launch_plugin_file_get_pid(path):elfldr_remote_onion_launch_file_get_pid(path,NULL);
  CHECK(pid==result);CHECK(!pthread_join(thread,NULL));
}
int main(void) {
  CHECK(elfldr_remote_onion_launch_plugin_file_get_pid(NULL)==-1);
  CHECK(elfldr_remote_onion_launch_plugin_file_get_pid("relative")==-1);
  CHECK(elfldr_remote_onion_launch_plugin_file_get_pid("/bad\npath")==-1);
  CHECK(elfldr_remote_onion_launch_plugin_file_get_pid("/bad?args=x")==-1);
  CHECK(elfldr_remote_onion_launch_plugin_file_get_pid("/bad%00path")==-1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","OK 321\n",321,1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","ERR unsupported payload format\n",-1,1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","OK 1\n",-1,1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","OK 321junk\n",-1,1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","OK 321",-1,1);
  run("/test/PLUG00001","hplg:/test/PLUG00001\n","OK 99999999999999999\n",-1,1);
  run("/test/util.elf","file:/test/util.elf\n","OK 456\n",456,0);
  puts("PASS: dedicated plugin protocol, legacy-route isolation, split/rejected/malformed PID replies");return 0;
}
