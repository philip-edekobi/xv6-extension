#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("Before sbrk: %p\n", sbrk(0));

  sbrk(1);

  printf("After sbrk(1): %p\n", sbrk(0));
  exit(0);
}
