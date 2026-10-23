# Proyecto de Sistemas Operativos
## Proyecto 3: Tablas de páginas en acción (xv6)

### Integrantes

- Juan Felipe Gallón Maldonado
- Laura Santamaría Espinosa
- Andrés Felipe Rengifo

### Curso
Sistemas Operativos

### Semestre
2026-2

---

# Descripción

Este proyecto extiende el kernel de xv6-riscv (commit `06aad25`) con tres
mecanismos que trabajan directamente sobre la tabla de páginas Sv39 de cada
proceso:

- **Parte A — Página compartida y `ugetpid()`:** el kernel reserva para cada
  proceso una página física de solo lectura (`USYSCALL`) que contiene su `pid`.
  Esa página se mapea en el espacio de usuario justo debajo del `TRAPFRAME`.
  La función de usuario `ugetpid()` obtiene el `pid` leyendo esa memoria, sin
  hacer ninguna llamada al sistema (evita el costo del trap al kernel).

- **Parte B — `vmprint()`:** una llamada al sistema que imprime en consola la
  tabla de páginas del proceso que la invoca, recorriéndola de forma recursiva
  por sus tres niveles Sv39. Para cada PTE válida imprime la dirección virtual
  que cubre, el valor de la PTE y la dirección física a la que apunta.

- **Parte C — `pgaccess()`:** una llamada al sistema que consulta y limpia el
  bit de acceso `PTE_A` que el hardware RISC-V enciende cuando una página se lee
  o escribe. Permite saber qué páginas usó un proceso, que es la base de los
  algoritmos de reemplazo de páginas.

---

# Archivos modificados

## kernel/

- **`memlayout.h`**: se define `USYSCALL` como la página justo debajo de
  `TRAPFRAME` (`TRAPFRAME - PGSIZE`) y la estructura `struct usyscall { int pid; }`.
  La estructura va dentro de `#ifndef __ASSEMBLER__` porque este header también
  lo incluye código en ensamblador, que no entiende `struct` de C.

- **`proc.h`**: se agrega a `struct proc` el campo `struct usyscall *usyscall`,
  un puntero a la página física compartida del proceso.

- **`proc.c`**:
  - `allocproc()`: reserva la página con `kalloc()` y guarda el `pid` en ella.
  - `proc_pagetable()`: mapea esa página en la dirección virtual `USYSCALL` con
    permisos `PTE_R | PTE_U` (el usuario puede leerla pero no escribirla). Si el
    mapeo falla, deshace los mapeos previos para no dejar la tabla a medias.
  - `freeproc()`: libera la página física con `kfree()` y pone el puntero en 0.
  - `proc_freepagetable()`: quita el mapeo de `USYSCALL` con `do_free=0` (la
    memoria física la libera `freeproc`, así no se libera dos veces).
  - `growproc()`: el tope del heap pasa de `TRAPFRAME` a `USYSCALL`, para que el
    heap no pueda crecer sobre la página compartida.

- **`riscv.h`**: se define `PTE_A (1L << 6)`, el bit de acceso de la PTE según la
  especificación de RISC-V.

- **`vm.c`**: se implementan `vmprint()` (imprime la línea `page table <pa>` y
  arranca el recorrido) y la función auxiliar recursiva `vmprint_rec()`, que
  sigue el modelo de `freewalk()`: visita las 512 entradas de cada tabla, imprime
  solo las PTEs válidas y baja de nivel cuando la entrada apunta a otra tabla.

- **`sysproc.c`**:
  - `sys_vmprint()`: handler de la syscall; imprime la tabla del proceso actual
    y devuelve 0.
  - `sys_pgaccess()`: handler de la syscall; valida argumentos, recorre las
    páginas con `walk()`, arma la máscara de bits según `PTE_A`, limpia el bit,
    copia el resultado al usuario con `copyout()` y devuelve 0 (o -1 si hay error).
  - `sys_sbrk()`: el tope del heap perezoso pasa de `TRAPFRAME` a `USYSCALL`.

- **`syscall.h`**: números nuevos `SYS_vmprint` (23) y `SYS_pgaccess` (24).

- **`syscall.c`**: se declaran `sys_vmprint` y `sys_pgaccess` y se registran en
  la tabla `syscalls[]`.

- **`defs.h`**: se declara el prototipo `void vmprint(pagetable_t)`.

## user/

- **`user.h`**: se declaran `int ugetpid(void)`, `int vmprint(void)` y
  `int pgaccess(void *base, int npages, uint64 *mask)`.

- **`usys.pl`**: se agregan las entradas que generan los stubs de `vmprint` y
  `pgaccess`.

