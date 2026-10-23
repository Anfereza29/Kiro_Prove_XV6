#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    // Parte A: el heap perezoso tampoco puede invadir la página USYSCALL.
    if (addr + n > USYSCALL)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Parte B: imprime la tabla de páginas del proceso que la invoca.
uint64
sys_vmprint(void)
{
  struct proc *p = myproc();
  vmprint(p->pagetable);
  return 0;
}

// Parte C: para cada una de las npages páginas a partir de base, marca en
// *mask el bit i si la página fue accedida (PTE_A) desde la última consulta,
// luego limpia PTE_A. Devuelve 0 en éxito, -1 si los argumentos son inválidos.
uint64
sys_pgaccess(void)
{
  uint64 base;  // dirección virtual de la primera página (void *base)
  int npages;   // número de páginas a consultar
  uint64 mask;  // dirección de usuario donde escribir el resultado (uint64 *)

  argaddr(0, &base);
  argint(1, &npages);
  argaddr(2, &mask);

  struct proc *p = myproc();

  // Validaciones: base alineada a página, npages en [1,64],
  // y el rango completo debe caber dentro del tamaño del proceso.
  if ((base % PGSIZE) != 0)
    return -1;
  if (npages < 1 || npages > 64)
    return -1;
  if (base + (uint64)npages * PGSIZE > p->sz)
    return -1;

  uint64 bitmask = 0; // se arma en el kernel y luego se copia al usuario
  for (int i = 0; i < npages; i++) {
    uint64 va = base + (uint64)i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0); // alloc=0: no crear tablas nuevas
    // Páginas sin PTE o sin memoria física (p. ej. sbrklazy no tocada):
    // se reportan como 0 y NO se les asigna memoria.
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if (*pte & PTE_A) {
      bitmask |= (1UL << i); // la página fue leída o escrita
      *pte &= ~PTE_A;        // limpiar para que la próxima consulta sea nueva
    }
  }

  // Copiar el resultado al espacio del usuario. copyout de esta versión del
  // curso recibe (pagetable, p->sz, dstva, src, len).
  if (copyout(p->pagetable, p->sz, mask, (char *)&bitmask, sizeof(bitmask)) < 0)
    return -1;

  return 0;
}
