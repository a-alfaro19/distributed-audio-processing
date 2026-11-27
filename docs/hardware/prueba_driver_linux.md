# Probar el driver `audiousb` con la matriz en Linux

Pasos para comprobar, de punta a punta, que el driver funciona con la Pico W y la matriz conectadas. Las conexiones, la alimentación y el firmware se explican en [matriz.md](matriz.md). Esta guía parte de una Pico ya cableada al MAX7219.

Orden de la prueba: firmware → USB → compilar → cargar el módulo → probar con `test_audiousb` → probar con `libaudio.a` → desconexión → descarga.

## 0. Requisitos

```bash
sudo apt install build-essential linux-headers-$(uname -r) usbutils mokutil lsof dwarves
```

- El directorio del repo **no puede tener espacios** en la ruta (por ejemplo, `Proyecto 2/`). Kbuild no compila módulos con `M=` en una ruta con espacios. Clona en algo como `~/distributed-audio-processing`.
- Si la máquina tiene **Secure Boot** activo, el kernel rechaza módulos sin firma (ver [Trampas](#trampas)). Revisa con `mokutil --sb-state`.
- `dwarves` es opcional: aporta `pahole` y evita un aviso de Kbuild al compilar.

## 1. Firmware en la Pico

En Arduino IDE: placa *Raspberry Pi Pico W* (grupo *Raspberry Pi RP2040 Boards*), **Herramientas → USB Stack → Adafruit TinyUSB**. No elijas *Adafruit TinyUSB Host (native)*. Carga `src/firmware/pico_matriz/pico_matriz.ino`.

✅ **Esperado:** al conectar la Pico se ve un **corazón** en la matriz. Si no aparece, el problema está en el cableado o en el firmware, no en el driver. Revisa [matriz.md](matriz.md) antes de seguir.

❌ Si la compilación falla con `#error TinyUSB is not selected`, el menú USB Stack sigue en *Pico SDK*. El IDE puede volver a esa opción al cambiar de placa: revísala antes de cada carga.

## 2. La Pico se ve por USB

```bash
lsusb -d 2e8a:4001
lsusb -v -d 2e8a:4001 2>/dev/null | grep -E 'bInterfaceClass|bEndpointAddress'
```

✅ **Esperado:**
- Una línea con `2e8a:4001` (fabricante `CE4303`, producto `Visualizador de audio 8x8`).
- Una interfaz con `bInterfaceClass 255 Vendor Specific Class`, con un endpoint `OUT` y uno `IN`. El driver se enlaza solo a esa interfaz.
- Las otras interfaces (CDC, clase 2 y 10) las toma `cdc_acm` y aparecen como `/dev/ttyACM0`. Es normal: **no** hay que descargar `cdc_acm`, los dos drivers conviven. Ese puerto serie además sirve para cargar firmware nuevo desde el IDE.

❌ Si aparece `2e8a:f00a` (el PID por defecto del core de Arduino) o no hay interfaz de clase 255, el firmware no está cargado o se compiló con *USB Stack = Pico SDK*. Recompila con TinyUSB.

## 3. Compilar el módulo y el programa de prueba

```bash
cd src/driver
make
```

✅ **Esperado:** se generan `audiousb.ko` y `test_audiousb` sin errores ni warnings en `audiousb.c` o `test_audiousb.c`.

Kbuild puede mostrar avisos propios que no indican un problema del código: `pahole version differs` (instala `dwarves`), `Skipping BTF generation ... unavailability of vmlinux` y una nota sobre la versión del compilador.

```bash
modinfo audiousb.ko | grep -E 'alias|description'
```

Debe mostrar `alias: usb:v2E8Ap4001d*dc*dsc*dp*icFFisc*ip*in*`: VID/PID de la Pico e interfaz de clase `FF`. El alias solo se usa para cargar el módulo solo cuando está instalado (`make modules_install` + `depmod`). Con `insmod`, el kernel enlaza la Pico por la `id_table` del driver.

## 4. Cargar el módulo

Abre una segunda terminal con los mensajes del kernel en vivo:

```bash
sudo dmesg -w | grep -iE 'audiousb|usb'
```

En la primera terminal:

```bash
sudo insmod audiousb.ko
lsmod | grep audiousb
ls -l /dev/audiousb*
```

✅ **Esperado:**
- En `dmesg`: `audiousb ...: Pico conectada: /dev/audiousbN (out=0x02 in=0x83)`.
- `/dev/audiousbN` existe, es un dispositivo de caracteres con major 180 (`usbmisc`).
- La primera vez que se carga un módulo propio también salen `loading out-of-tree module taints kernel` y `module verification failed: signature and/or required key missing`. Son normales con Secure Boot desactivado.

**El número `N` no siempre es 0.** Ubuntu y Debian compilan el kernel con `CONFIG_USB_DYNAMIC_MINORS=y`: el driver recibe el primer minor libre de `usbmisc`, que comparte con `/dev/usb/hiddev*`. En una laptop con tres `hiddev`, la matriz queda en `/dev/audiousb3`. Para no depender de ese número, la regla udev de abajo crea el enlace fijo **`/dev/matriz8x8`**, que es la ruta por defecto de `test_audiousb` y de `libaudio.a`.

No importa el orden: si la Pico se conecta después del `insmod`, el `probe` corre al enumerarse. Desconecta y reconecta la Pico para verlo. Cada vez debe aparecer el mensaje.

### Permisos y nombre fijo (una sola vez)

Sin esto, solo root puede abrir el dispositivo, y `mpirun` no corre como root:

```bash
echo 'SUBSYSTEM=="usbmisc", KERNEL=="audiousb[0-9]*", MODE="0666", SYMLINK+="matriz8x8"' | sudo tee /etc/udev/rules.d/99-audiousb.rules
sudo udevadm control --reload && sudo udevadm trigger
ls -l /dev/matriz8x8 /dev/audiousb*     # enlace -> audiousbN, y crw-rw-rw-
```

Si `/dev/matriz8x8` no aparece tras el `trigger`, desconecta y reconecta la Pico.

## 5. Prueba directa del driver: `test_audiousb`

Habla con el driver por `ioctl`/`write`, sin `libaudio.a`. Sirve para aislar fallas del driver.

```bash
./test_audiousb                    # usa /dev/matriz8x8
./test_audiousb /dev/audiousb3     # o una ruta explícita
```

✅ **Esperado en la matriz**, en este orden:

| Paso | Comando | Qué se ve |
|---|---|---|
| 1 | `SET_FRAME {1..8}` | Escalera de barras que **sube** de izquierda a derecha (1 s) |
| 2 | `SET_PATTERN` | Carita sonriente (1 s) |
| 3 | `SET_TIMING` + `CLEAR` + `SET_POINT (3,4)` | Todo apagado salvo un LED: columna 3, fila 4, contando desde 0 arriba a la izquierda (1 s) |
| 4 | `SET_FRAME {9,...}` | Nada cambia. El driver lo rechaza |
| 5 | `write()` de 8 bytes `{8..1}` | Escalera que **baja** de izquierda a derecha. Aparece justo después del paso 3 y se queda al terminar |

✅ **Esperado en la terminal:** solo una línea, `nivel invalido rechazado: Invalid argument (esperado)`. Cualquier otra línea (`SET_FRAME: ...`, `write: ...`) es un error.

❌ Si sale `open: No such file or directory`, falta el enlace `/dev/matriz8x8` (regla udev) o el módulo no se enlazó (volver al paso 4). Si sale `Permission denied`, faltan los permisos.

### Pruebas manuales desde la shell

`write()` acepta exactamente 8 bytes binarios (niveles 0..8), uno por columna. Usa `bash`: el `printf` de `sh`/`dash` no entiende `\x`.

```bash
printf '\x08\x08\x08\x08\x08\x08\x08\x08' > /dev/matriz8x8   # todas las barras llenas
printf '\x00\x02\x04\x06\x08\x06\x04\x02' > /dev/matriz8x8   # pico al centro
```

Casos que **deben fallar** con `write error: Invalid argument`:

```bash
echo 12345678 > /dev/matriz8x8                               # 9 bytes (echo agrega \n) y valores ASCII > 8
printf '\x09\x00\x00\x00\x00\x00\x00\x00' > /dev/matriz8x8   # nivel 9 fuera de rango
printf '\x01\x02\x03' > /dev/matriz8x8                       # menos de 8 bytes
```

`read()` no tiene datos: el firmware no envía nada por el endpoint IN. `cat /dev/matriz8x8` termina tras ~1 s con `Connection timed out` y deja un `error en bulk IN (-110)` en `dmesg`. Es lo esperado.

## 6. Prueba a través de `libaudio.a`

Desde la raíz del repo:

```bash
make probar-libaudio                 # sin hardware: valida argumentos
make demo-matriz && ./bin/demo_matriz        # usa /dev/matriz8x8; acepta una ruta como argumento
```

✅ **Esperado:** `probar-libaudio` termina con `Todas las pruebas pasaron.`. La demo muestra, en orden: todos los LEDs encendidos, corazón, carita y tablero de ajedrez (1,5 s cada uno). Luego un barrido que enciende los LEDs uno por uno de arriba a la izquierda a abajo a la derecha. Al final, seis ráfagas de barras que suben de golpe y **bajan despacio** (atenuación). Termina con `Demo terminada.` y la matriz apagada.

## 7. Desconexión en caliente

Comprueba que el driver no se cuelga ni deja el kernel en mal estado si la Pico se desconecta en uso.

```bash
./bin/demo_matriz &      # o deja corriendo test_audiousb
# desconecta el cable USB de la Pico mientras corre
```

✅ **Esperado:**
- La demo termina con `<paso>: No such device` (`ENODEV`), por ejemplo `punto: No such device`. Si la desconexión cae justo durante una transferencia, el error puede ser otro (`-EPROTO`, `-ESHUTDOWN`) y queda en `dmesg` como `error en bulk OUT`. En ningún caso se cuelga.
- En `dmesg`: `Pico desconectada`, sin `BUG`, `Oops` ni `WARNING`.
- `/dev/audiousbN` y `/dev/matriz8x8` desaparecen. Al reconectar vuelven a aparecer (el número `N` puede cambiar, el enlace no) y `./test_audiousb` funciona de nuevo **sin** recargar el módulo.

## 8. Descargar el módulo

```bash
sudo rmmod audiousb
lsmod | grep audiousb    # sin salida
```

✅ **Esperado:** `rmmod` termina sin error con la Pico conectada o desconectada, y `dmesg` no muestra advertencias.

❌ `rmmod: ERROR: Module audiousb is in use` significa que algún proceso todavía tiene abierta la matriz. Búscalo con `sudo lsof /dev/audiousb*` y ciérralo antes.

## Checklist rápido

- [ ] Corazón al encender la Pico
- [ ] `lsusb` muestra `2e8a:4001` con interfaz de clase 255
- [ ] `make` en `src/driver` sin warnings del código
- [ ] `insmod` → `Pico conectada: /dev/audiousbN` en `dmesg`
- [ ] `/dev/matriz8x8` existe y apunta a `audiousbN`
- [ ] `test_audiousb`: 5 pasos visibles y solo la línea de "rechazado"
- [ ] `printf` con 8 bytes funciona, y los casos inválidos dan `Invalid argument`
- [ ] `probar-libaudio` y `demo_matriz` completos
- [ ] Desconexión en caliente sin `Oops`, y reconexión sin recargar
- [ ] `rmmod` limpio

## Trampas

- ⚠️ **`sudo: a terminal is required to read the password`.** Pasa al ejecutar `sudo` desde una herramienta sin terminal interactiva (un IDE, un script lanzado sin TTY). Ejecuta `insmod`, `rmmod` y la regla udev en una terminal normal. Después de la regla udev, las pruebas no necesitan `sudo`.
- ⚠️ **`insmod: ERROR: could not insert module audiousb.ko: Key was rejected by service`.** Secure Boot activo: el kernel solo carga módulos firmados. Desactiva Secure Boot en la BIOS, o firma el módulo con una clave MOK (`mokutil --import`). En una VM, lo más simple es desactivarlo.
- ⚠️ **`insmod: ERROR: ... Invalid module format`.** El módulo se compiló contra otros headers que el kernel en ejecución (por ejemplo, después de actualizar el kernel sin reiniciar). Reinicia, o reinstala `linux-headers-$(uname -r)`, y luego `make clean && make`.
- ⚠️ **`make` falla con `No rule to make target` o rutas cortadas.** La ruta del repo tiene espacios. Mueve el repo a una ruta sin espacios.
- ⚠️ **En una VM (VirtualBox/VMware) la Pico no aparece en `lsusb`.** Hay que pasar el dispositivo USB a la VM (filtro USB para `2e8a:4001`). Ojo: al cargar el firmware, la Pico se re-enumera y puede volver al host. Revisa el filtro después de cada carga.
- ⚠️ **`dmesg` muestra `error en bulk OUT (-110)`.** La Pico no está leyendo el endpoint (firmware colgado o en modo BOOTSEL). Desconecta y reconecta la Pico.
- ⚠️ **`dmesg` se llena de mensajes `apparmor="DENIED"`.** Son de otras aplicaciones (snaps). Filtra con `sudo dmesg | grep audiousb`.