- **`ulib.c`**: se incluye `kernel/memlayout.h` y se implementa `ugetpid()`, que
  devuelve el `pid` leyendo `((struct usyscall *)USYSCALL)->pid`.

- **`pgtbltest.c`**: programa de pruebas oficial entregado por el docente
  (Partes A, B y C). Se incluye tal cual para dejar el proyecto listo para
  compilar y probar.

> También se agrega `$U/_pgtbltest` a `UPROGS` en el `Makefile` (fuera de esta
> carpeta, porque el `Makefile` vive en la raíz de xv6). Para `usertests` se usa
> la versión del curso entregada por el docente (con los dos ajustes por la
> página USYSCALL).

---

# Integración con xv6

1. Clonar xv6-riscv y hacer `git switch -c proyecto3 06aad25`.
2. Copiar los archivos entregados en sus rutas (`kernel/...` y `user/...`).
3. Agregar `$U/_pgtbltest` a `UPROGS` en el `Makefile`.
4. Compilar con `make qemu`.
5. Ejecutar `pgtbltest` y `usertests -q`.

---

# Respuestas a las preguntas

## 1. Interpretación de vmprint()

La salida de `vmprint()` para un proceso recién creado muestra la jerarquía
Sv39 de tres niveles. Las PTEs internas (niveles 2 y 1) no tienen permisos
R/W/X: solo apuntan a la siguiente tabla. Las **hojas** (nivel 0) son las que
describen páginas reales. En un proceso de usuario encontramos:

- **En direcciones virtuales bajas (rango que empieza en `0x0`):**
  - **texto (código):** primera(s) página(s), permisos de lectura y ejecución
    para el usuario (`PTE_R | PTE_X | PTE_U`), sin escritura.
  - **datos/bss:** páginas con lectura y escritura para el usuario
    (`PTE_R | PTE_W | PTE_U`).
  - **página de guarda:** una página inválida para el usuario (sin `PTE_U`) que
    separa la pila del resto; cualquier acceso cae en fallo.
  - **pila (stack):** página con `PTE_R | PTE_W | PTE_U`.

- **En direcciones virtuales altas:**
  - **USYSCALL** (`TRAPFRAME - PGSIZE`): nuestra página compartida, con
    `PTE_R | PTE_U` (solo lectura para el usuario).
  - **TRAPFRAME** (`TRAMPOLINE - PGSIZE`): `PTE_R | PTE_W` **sin** `PTE_U`; la usa
    el código de trampolín en modo supervisor, no el usuario.
  - **TRAMPOLINE** (la dirección virtual más alta): `PTE_R | PTE_X` **sin** `PTE_U`;
    código compartido para entrar y salir del kernel.

## 2. Página de guarda

La página de guarda **no** tiene el bit `PTE_U` para que el usuario no pueda
acceder a ella: cualquier lectura o escritura sobre esa página genera un fallo
de página que el kernel trata como error. Se coloca justo debajo de la pila
precisamente para **detectar desbordamientos de pila**: si un programa empuja
más datos de los que caben en su pila, termina escribiendo en la página de
guarda, el hardware lanza una excepción y el kernel mata el proceso, en lugar de
dejar que la pila corrompa silenciosamente la memoria contigua (datos o heap).

## 3. Otras llamadas que se pueden acelerar

Con la página compartida se pueden acelerar llamadas que **solo leen** un dato
del kernel que es propio del proceso y cambia poco:

- **Se podrían acelerar:** `getpid()` (ya lo hacemos; el pid no cambia nunca),
  y datos de solo lectura como el identificador del proceso padre, si se
  publicaran en la página. En general, cualquier información que el kernel pueda
  escribir una vez y el usuario solo leer.
- **No conviene / no se puede acelerar:**
  - Llamadas que **modifican** estado del kernel (`fork`, `exec`, `kill`,
    `open`, `write`, `sbrk`): requieren ejecutar código del kernel con sus
    permisos; no es seguro dejar que el usuario las haga leyendo memoria.
  - Llamadas que leen estado **compartido o cambiante** de otros procesos o del
    sistema (p. ej. `uptime`), porque mantener ese dato siempre actualizado en
    cada página de usuario costaría más que el propio trap, y exponerlo abre
    riesgos de seguridad.

## 4. Riesgo de USYSCALL con PTE_W

