#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(2, "Usage: sleep num_of_ticks\n");
    exit(1);
  }

  int num_of_ticks = atoi(argv[1]);
  int result = sleep(num_of_ticks);
  if (result != 0) {
    fprintf(2, "sleep fails, result is %d\n", result);
  }
  exit(0);
}
