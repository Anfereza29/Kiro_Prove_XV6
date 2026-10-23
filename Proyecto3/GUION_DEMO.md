# Guion rapido para la demostracion - Proyecto 3 (xv6)

> Leelo mientras muestras la pantalla. Esta pensado como teleprompter.

---

## APERTURA (antes de ejecutar)

"Profe, nuestro Proyecto 3 extiende el kernel de xv6 con TRES mecanismos que
trabajan sobre la tabla de paginas: una pagina compartida para acelerar getpid
(Parte A), una herramienta para imprimir la tabla de paginas (Parte B), y la
deteccion de paginas accedidas con el bit PTE_A (Parte C). Voy a compilar desde
cero y correr las pruebas oficiales."

---

## MIENTRAS CORRE  make clean && make qemu

"Estoy compilando el kernel desde cero con nuestras modificaciones. Compila sin
advertencias, que era un requisito. Cuando arranque, aparece el prompt de xv6."

Comandos:
    cd ~/xv6-riscv
    make clean
    make qemu

---

## AL CORRER  pgtbltest

### Parte A (primeras pruebas)
"Las tres primeras pruebas (ugetpid_basico, ugetpid_hijos, ugetpid_exec)
verifican que nuestro ugetpid() devuelve el mismo pid que getpid(), pero
leyendolo de memoria, SIN hacer una llamada al sistema. Funciona en el proceso,
en los hijos y tras un exec."

### Cuando aparezca el usertrap (scause 0xf stval=0x3fffffd000)
"Este mensaje NO es un error, es lo que queremos. La prueba usyscall_solo_lectura
intenta ESCRIBIR en la pagina compartida, que mapeamos de SOLO LECTURA. El
hardware lanza un fallo y el kernel mata al proceso. Por eso la prueba termina en
OK: la proteccion funciona."

"Las siguientes, heap_no_invade y sin_fugas_usyscall, confirman que el heap no
crece sobre esa pagina y que crear y destruir 40 procesos no pierde memoria."

### Parte B (salida de vmprint)
"Aqui vemos la salida de vmprint, que imprime la tabla de paginas en sus tres
niveles. La primera linea 'page table 0x...87f23000' es la direccion fisica de la
tabla raiz. Cada '..' indica el nivel de profundidad."

Senalar las tres paginas altas:
"Y lo mas importante, estas tres paginas del final:
 - 0x3fffffd000 -> nuestra pagina USYSCALL. Su PTE termina en 13, que en binario
   tiene Valida + Lectura + Usuario, pero NO Escritura: por eso es de solo lectura.
 - 0x3fffffe000 -> el TRAPFRAME.
 - 0x3ffffff000 -> el TRAMPOLINE.
Ademas, de la region perezosa de 8 paginas solo aparece la que el proceso toco
(0x8000); las no tocadas no se imprimen porque no tienen memoria asignada."

### Parte C (ultimas pruebas)
"Las ultimas cuatro pruebas verifican pgaccess: detecta exactamente las paginas
leidas y escritas, limpia el bit tras consultarlo, funciona con el maximo de 64
paginas, rechaza argumentos invalidos, y no asigna memoria a las paginas
perezosas."

Al final:
"Y aqui: TODAS LAS PRUEBAS PASARON, 11 de 11."

---

## AL CORRER  usertests -q

"Ahora la suite oficial de xv6, para demostrar que no rompimos nada del kernel.
Van a aparecer muchos mensajes usertrap() - son normales: varias pruebas provocan
accesos invalidos a proposito para comprobar que el kernel los rechaza. Cada
bloque termina en OK."

Al final:
"ALL TESTS PASSED - la suite completa pasa, sin fugas de memoria."

---

## CIERRE - como lo logramos

"Para lograrlo, primero estudiamos el codigo base de este xv6, que trae asignacion
perezosa. La Parte A fue la base: reservamos una pagina fisica por proceso con
kalloc, guardamos el pid, y la mapeamos con permisos de solo lectura; la liberamos
con cuidado para no tener doble liberacion. La Parte B la resolvimos con un
recorrido recursivo de la tabla, como freewalk, reconstruyendo la direccion
virtual nivel por nivel. Y la Parte C leyendo y limpiando el bit PTE_A, usando
walk sin asignar memoria para respetar las paginas perezosas."

---

## Significado de cada comando (por si preguntan)

- make clean          -> borra lo compilado, para compilar limpio
- make qemu           -> compila el kernel y arranca xv6 en el emulador QEMU
- pgtbltest           -> prueba nuestras 3 partes (11 pruebas)
- usertests -q        -> prueba que el kernel completo sigue funcionando
- Ctrl-a  luego  x    -> salir de QEMU

---

## Salir de QEMU
Presiona Ctrl y a juntos, suelta, y luego pulsa x.
