# qemu -kernel loads the kernel at 0x80000000
# and causes each hart (i.e. CPU) to jump there.
# kernel.ld causes the following code to
# be placed at 0x80000000.
.section .text
.global  _entry

_entry:
# set up a stack for C.
# stack0 is declared in start.c
# with a 4096-byte stack per CPU.
# sp = stack0 + ((hartid + 1) * 4096)

# the hart id of the cpu is provided in $a0 by the bootloader
# $a1 contains the address of the device tree in memory
# save $a0 and $a1 in order to pass them to start at the end of this routine
mv t3, a0
mv t4, a1

la   sp, stack0
li   s0, 1024*4
addi s1, a0, 1
mul  s0, s0, s1
add  sp, sp, s0

# bring back a0 and a1
mv a0, t3
mv a1, t4

# jump to start() in start.c
call start

spin:
	j spin