Si `USYSCALL` se mapeara con `PTE_W`, el proceso de usuario podría **escribir**
en esa página. El riesgo principal es que un proceso podría **falsear su propio
`pid`** (y cualquier otro dato que publicáramos ahí), de modo que `ugetpid()`
devolvería un valor mentiroso. Más grave aún: si en el futuro se compartiera más
información del kernel por esa vía, permitir escritura dejaría que el usuario
corrompa datos de los que el kernel depende, rompiendo el aislamiento entre
usuario y kernel. Por eso la página es de **solo lectura** (`PTE_R | PTE_U`): el
kernel la escribe, el usuario solo la lee.

## 5. pgaccess() y páginas perezosas

`pgaccess()` reporta `0` para una página reservada con `sbrklazy()` que todavía
no se ha tocado porque esa página **aún no tiene memoria física asignada**: su
PTE no existe o no es válida (`PTE_V == 0`), así que no hay bit `PTE_A` que
consultar; nunca fue accedida. Además, `pgaccess()` **no debe asignarle memoria**
(llamamos a `walk()` con `alloc = 0`) porque su trabajo es *observar* el uso de la
memoria, no modificarlo: si asignara la página, cambiaría el comportamiento del
programa, desperdiciaría memoria física para páginas que quizá nunca se usen y
rompería justamente la semántica de la asignación perezosa que se quiere medir.

---

# Resultados de las pruebas

Las pruebas se ejecutaron en QEMU con el script oficial `ejecutar_pruebas.py`.
La evidencia completa (fecha, commit, duración y resultado) está en
`pruebas/pgtbltest.txt` y `pruebas/usertests.txt`.

- **`pgtbltest`: PASÓ — TODAS LAS PRUEBAS PASARON (11 de 11)**, y la revisión
  automática del formato de `vmprint()` resultó **OK**. Verifica que `ugetpid()`
  coincide con `getpid()` en procesos nuevos, hijos y tras `exec()`; que
  `USYSCALL` es de solo lectura (escribir en ella mata al proceso); que el heap
  no invade `USYSCALL`; que no hay fugas al crear y destruir procesos; que
  `vmprint()` imprime las páginas esperadas con el formato correcto; y que
  `pgaccess()` detecta accesos, limpia `PTE_A`, funciona con 64 páginas, respeta
  las páginas perezosas y rechaza argumentos inválidos.
- **`usertests -q`: PASÓ — ALL TESTS PASSED**, sin fugas de memoria, incluidas
  las pruebas de asignación perezosa (`lazy_alloc`, `lazy_unmap`, `lazy_copy`,
  `lazy_sbrk`).

> Los mensajes `usertrap(): unexpected scause ...` que aparecen en la salida son
> esperados: varias pruebas provocan accesos inválidos a propósito para
> comprobar que el kernel los rechaza. Cada bloque termina en `OK`.

---

# Decisiones de diseño

- **La página USYSCALL se libera en `freeproc()` con `kfree()`**, y en
  `proc_freepagetable()` solo se *desmapea* (`do_free = 0`). Así la memoria física
  se libera exactamente una vez y no hay fugas ni doble liberación.
- **El requisito "escribir en USYSCALL termina el proceso" se cumple sin código
  extra:** como la página se mapea sin `PTE_W`, una escritura provoca un *store
  page fault*; `vmfault()` ve que la página ya está mapeada y devuelve 0, así que
  `usertrap()` cae en el caso por defecto y llama a `setkilled()`.
- **`vmprint()` reconstruye la dirección virtual** acumulando el índice de cada
  nivel desplazado por `PXSHIFT(level)`, de modo que la VA impresa es el inicio
  del rango que cubre cada PTE, tal como pide el enunciado.
- **`pgaccess()` arma la máscara en una variable del kernel** y la copia de una
  sola vez con `copyout()`, en lugar de escribir bit por bit en el usuario.
- **Las syscalls nuevas reutilizan la infraestructura del Proyecto 2** (registro
  en `syscall.h`, `syscall.c`, `usys.pl`, `user.h`).

---

# Limitaciones conocidas

- La página compartida solo publica el `pid`; no se aceleran otras llamadas al
  sistema (está fuera del alcance del proyecto).
- No se implementa Copy-on-Write, superpáginas ni el uso del bit `PTE_D`.
- `pgaccess()` admite como máximo 64 páginas por llamada, porque la máscara de
  resultado es un `uint64` (64 bits).

---

# Declaración de uso de IA

Durante el desarrollo de este proyecto se utilizó **Kiro** (asistente de IA
generativa) como apoyo para comprender los conceptos de paginación Sv39, revisar
el código base de xv6, proponer la implementación de las tres partes y redactar
este README. El equipo revisó, comprende y es capaz de explicar y justificar cada
fragmento de código entregado. Los integrantes asumen plena responsabilidad
académica sobre el contenido entregado.
