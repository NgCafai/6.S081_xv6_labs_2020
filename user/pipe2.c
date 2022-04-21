#include "kernel/types.h"
#include "user/user.h"

// pipe2.c: communication between two processes

int
main()
{
  int pid;
  int fds[2];
  char buf[100];
  printf("%l", sizeof(buf));
  
  // create a pipe, with two FDs in fds[0], fds[1].
  pipe(fds);
  close(fds[0]);

  pid = fork();
  if (pid == 0) {
    int input = 35;
    write(fds[1], &input, sizeof(input));
    input = 30;
    write(fds[1], &input, sizeof(input));
  } else {
    int output;
    int read_output;
    read_output = read(fds[0], &output, sizeof(output));
    printf("read_output: %d, receive: %d\n", read_output, output);
    read_output = read(fds[0], &output, sizeof(output));
    printf("read_output: %d, receive: %d\n", read_output, output);
  }

  exit(0);
}