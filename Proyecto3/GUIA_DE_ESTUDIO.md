# Guía de estudio integral — Proyecto 3: Tablas de páginas en acción (xv6)

> Material de estudio para dominar el proyecto y sustentarlo. Léelo con calma.
> Está escrito de lo más simple a lo más técnico.

---

# PARTE 0 — El problema desde cero (nivel básico)

## 0.1 ¿Qué es un sistema operativo y qué es xv6?

Un sistema operativo (SO) es el programa que administra el computador: reparte
la memoria, decide qué proceso corre, controla el disco, etc. **xv6** es un SO
pequeñito, hecho para enseñar, que imita a Unix. Corre sobre un procesador
**RISC-V** simulado con un programa llamado **QEMU** (como un "computador
virtual" dentro de tu computador).

## 0.2 El concepto central: memoria virtual

Imagina que cada programa (proceso) cree que tiene toda la memoria del mundo
para él solo, empezando en la dirección 0. Pero eso es mentira: la memoria RAM
real es compartida por todos. ¿Cómo se logra esa "mentira útil"?

Con una **tabla de páginas**: una especie de *diccionario traductor* que
convierte las **direcciones virtuales** (las que ve el programa) en
**direcciones físicas** (la RAM de verdad).

- **Dirección virtual (VA):** la que usa tu programa. Ej: "quiero el byte 0x1000".
- **Dirección física (PA):** dónde está de verdad ese byte en la RAM.
- **Página:** la memoria se maneja en bloques de 4096 bytes (4 KB) llamados
  *páginas*. No se traduce byte por byte, sino página por página.

Analogía: la VA es como el número de un apartamento en un edificio ("apto 302"),
y la PA es la dirección real en la ciudad. La tabla de páginas es el conserje que
sabe la equivalencia.

## 0.3 ¿Qué nos pide el Proyecto 3?

El proyecto nos hace **tocar directamente esa tabla de páginas** con tres
ejercicios independientes. En una frase cada uno:

- **Parte A — Página compartida + `ugetpid()`:** que un programa pueda saber su
  número de proceso (pid) *leyendo memoria*, sin molestar al kernel. (Es una
  optimización: evitar una "llamada al sistema", que es cara.)

- **Parte B — `vmprint()`:** una herramienta para *imprimir* la tabla de páginas
  y poder verla. Como abrir el diccionario traductor y leerlo entrada por entrada.

- **Parte C — `pgaccess()`:** detectar qué páginas usó un proceso recientemente,
  leyendo un "bit" que el hardware enciende automáticamente cada vez que se toca
  una página.

## 0.4 Dos conceptos que hay que tener clarísimos

**(a) Llamada al sistema (syscall):** cuando un programa necesita algo del kernel
(abrir un archivo, saber su pid, etc.), hace una *syscall*. Esto implica "entrar"
al kernel (un cambio de modo llamado *trap*) y "salir". Es correcto pero **cuesta
tiempo**. La Parte A busca evitar ese costo para `getpid()`.

**(b) Permisos de una página (los bits de la PTE):** cada entrada de la tabla
(PTE = Page Table Entry) no solo dice "a qué PA apunta", también tiene *banderas*
de permiso:
- `PTE_V` (Valid): la entrada es válida/existe.
- `PTE_R` (Read): se puede leer.
- `PTE_W` (Write): se puede escribir.
- `PTE_X` (eXecute): se puede ejecutar como código.
- `PTE_U` (User): el programa de usuario puede acceder (sin esto, solo el kernel).
- `PTE_A` (Accessed): el hardware lo enciende al leer/escribir la página. ← Parte C

---

# PARTE 1 — RISC-V Sv39: cómo funciona la traducción (nivel intermedio)

Necesitas entender esto para las Partes B y C. Es la "matemática" del proyecto.

## 1.1 Direcciones de 39 bits, 3 niveles

xv6 usa el esquema **Sv39**: las direcciones virtuales útiles tienen 39 bits.
Esos 39 bits se parten así:

```
 Dirección virtual (39 bits útiles):
 [ índice nivel 2 (9 bits) ][ índice nivel 1 (9 bits) ][ índice nivel 0 (9 bits) ][ offset (12 bits) ]
        bits 38..30               bits 29..21               bits 20..12            bits 11..0
```

- Los **12 bits de offset** = posición dentro de la página (2^12 = 4096 = 4 KB).
- Cada **índice de 9 bits** elige una de 512 entradas (2^9 = 512) en una tabla.
- Hay **3 niveles** de tablas encadenadas (de ahí "3 niveles").

## 1.2 El recorrido (walk)

Para traducir una VA:
1. Se parte de la **tabla raíz** (nivel 2). El índice de nivel 2 elige una entrada.
2. Esa entrada apunta a una **tabla de nivel 1**. El índice de nivel 1 elige otra.
3. Esa apunta a una **tabla de nivel 0**. El índice de nivel 0 elige la PTE final.
4. Esa PTE (si es "hoja") da la dirección física de la página. Se le suma el offset.

Macros clave de xv6 (archivo `riscv.h`):
- `PX(level, va)`: extrae el índice de 9 bits del nivel indicado.
- `PXSHIFT(level) = 12 + 9*level`: cuántos bits hay que desplazar para ese nivel.
  - nivel 0 → 12, nivel 1 → 21, nivel 2 → 30.
- `PTE2PA(pte)`: saca la dirección física que guarda una PTE.
- `PA2PTE(pa)`: lo contrario.
- `PTE_FLAGS(pte)`: saca solo los bits de permiso.

## 1.3 Hoja vs. entrada interna

- Una entrada **interna** (niveles 2 y 1) solo apunta a la siguiente tabla. **No
  tiene** permisos R/W/X encendidos.
- Una **hoja** (normalmente nivel 0) describe una página real y **sí tiene**
  algún permiso R/W/X. Regla práctica: si `(pte & (PTE_R|PTE_W|PTE_X)) == 0` y es
  válida, es una entrada interna; si no, es hoja.

## 1.4 El mapa de memoria de un proceso (de abajo hacia arriba)

```
 0x0           texto (código)          R-X-U
               datos / bss             RW-U
               pila (stack)            RW-U
               (página de guarda)      inválida para el usuario (sin PTE_U)
               heap (crece con sbrk)   RW-U
               ...
 USYSCALL      página compartida (pid) R--U   (sin W)   ← Parte A, la añadimos
 TRAPFRAME     datos para el trampolín RW     (sin U)
 TRAMPOLINE    código de entrada/salida R-X   (sin U)   la dirección más alta
```

Direcciones concretas en este xv6:
- `TRAMPOLINE = 0x3ffffff000`
- `TRAPFRAME  = 0x3fffffe000` (= TRAMPOLINE - 4096)
- `USYSCALL   = 0x3fffffd000` (= TRAPFRAME - 4096) ← la que definimos nosotros

---

# PARTE 2 — La lógica de desarrollo (cómo fuimos avanzando)

Este fue el orden lógico que seguimos. Entenderlo te sirve para explicar el
"cómo pensamos el problema".

1. **Preparar la base limpia.** Clonamos xv6 en el commit exacto `06aad25` y
   creamos la rama `proyecto3`. Regla de oro: trabajar sobre xv6 completo, pero
   entregar solo los archivos modificados.

2. **Entender el código base antes de tocarlo.** Leímos `proc.c`, `vm.c`,
   `memlayout.h`, `riscv.h`, `sysproc.c`, `trap.c`, etc. Descubrimos dos cosas
   importantes de ESTA versión del curso:
   - Ya trae *asignación perezosa* (lazy allocation): `sbrklazy`, `vmfault`,
     `ismapped`. Por eso la guía insiste en "páginas perezosas no tocadas".
   - El `copyout` tiene 5 argumentos: `copyout(pagetable, p->sz, dst, src, len)`.

3. **Parte A (la base de todo).** Definir `USYSCALL` y `struct usyscall`, reservar
   la página al crear el proceso, mapearla de solo lectura, liberarla sin fugas,
   ajustar los límites del heap y escribir `ugetpid()`.

4. **Registrar las syscalls nuevas.** `vmprint` y `pgaccess` necesitan número,
   entrada en la tabla, stub y declaración. (Igual que `trace`/`sysinfo` del
   Proyecto 2.)

5. **Parte B (`vmprint`).** Recorrido recursivo tipo `freewalk()`, con el formato
   exacto que valida el script del profesor.

6. **Parte C (`pgaccess`).** Leer y limpiar `PTE_A`, con todas las validaciones.

7. **Verificar.** Compilar sin warnings, correr `pgtbltest` (11/11) y
   `usertests -q` (ALL TESTS PASSED), y revisar el formato de `vmprint`.

---

# PARTE 3 — Análisis profundo del código (archivo por archivo)

Para cada archivo: qué se cambió, por qué, y a qué parte pertenece.

## 3.1 `kernel/memlayout.h` — (Parte A)

**Qué se añadió:**
```c
#define USYSCALL  (TRAPFRAME - PGSIZE)

#ifndef __ASSEMBLER__
struct usyscall {
  int pid;
};
#endif
```

**Por qué:**
- `USYSCALL` es la dirección virtual de la página compartida. Se coloca una
  página (4 KB) debajo del `TRAPFRAME`, siguiendo lo que pide la guía.
- `struct usyscall` define el contenido de esa página (por ahora solo el `pid`).
- `#ifndef __ASSEMBLER__ ... #endif`: este header también lo incluye código en
  ensamblador, que NO entiende `struct` de C. Ese guardián hace que la struct
  solo se "vea" cuando compila C, no ensamblador. Si no lo pusiéramos, el
  ensamblador daría error.

## 3.2 `kernel/proc.h` — (Parte A)

**Qué se añadió** (dentro de `struct proc`):
```c
struct usyscall *usyscall;   // puntero a la página compartida del proceso
```

**Por qué:** cada proceso necesita recordar *dónde* está su página física
USYSCALL, para poder llenarla, mapearla y liberarla. Es un puntero a memoria del
kernel.

## 3.3 `kernel/proc.c` — (Parte A) — 4 funciones tocadas

### (a) `allocproc()` — cuando nace un proceso
```c
if ((p->usyscall = (struct usyscall *)kalloc()) == 0) {
  freeproc(p);
  release(&p->lock);
  return 0;
}
p->usyscall->pid = p->pid;
```
- `kalloc()` reserva una página física de 4 KB. Devuelve 0 si no hay memoria
  (por eso el chequeo y el `freeproc` para no dejar basura).
- Guardamos el `pid` del proceso dentro de esa página. Esto es lo que después
  leerá `ugetpid()`.
- **Parte A.**

### (b) `proc_pagetable()` — arma la tabla de páginas del proceso
```c
if (mappages(pagetable, USYSCALL, PGSIZE, (uint64)(p->usyscall),
             PTE_R | PTE_U) < 0) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, 0);
  return 0;
}
```
- `mappages(tabla, VA, tamaño, PA, permisos)`: crea la traducción VA→PA.
  Mapeamos la dirección virtual `USYSCALL` hacia la página física `p->usyscall`.
- **Permisos `PTE_R | PTE_U` (SIN `PTE_W`):** el usuario puede LEER pero NO
  escribir. Esta es la decisión de diseño clave de la Parte A (seguridad).
- Si falla, deshacemos los mapeos previos (trampolín y trapframe) para no dejar
  la tabla a medias → sin fugas.
- **Parte A.**

### (c) `freeproc()` — cuando muere el proceso
```c
if (p->usyscall)
  kfree((void *)p->usyscall);
p->usyscall = 0;
```
- `kfree()` devuelve la página física al sistema. Ponemos el puntero en 0 para no
  usarlo por error después.
- **Parte A.**

### (d) `proc_freepagetable()` — libera la tabla
```c
uvmunmap(pagetable, USYSCALL, 1, 0);
```
- `uvmunmap(tabla, VA, nº_páginas, do_free)`: quita el mapeo.
- **`do_free = 0` (clave):** quitamos la traducción PERO no liberamos la memoria
  física aquí, porque de eso ya se encarga `freeproc()` con `kfree`. Si pusiéramos
  `do_free = 1`, liberaríamos la misma página dos veces → bug grave. Esta es una
  decisión sutil que el profe puede preguntar.
- **Parte A.**

### (e) `growproc()` — límite del heap
```c
if (sz + n > USYSCALL) {   // antes era TRAPFRAME
  return -1;
}
```
- El heap crece hacia arriba. Antes el tope era `TRAPFRAME`; ahora es `USYSCALL`,
  una página más abajo, para que el heap NO invada la página compartida.
- **Parte A (requisito 6).**

## 3.4 `kernel/sysproc.c` — (Partes A, B y C)

### (a) `sys_sbrk()` — mismo límite para el heap perezoso (Parte A)
```c
if (addr + n > USYSCALL)   // antes TRAPFRAME
  return -1;
```
Igual que `growproc`, pero para la rama de asignación perezosa (`sbrklazy`).

### (b) `sys_vmprint()` — handler de la syscall vmprint (Parte B)
```c
uint64 sys_vmprint(void) {
  struct proc *p = myproc();
  vmprint(p->pagetable);
  return 0;
}
```
- `myproc()` devuelve el proceso que está corriendo ahora.
- Llama a `vmprint` con la tabla de ESE proceso y devuelve 0 (como pide la guía).
- **Parte B.**

### (c) `sys_pgaccess()` — handler de pgaccess (Parte C)
```c
uint64 sys_pgaccess(void) {
  uint64 base; int npages; uint64 mask;
  argaddr(0, &base);
  argint(1, &npages);
  argaddr(2, &mask);

  struct proc *p = myproc();

  if ((base % PGSIZE) != 0) return -1;              // base alineada a página
  if (npages < 1 || npages > 64) return -1;         // npages en [1,64]
  if (base + (uint64)npages * PGSIZE > p->sz) return -1;  // rango dentro del proceso

  uint64 bitmask = 0;
  for (int i = 0; i < npages; i++) {
    uint64 va = base + (uint64)i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0);   // alloc=0: NO crear tablas nuevas
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;                               // página perezosa no tocada -> 0
    if (*pte & PTE_A) {
      bitmask |= (1UL << i);                  // marcar la página i como accedida
      *pte &= ~PTE_A;                         // limpiar el bit (consulta nueva)
    }
  }

  if (copyout(p->pagetable, p->sz, mask, (char *)&bitmask, sizeof(bitmask)) < 0)
    return -1;
  return 0;
}
```
Explicación de cada parte:
- `argaddr` / `argint`: leen los argumentos que el programa de usuario pasó
  (`base`, `npages`, `mask`). Son punteros/enteros que venían en registros.
- **Validaciones (los 3 `if`):** si algo está mal, devuelve -1 (lo exige la guía):
  base no alineada, npages fuera de 1..64, o el rango se sale del proceso.
- `walk(tabla, va, 0)`: baja por la tabla hasta la PTE de esa VA. El `0` final
  (alloc=0) significa "no crees tablas nuevas si no existen". Esto es CRÍTICO:
  así NO asignamos memoria a páginas perezosas (otra decisión que el profe
  pregunta).
- Si la PTE no existe o no es válida → esa página se reporta como 0 (bit apagado).
- `*pte & PTE_A`: ¿el hardware encendió el bit de acceso? Si sí:
  - `bitmask |= (1UL << i)`: encendemos el bit `i` de la máscara (operación de
    bits: desplazar un 1 a la posición i y hacer OR).
  - `*pte &= ~PTE_A`: apagamos PTE_A (AND con el complemento), para que la
    próxima consulta reporte solo accesos NUEVOS.
- `copyout(...)`: copia la máscara desde el kernel hacia la memoria del usuario
  (la dirección `mask`). Si la dirección del usuario es inválida, falla → -1.
- **Parte C.**

## 3.5 `kernel/vm.c` — (Parte B) — vmprint

```c
static void
vmprint_rec(pagetable_t pagetable, int level, uint64 va)
{
  for (int i = 0; i < 512; i++) {          // 512 entradas por tabla
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0)
      continue;                            // solo PTEs válidas

    uint64 child_va = va | ((uint64)i << PXSHIFT(level));  // reconstruir la VA
    uint64 pa = PTE2PA(pte);               // dirección física que apunta

    int depth = 2 - level;                 // 0 en raíz, 1 y 2 abajo
    for (int d = 0; d <= depth; d++)
      printk(" ..");                       // indentación: " .." por nivel
    printk("%p: pte %p pa %p\n", (void *)child_va, (void *)pte, (void *)pa);

    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0 && level > 0)
      vmprint_rec((pagetable_t)pa, level - 1, child_va);  // bajar un nivel
  }
}

void
vmprint(pagetable_t pagetable)
{
  printk("page table %p\n", (void *)pagetable);  // primera línea
  vmprint_rec(pagetable, 2, 0);                  // arranca en el nivel 2, VA 0
}
```
Explicación:
- Se recorre igual que `freewalk()` (la función de xv6 que libera tablas): las
  512 entradas de la tabla, de forma recursiva.
- `child_va = va | (i << PXSHIFT(level))`: ESTA es la "matemática" de la Parte B.
  Reconstruye la dirección virtual que cubre la entrada `i`, poniendo el índice
  en la posición de bits del nivel actual. Los niveles superiores ya venían
  acumulados en `va`. El offset (12 bits bajos) queda en 0 = inicio del rango.
- `depth = 2 - level`: en la raíz (level 2) imprime 1 grupo " .."; en level 1,
  dos; en level 0, tres. Coincide con el formato oficial `" .."`, `" .. .."`,
  `" .. .. .."`.
- `%p`: imprime con `0x` + 16 dígitos hex (lo hace `printptr` en `printk.c`).
- La recursión baja solo en entradas internas (sin R/W/X) y mientras `level > 0`.
- **Parte B.**

## 3.6 `kernel/riscv.h` — (Parte C)

```c
#define PTE_A (1L << 6) // bit de acceso (bit 6), lo enciende el hardware
```
- Define la bandera del bit de acceso, según la especificación de RISC-V (bit 6).
- **Parte C.**

## 3.7 `kernel/defs.h` — (Parte B)

```c
void vmprint(pagetable_t);
```
- Declara el prototipo de `vmprint` para que `sysproc.c` pueda llamarla (está
  definida en `vm.c`). `defs.h` es el "índice" de funciones del kernel.

## 3.8 Registro de syscalls — (común a B y C)

- **`syscall.h`:** números nuevos
  ```c
  #define SYS_vmprint  23
  #define SYS_pgaccess 24
  ```
- **`syscall.c`:** declarar `extern uint64 sys_vmprint(void);` y
  `extern uint64 sys_pgaccess(void);`, y añadirlas a la tabla `syscalls[]`.
  Esa tabla relaciona el número con la función que la atiende.
- **`user/usys.pl`:** genera el "stub" en ensamblador que el programa de usuario
  llama. El stub pone el número en el registro `a7` y ejecuta `ecall` (la
  instrucción que salta al kernel).
- **`user/user.h`:** declara las firmas para que los programas de usuario puedan
  llamarlas:
  ```c
  int ugetpid(void);                                   // Parte A
  int vmprint(void);                                   // Parte B
  int pgaccess(void *base, int npages, uint64 *mask);  // Parte C
  ```

## 3.9 `user/ulib.c` — (Parte A) — ugetpid

```c
#include "kernel/memlayout.h"   // para USYSCALL y struct usyscall

int
ugetpid(void)
{
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
}
```
- Interpreta la dirección `USYSCALL` como un `struct usyscall` y lee su `pid`.
- **No hace ninguna syscall:** solo lee memoria. Ese es todo el punto de la Parte
  A — evitar el trap al kernel.
- **Parte A.**

## 3.10 `user/pgtbltest.c` — programa de pruebas (oficial del docente)

No lo escribimos nosotros (lo entrega el profe). Corre 11 pruebas que verifican
las tres partes. Lo incluimos tal cual para que el proyecto quede listo para
compilar y probar.

## 3.11 Un detalle "gratis": matar al proceso que escribe en USYSCALL

El requisito A.7 (escribir en USYSCALL mata el proceso) **no necesitó código
extra**. ¿Por qué? Porque la página se mapeó SIN `PTE_W`. Si el proceso intenta
escribir, el hardware lanza un "store page fault" (scause 0xf). En `trap.c`, la
función `usertrap()` intenta resolverlo con `vmfault()`, pero como la página YA
está mapeada, `vmfault` devuelve 0, y el flujo cae en el caso por defecto que
llama a `setkilled(p)`. El proceso muere. Por eso en las pruebas ves:
```
usertrap(): unexpected scause 0xf ... stval=0x3fffffd000
```
y la prueba igual termina en OK (porque matar al proceso era lo correcto).

---

# PARTE 4 — Guion para la sustentación

## 4.1 Cómo presentar (orden sugerido, ~5-8 min)

1. **Contexto (30 s):** "El Proyecto 3 extiende xv6 con tres mecanismos que usan
   la tabla de páginas: una página compartida para acelerar `getpid`, una
   herramienta para imprimir la tabla, y detección de páginas accedidas."

2. **Demo en vivo (lo que el profe pedirá):**
   ```
   make qemu
   pgtbltest         -> TODAS LAS PRUEBAS PASARON (11 de 11)
   usertests -q      -> ALL TESTS PASSED
   ```
   Para salir de QEMU: `Ctrl-a` y luego `x`.

3. **Interpretar `vmprint` en vivo:** señala en la salida la línea de `USYSCALL`
   (`0x3fffffd000`, PTE termina en `13` = V+R+U sin W), TRAPFRAME (`...e000`) y
   TRAMPOLINE (`...f000`). Explica que la región perezosa solo muestra la página
   que el proceso tocó.

4. **Explicar una decisión de diseño por parte** (ver 4.3).

## 4.2 Lo más complejo de abordar (y cómo lo superamos)

- **Entender la versión del curso de xv6.** No era el xv6 estándar: traía lazy
  allocation y un `copyout` de 5 argumentos. Lo resolvimos LEYENDO el código base
  (`vm.c`, `trap.c`) antes de escribir nada, en vez de copiar soluciones de
  internet que no encajaban.

- **El formato exacto de `vmprint`.** El profe valida la salida con una expresión
  regular que exige un espacio inicial (`" .."`, no `".."`). Lo detectamos al
  comparar nuestra salida con el ejemplo oficial y con el script
  `ejecutar_pruebas.py`, y ajustamos la indentación.

- **Las pruebas de `usertests` "fallando" al principio.** `lazy_copy` y
  `lazy_sbrk` fallaban con el `usertests.c` genérico de MIT, porque ese archivo
  no sabía de la página USYSCALL. Entendimos (leyendo la guía) que el profe
  entrega una versión ajustada de `usertests.c`. Con la oficial, pasó todo. Esto
  nos enseñó a distinguir "mi código" de "los archivos de prueba".

- **La gestión de memoria sin fugas.** La parte más delicada en C: asegurar que
  la página USYSCALL se libera exactamente una vez. La clave fue separar
  responsabilidades: `freeproc` hace `kfree` (libera físico) y
  `proc_freepagetable` hace `uvmunmap` con `do_free=0` (solo desmapea). Lo
  verificamos con la prueba `sin_fugas_usyscall` y con
  `usertests` (que detecta fugas).

- **Metodología:** investigación de la especificación Sv39, lectura del libro de
  xv6, iteración con apoyo de IA para entender y revisar, y verificación empírica
  con QEMU en cada paso.

## 4.3 Puntos clave para resaltar (y posibles preguntas)

- **Parte A — ¿por qué solo lectura?** Si fuera escribible (`PTE_W`), el usuario
  podría falsear su pid y, peor, corromper datos del kernel. Rompería el
  aislamiento usuario/kernel.

- **Parte A — ¿por qué `do_free=0` al desmapear?** Para no liberar dos veces la
  misma página física (la libera `freeproc`). Evita corrupción de memoria.

- **Parte B — ¿cómo se reconstruye la VA?** `va | (indice << PXSHIFT(level))`.
  Cada nivel aporta 9 bits en su posición; el offset bajo queda en 0.

- **Parte B — ¿por qué solo PTEs válidas y páginas tocadas?** Porque recorremos
  con `PTE_V` y las páginas perezosas no tocadas no tienen PTE válida, así que no
  aparecen.

- **Parte C — ¿por qué `walk(..., 0)`?** Para NO asignar memoria a páginas
  perezosas: `pgaccess` observa el uso, no debe cambiarlo.

- **Parte C — ¿por qué limpiar PTE_A?** Para que la siguiente consulta reporte
  solo accesos nuevos. Es la base de los algoritmos de reemplazo de páginas.

- **Pregunta transversal — ¿qué pasa si un programa desborda la pila?** Cae en la
  página de guarda (sin `PTE_U`), el hardware lanza un fallo y el kernel mata al
  proceso, evitando que corrompa memoria contigua.

## 4.4 Checklist final antes de presentar

- [ ] `make clean && make qemu` compila sin warnings.
- [ ] `pgtbltest` -> 11/11.
- [ ] `usertests -q` -> ALL TESTS PASSED.
- [ ] Sé interpretar la salida de `vmprint` (USYSCALL, TRAPFRAME, TRAMPOLINE).
- [ ] Sé salir de QEMU (`Ctrl-a`, `x`).
- [ ] Puedo explicar una decisión de diseño por cada parte (sección 4.3).
- [ ] La entrega está en el repo del grupo (carpeta `Proyecto3/`).
