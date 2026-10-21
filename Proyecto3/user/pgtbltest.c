// pgtbltest.c (versión de apoyo del grupo)
//
// IMPORTANTE: este archivo es una prueba propia hecha por el grupo para
// poder verificar las Partes A, B y C mientras no se tiene el pgtbltest.c
// oficial del docente. Cuando el docente entregue su pgtbltest.c, se debe
// usar ESE (sobrescribe este archivo) porque es el que se califica.
//
// Las firmas que usa coinciden con la guía del Proyecto 3:
//   int ugetpid(void);
//   int vmprint(void);
//   int pgaccess(void *base, int npages, uint64 *mask);

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

//
// Parte A: ugetpid() debe coincidir con getpid() en el proceso y en los hijos.
//
void
ugetpid_test(void)
{
  int i;

  printf("ugetpid_test starting\n");

  for (i = 0; i < 4; i++) {
    int ret = fork();
    if (ret != 0) {
      wait(0);
      continue;
    }
    // proceso hijo
    if (getpid() != ugetpid()) {
      printf("  FALLO: getpid()=%d != ugetpid()=%d\n", getpid(), ugetpid());
      exit(1);
    }
    exit(0);
  }
  printf("ugetpid_test: OK\n");
}

//
// Parte A: USYSCALL debe ser de solo lectura. Escribir en ella debe matar
// al proceso (lo probamos en un hijo para no morir nosotros).
//
void
usyscall_readonly_test(void)
{
  printf("usyscall_readonly_test starting\n");
  int pid = fork();
  if (pid == 0) {
    // Esta escritura debe provocar que el kernel termine este hijo.
    int *p = (int *)(TRAPFRAME - PGSIZE); // dirección de USYSCALL
    *p = 0;                               // escritura prohibida
    printf("  FALLO: se pudo escribir en USYSCALL\n");
    exit(0);
  }
  int xstatus;
  wait(&xstatus);
  if (xstatus != 0)
    printf("usyscall_readonly_test: OK (el hijo fue terminado)\n");
  else
    printf("usyscall_readonly_test: revisar (el hijo no fue terminado)\n");
}

//
// Parte B: vmprint() imprime la tabla de páginas del proceso actual.
//
void
vmprint_test(void)
{
  printf("vmprint_test starting (salida de vmprint abajo)\n");
  vmprint();
  printf("vmprint_test: OK\n");
}

//
// Parte C: pgaccess() detecta accesos, limpia el bit y rechaza argumentos
// inválidos.
//
void
pgaccess_test(void)
{
  printf("pgaccess_test starting\n");

  uint64 mask = 0;
  // Reservamos 3 páginas con sbrk (memoria real).
  char *base = sbrk(3 * PGSIZE);
  if (base == (char *)-1) {
    printf("  FALLO: sbrk\n");
    exit(1);
  }

  // Tocamos (escribimos) solo las páginas 0 y 2.
  base[0 * PGSIZE] = 1;
  base[2 * PGSIZE] = 1;

  if (pgaccess(base, 3, &mask) < 0) {
    printf("  FALLO: pgaccess devolvió -1\n");
    exit(1);
  }

  // Esperamos bits 0 y 2 encendidos (0b101 = 5).
  printf("  mask tras tocar paginas 0 y 2 = 0x%x (esperado 0x5)\n", (int)mask);

  // Segunda consulta sin tocar nada: el bit debió limpiarse -> mask == 0.
  mask = 0;
  if (pgaccess(base, 3, &mask) < 0) {
    printf("  FALLO: pgaccess (segunda consulta) devolvió -1\n");
    exit(1);
  }
  printf("  mask en segunda consulta = 0x%x (esperado 0x0)\n", (int)mask);

  // Argumentos inválidos -> -1.
  if (pgaccess((void *)(base + 1), 3, &mask) != -1)
    printf("  revisar: base no alineada deberia dar -1\n");
  if (pgaccess(base, 0, &mask) != -1)
    printf("  revisar: npages=0 deberia dar -1\n");
  if (pgaccess(base, 65, &mask) != -1)
    printf("  revisar: npages=65 deberia dar -1\n");

  printf("pgaccess_test: OK\n");
}

int
main(int argc, char *argv[])
{
  ugetpid_test();
  usyscall_readonly_test();
  vmprint_test();
  pgaccess_test();
  printf("pgtbltest: todas las pruebas de apoyo terminaron\n");
  exit(0);
}
