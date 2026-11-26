#!/bin/bash
# run_evidence.sh - Genera el log de evidencia del driver audiousb.
# Uso (con la Pico conectada, en la carpeta del driver):
#     make && sudo ./run_evidence.sh
# Con firmware que NO hace eco:  sudo NOLOOP=1 ./run_evidence.sh
set -u
cd "$(dirname "$0")" || exit 1
[ "$(id -u)" -eq 0 ] || { echo "Ejecutar con sudo"; exit 1; }

LOG="evidence_$(date +%Y%m%d_%H%M%S).log"
exec > >(tee "$LOG") 2>&1

PASS=0; FAIL=0
sec()  { echo; echo "==== $* ===="; }
ok()   { echo "[PASS] $*"; PASS=$((PASS+1)); }
bad()  { echo "[FAIL] $*"; FAIL=$((FAIL+1)); }
wait_node() {
	for _ in $(seq 1 30); do
		ls /dev/audiousb* >/dev/null 2>&1 && return 0
		sleep 0.1
	done
	return 1
}
run_tests() {
	if [ -z "${NOLOOP:-}" ]; then LOOPBACK=1 ./test_driver "$1"; else ./test_driver "$1"; fi
}

sec "Entorno"
date
uname -a
lsusb
BASE=$(dmesg | wc -l)   # solo se analizan las lineas de dmesg posteriores a este punto

sec "1. Carga del modulo de kernel"
rmmod audiousb 2>/dev/null
if insmod ./audiousb.ko; then ok "insmod audiousb.ko"; else bad "insmod audiousb.ko"; fi
sleep 1
if lsmod | grep -w audiousb; then ok "el modulo aparece en lsmod"; else bad "el modulo NO aparece en lsmod"; fi
modinfo ./audiousb.ko | grep -E "^(filename|license|description|vermagic|alias)"

sec "2. El driver propio (no uno del sistema) es el que reclama la Pico"
lsusb -t
ls -l /sys/bus/usb/drivers/audiousb/
if ls /sys/bus/usb/drivers/audiousb/ | grep -qE '^[0-9]+-[0-9.]+:'; then
	ok "interfaz USB enlazada al driver audiousb"
else
	bad "ninguna interfaz enlazada a audiousb"
fi
if wait_node; then ok "nodo /dev/audiousb* creado"; else bad "no se creo el nodo"; fi
ls -l /dev/audiousb* 2>/dev/null
NODE=$(ls /dev/audiousb* 2>/dev/null | head -1)
if [ -z "$NODE" ]; then
	echo "Sin nodo /dev/audiousb*: conecte la Pico y reintente."
	exit 1
fi

sec "3. Pruebas funcionales, de errores, estres y concurrencia"
if [ -x ./test_driver ]; then
	if run_tests "$NODE"; then ok "test_driver: todas las pruebas OK"; else bad "test_driver: hubo pruebas fallidas"; fi
else
	bad "falta ./test_driver (ejecute make)"
fi

sec "4. rmmod con el nodo abierto debe ser rechazado"
sleep 600 < "$NODE" &
SPID=$!
sleep 0.5
if rmmod audiousb 2>&1; then
	bad "rmmod se permitio con el nodo abierto"
else
	ok "rmmod rechazado mientras el dispositivo esta en uso"
fi
kill "$SPID" 2>/dev/null; wait "$SPID" 2>/dev/null

sec "5. 20 ciclos de insmod/rmmod"
CYC=0
for i in $(seq 1 20); do
	rmmod audiousb 2>/dev/null
	if ! insmod ./audiousb.ko; then bad "ciclo $i: insmod fallo"; CYC=1; break; fi
	if ! wait_node; then bad "ciclo $i: no aparecio el nodo"; CYC=1; break; fi
done
[ "$CYC" -eq 0 ] && ok "20 ciclos de carga/descarga sin errores"
NODE=$(ls /dev/audiousb* 2>/dev/null | head -1)

sec "6. Desconexion fisica con el archivo abierto (manual)"
sleep 600 < "$NODE" &
SPID=$!
echo ">>> Desconecte la Pico AHORA y presione Enter"
read -r _
sleep 1
if [ -e "$NODE" ]; then bad "el nodo sigue existiendo tras la desconexion"; else ok "nodo eliminado tras la desconexion"; fi
kill "$SPID" 2>/dev/null; wait "$SPID" 2>/dev/null   # aqui release() libera el kref
sleep 1
echo ">>> Reconecte la Pico y presione Enter"
read -r _
if wait_node; then ok "el driver se reengancha sin recargar el modulo"; else bad "no reaparecio el nodo"; fi
NODE=$(ls /dev/audiousb* 2>/dev/null | head -1)
if [ -n "$NODE" ]; then
	if run_tests "$NODE"; then ok "pruebas tras reconexion OK"; else bad "pruebas tras reconexion fallaron"; fi
fi

sec "7. dmesg generado durante la evidencia"
dmesg | tail -n +$((BASE+1))
ERR=$(dmesg | tail -n +$((BASE+1)) | grep -ciE "BUG:|Oops|WARNING:|Call Trace|KASAN|use-after-free|general protection")
if [ "$ERR" -eq 0 ]; then ok "dmesg sin BUG/Oops/WARNING/Call Trace"; else bad "dmesg contiene $ERR lineas de error del kernel"; fi

sec "RESUMEN"
echo "OK: $PASS   FALLOS: $FAIL"
echo "Log guardado en: $LOG"
[ "$FAIL" -eq 0 ]