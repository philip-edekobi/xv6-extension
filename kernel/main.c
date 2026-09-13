#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "fdt.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main(uint64 hartid, uint64 dtb)
{
  if (cpuid() == 0) {
    // read dtb to setup memory
    struct fdt_result parse_res = parse_fdt(dtb);
    char *res_status =
      parse_res.status == FDT_OK ? "DTB parsed" : "DTB parse failed";
    struct fdt_node *root = fdt_get_root_node();
    struct fdt_node *mem = fdt_get_memory_node(root);
    uint64 base = 0, size = 0;
    fdt_populate_memory_vals(mem, &base, &size);

    consoleinit();
    printkinit();
    printk("\n");
    printk("xv6 kernel is booting\n");
    printk("%s\n", res_status);

    printk("\n");
    kinit(&base, &size); // physical page allocator

    if (mem == 0) {
      base = 0x80000000L;
      size = base + (128 * 1024 * 1024);
    }

    kvminit(&base, &size); // create kernel page table
    kvminithart();         // turn on paging
    procinit();            // process table
    trapinit();            // trap vectors
    trapinithart();        // install kernel trap vector
    plicinit();            // set up interrupt controller
    plicinithart();        // ask PLIC for device interrupts
    binit();               // buffer cache
    iinit();               // inode table
    fileinit();            // file table
    virtio_disk_init();    // emulated hard disk
    userinit();            // first user process

    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging
    trapinithart(); // install kernel trap vector
    plicinithart(); // ask PLIC for device interrupts
  }

  scheduler();
}
