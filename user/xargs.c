#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  char buf[512], ch;
  char* p = buf;
  char* arguments[MAXARG];
  int count = 0;
  int blanks = 0;
  int offset = 0;

  if (argc < 2) {
    fprintf(2, "Usage: xargs <command> [argv...]\n");
    exit(1);
  }

  for (count = 1; count < argc; count++) {
    arguments[count - 1] = argv[count];
  }
  count--;

  while(read(0, &ch, 1) > 0) {
    if (ch == ' ' || ch == '\t') {
      blanks++;
      continue;
    }

    if (blanks > 0) {
      buf[offset++] = 0;
      arguments[count++] = p;
      p = buf + offset;
      blanks = 0;
    }

    if (ch != '\n') {
      buf[offset++] = ch;
    } else {
      buf[offset++] = 0;
      arguments[count++] = p;
      if (fork() != 0) {
        exec(arguments[0], arguments);
        fprintf(2, "exec failed\n");
        exit(1);
      }
      wait(0);

      count = argc - 1;
      blanks = 0;
      offset = 0;
      p = buf;
    }
  }

  exit(0);
}