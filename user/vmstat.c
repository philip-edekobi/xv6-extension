#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc > 2) {
    printf("Pass no args");
    exit(0);
  }

  uint64 bytes = vmstat();
  printf("Free mem in bytes: %ld\n", bytes);

  return 0;
}
