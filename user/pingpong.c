#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  int p2c[2], c2p[2];
  pipe(p2c);
  pipe(c2p);

  // int n;
  char buf[64];
  int pid = fork();
  if (pid == 0) {
    // child process
    close(p2c[1]); // close the unused write end
    read(p2c[0], buf, sizeof(buf));
    close(p2c[0]);
    printf("%d: received %s\n", getpid(), buf);

    char* send_msg = "pong";
    close(c2p[0]); // close the unused read end
    write(c2p[1], send_msg, strlen(send_msg));
    close(c2p[1]);
  } else {
    // parent process
    char* send_msg = "ping";
    close(p2c[0]); // close the unused read end
    write(p2c[1], send_msg, strlen(send_msg));
    close(p2c[1]);

    close(c2p[1]); // close the unused write end
    read(c2p[0], buf, sizeof(buf));
    close(c2p[0]);
    printf("%d: received %s\n", getpid(), buf);
  }
  exit(0);
}