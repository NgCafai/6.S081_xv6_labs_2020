#include "kernel/types.h"
#include "user/user.h"

void Process(int parent_to_cur[]) {
  close(parent_to_cur[1]);

  int cur_prime;
  if (read(parent_to_cur[0], &cur_prime, sizeof(cur_prime)) <= 0) {
    exit(0);
  }
  printf("prime %d\n", cur_prime);

  int cur_to_child[2];
  if (pipe(cur_to_child) != 0) {
    fprintf(2, "fail to open pipe");
    exit(1);
  }

  int pid = fork();
  if (pid == 0) {
    Process(cur_to_child);
  } else {
    int num;
    while(read(parent_to_cur[0], &num, sizeof(num)) > 0) {
      if (num % cur_prime != 0) {
        write(cur_to_child[1], &num, sizeof(num));
      }
    }
    close(parent_to_cur[0]);
    close(cur_to_child[1]);
    wait((int*)0);
  }
  exit(0);
}

int main(int argc, char* argv[]) {
  int first_prime = 2;
  int maximum = 35;

  int fds[2];
  if (pipe(fds) != 0) {
    fprintf(2, "fail to open pipe\n");
    exit(1);
  }

  int pid = fork();
  if (pid != 0) {
    // parent process
    close(fds[0]); // close the unused read end
    write(fds[1], &first_prime, sizeof(first_prime));
    for (int i = 2; i <= maximum; i++) {
      write(fds[1], &i, sizeof(i));
    }
    close(fds[1]);
    wait((int*)0);
  } else {
    Process(fds);
  }
  exit(0);
}