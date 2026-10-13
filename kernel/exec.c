#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

#define BUFSIZ    192
#define REDIR_LIM 4

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);
static int eatwhitespace(char buf[], int n);

// map ELF permissions to PTE permission bits.
int
flags2perm(int flags)
{
  int perm = 0;
  if (flags & 0x1)
    perm = PTE_X;
  if (flags & 0x2)
    perm |= PTE_W;
  return perm;
}

//
// the implementation of the exec() system call
//
int
kexec(char *path, char **argv)
{
  char *s, *last;
  int i, off, depth = 0;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  pagetable_t kpagetable = 0, oldkpagetable;
  struct proc *p = myproc();

  begin_op();

  char paths[REDIR_LIM][MAXPATH];
  char *argvs[REDIR_LIM][MAXARG];

  while (depth < REDIR_LIM) {
    // Open the executable file.
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }

    char buf[BUFSIZ];

    ilock(ip);

    int n = readi(ip, 0, (uint64)buf, 0, BUFSIZ - 1);
    if (n > 0) {
      buf[n] = '\0';
      if (n >= 2 && buf[0] == '#' && buf[1] == '!') {
        i = 0;
        char *pathstr = buf + eatwhitespace(buf, 2);
        while (*pathstr && *pathstr != ' ' && *pathstr != '\n' &&
               *pathstr != '\r' && *pathstr != '\t' && i < MAXPATH - 1)
          paths[depth][i++] = *pathstr++;

        paths[depth][i] = '\0';

        iunlockput(ip);

        if (i == 0) {
          end_op();
          return -1;
        }
        if (*pathstr && *pathstr != ' ' && *pathstr != '\n' &&
            *pathstr != '\r' && *pathstr != '\t') {
          end_op();
          return -1;
        }

        // build new argv
        int newargc = 0;
        argvs[depth][newargc++] = paths[depth];
        argvs[depth][newargc++] = path;

        for (i = 1; argv[i]; i++) {
          if (newargc >= MAXARG - 1) {
            end_op();
            return -1;
          }
          argvs[depth][newargc++] = argv[i];
        }
        argvs[depth][newargc] = 0;

        path = paths[depth];
        argv = argvs[depth];
        depth++;
      } else {
        break;
      }
    } else {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (depth == REDIR_LIM) {
    end_op();
    return -1;
  }

  // Read the ELF header.
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // Is this really an ELF file?
  if (elf.magic != ELF_MAGIC)
    goto bad;

  if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  if ((kpagetable = proc_kpagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz,
                        flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) ==
      0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK * PGSIZE;

  // Copy argument strings into new stack, remember their
  // addresses in ustack[].
  for (argc = 0; argv[argc]; argc++) {
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sz, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push a copy of ustack[], the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sz, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) <
      0)
    goto bad;

  if (KUSERBASE + sz > KUSERLIMIT)
    goto bad;

  if (uvmkclone(pagetable, kpagetable, 0, sz) < 0)
    goto bad;

  // a0 and a1 contain arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  oldkpagetable = p->kpagetable;
  p->kpagetable = kpagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = ulib.c:start()
  p->trapframe->sp = sp;         // initial stack pointer

  w_satp(MAKE_SATP(p->kpagetable));
  sfence_vma();

  proc_freepagetable(oldpagetable, oldsz);
  proc_kfreepagetable(oldkpagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (kpagetable)
    proc_kfreepagetable(kpagetable);
  if (ip) {
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset,
        uint sz)
{
  uint i, n;
  uint64 pa;

  for (i = 0; i < sz; i += PGSIZE) {
    pa = walkaddr(pagetable, va + i);
    if (pa == 0)
      panic("loadseg: address should exist");
    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
      return -1;
  }

  return 0;
}

static int
eatwhitespace(char buf[], int n)
{
  int i = n;
  while (buf[i] && (buf[i] == ' ' || buf[i] == '\t'))
    i++;

  return i;
}
