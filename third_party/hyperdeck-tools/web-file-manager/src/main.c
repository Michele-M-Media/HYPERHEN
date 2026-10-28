#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __SCE__
#include <sys/syscall.h>
#include <sys/sysctl.h>
#endif

#include "app_installer.h"
#include "archive_helper.h"
#include "filemgr.h"
#include "notify.h"
#include "websrv.h"

#define PROCESS_NAME "web-file-mgr.elf"
#define DEFAULT_PORT 8888

static int
port_available(unsigned short port) {
  struct sockaddr_in addr;
  int fd;
  int ret;

  if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }
  if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    perror("setsockopt");
    close(fd);
    return -1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  ret = !bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  close(fd);
  return ret;
}

static unsigned short
find_available_port(unsigned short start) {
  unsigned int port;

  for(port = start; port <= 65535; port++) {
    int available = port_available((unsigned short)port);
    if(available < 0) {
      return 0;
    }
    if(available) {
      return (unsigned short)port;
    }
  }
  return 0;
}

#ifdef __SCE__
static pid_t
find_pid(const char *name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }
  if(!(buf = malloc(buf_size))) {
    perror("malloc");
    return -1;
  }
  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    perror("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr = buf; ptr < buf + buf_size;) {
    int ki_structsize = *(int *)ptr;
    pid_t ki_pid = *(pid_t *)&ptr[72];
    char *ki_tdname = (char *)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && ki_pid != mypid) {
      pid = ki_pid;
    }
  }

  free(buf);
  return pid;
}
#endif

int
main(int argc, char **argv) {
  unsigned short port;
#ifdef __SCE__
  unsigned short notified_port = 0;
  pid_t pid;
#endif

  (void)argc;
  (void)argv;

#ifdef __SCE__
  syscall(SYS_thr_set_name, -1, PROCESS_NAME);
  while((pid = find_pid(PROCESS_NAME)) > 0) {
    if(kill(pid, SIGKILL)) {
      perror("kill");
      return 1;
    }
    sleep(1);
  }
#else
#endif

  puts(PROCESS_NAME);
  printf("version: %s\n", VERSION_TAG);

#ifdef __SCE__
  if(archive_helper_autostart()) {
    perror("start wfm-7zip-helper");
  }
#endif
  filemgr_recover_extract_tasks();

  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  while(1) {
    port = find_available_port(DEFAULT_PORT);
    if(!port) {
      fprintf(stderr, "no available port from %u\n", DEFAULT_PORT);
      sleep(3);
      continue;
    }

    printf("listening on port %u\n", port);
#ifdef __SCE__
    if(notified_port != port) {
      notify_user("Web File Manager\nVersion: %s\nPort: %u", VERSION_TAG, port);
      notified_port = port;
    }
#endif

    websrv_listen(port);
    if(websrv_stop_requested()) {
      break;
    }
    sleep(3);
  }

  return 0;
}
