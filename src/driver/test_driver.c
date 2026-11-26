/*
 * test_driver.c - Suite de pruebas del driver audiousb (evidencia de funcionamiento)
 *
 * Uso:  LOOPBACK=1 ./test_driver [/dev/audiousb0]
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "audiousb_ioctl.h"

static int total, fails, loopback;

static void report(const char *name, int pass)
{
	total++;
	if (!pass)
		fails++;
	printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
}

/* ret < 0 y errno == err */
static int err_is(int ret, int err)
{
	return ret < 0 && errno == err;
}

/* Lee el eco de la Pico y lo compara con el paquete esperado */
static void check_echo(int fd, const char *name, const unsigned char *exp, size_t len)
{
	unsigned char buf[64];
	ssize_t n;

	if (!loopback)
		return;
	n = read(fd, buf, sizeof(buf));
	report(name, n == (ssize_t)len && memcmp(buf, exp, len) == 0);
}

static void test_valid(int fd)
{
	struct audiousb_frame f = { .levels = { 0, 1, 2, 3, 4, 5, 6, 8 } };
	struct audiousb_point p = { .x = AUDIOUSB_COLS - 1, .y = AUDIOUSB_ROWS - 1, .on = 1 };
	struct audiousb_timing t = { .refresh_ms = AUDIOUSB_REFRESH_MAX_MS, .decay = 255 };
	struct audiousb_timing tmin = { .refresh_ms = AUDIOUSB_REFRESH_MIN_MS, .decay = 0 };
	unsigned char frame_pkt[1 + AUDIOUSB_FRAME_SIZE];
	unsigned char point_pkt[4] = { AUDIOUSB_CMD_POINT, AUDIOUSB_COLS - 1, AUDIOUSB_ROWS - 1, 1 };
	unsigned char clear_pkt[1] = { AUDIOUSB_CMD_CLEAR };
	unsigned char tmax_pkt[4] = { AUDIOUSB_CMD_TIMING, 1000 & 0xFF, 1000 >> 8, 255 };
	unsigned char tmin_pkt[4] = { AUDIOUSB_CMD_TIMING, 1, 0, 0 };

	frame_pkt[0] = AUDIOUSB_CMD_FRAME;
	memcpy(&frame_pkt[1], f.levels, sizeof(f.levels));

	report("SET_FRAME valido (niveles 0..8) devuelve 0",
	       ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &f) == 0);
	check_echo(fd, "SET_FRAME: paquete recibido por la Pico == esperado",
		   frame_pkt, sizeof(frame_pkt));

	report("write() de 8 bytes (frame crudo) devuelve 8",
	       write(fd, f.levels, sizeof(f.levels)) == AUDIOUSB_FRAME_SIZE);
	check_echo(fd, "write(): paquete recibido por la Pico == esperado",
		   frame_pkt, sizeof(frame_pkt));

	report("SET_POINT en la esquina (7,7) devuelve 0",
	       ioctl(fd, AUDIOUSB_IOC_SET_POINT, &p) == 0);
	check_echo(fd, "SET_POINT: paquete recibido == esperado",
		   point_pkt, sizeof(point_pkt));

	report("CLEAR devuelve 0", ioctl(fd, AUDIOUSB_IOC_CLEAR) == 0);
	check_echo(fd, "CLEAR: paquete recibido == esperado",
		   clear_pkt, sizeof(clear_pkt));

	report("SET_TIMING en el limite maximo (1000 ms) devuelve 0",
	       ioctl(fd, AUDIOUSB_IOC_SET_TIMING, &t) == 0);
	check_echo(fd, "SET_TIMING max: paquete recibido == esperado (little-endian)",
		   tmax_pkt, sizeof(tmax_pkt));

	report("SET_TIMING en el limite minimo (1 ms) devuelve 0",
	       ioctl(fd, AUDIOUSB_IOC_SET_TIMING, &tmin) == 0);
	check_echo(fd, "SET_TIMING min: paquete recibido == esperado",
		   tmin_pkt, sizeof(tmin_pkt));
}

/* Ninguna de estas llamadas debe generar trafico USB */
static void test_invalid(int fd)
{
	struct audiousb_frame f = { .levels = { 9, 0, 0, 0, 0, 0, 0, 0 } };
	struct audiousb_point p;
	struct audiousb_timing t;
	unsigned char raw[AUDIOUSB_FRAME_SIZE + 1] = { 0 };
	unsigned char buf[64];
	void *volatile badptr = (void *)1;	/* volatile: evita el aviso del compilador */

	report("SET_FRAME con nivel 9 -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &f), EINVAL));

	p = (struct audiousb_point){ .x = AUDIOUSB_COLS, .y = 0, .on = 1 };
	report("SET_POINT con x fuera de rango -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_POINT, &p), EINVAL));
	p = (struct audiousb_point){ .x = 0, .y = AUDIOUSB_ROWS, .on = 1 };
	report("SET_POINT con y fuera de rango -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_POINT, &p), EINVAL));
	p = (struct audiousb_point){ .x = 0, .y = 0, .on = 2 };
	report("SET_POINT con on=2 -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_POINT, &p), EINVAL));

	t = (struct audiousb_timing){ .refresh_ms = 0, .decay = 1 };
	report("SET_TIMING con refresh 0 -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_TIMING, &t), EINVAL));
	t = (struct audiousb_timing){ .refresh_ms = AUDIOUSB_REFRESH_MAX_MS + 1, .decay = 1 };
	report("SET_TIMING con refresh 1001 -> EINVAL",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_TIMING, &t), EINVAL));

	report("ioctl desconocido -> ENOTTY",
	       err_is(ioctl(fd, _IO(AUDIOUSB_IOC_MAGIC, 99)), ENOTTY));
	report("ioctl de otro 'magic' -> ENOTTY",
	       err_is(ioctl(fd, _IO('Z', 1)), ENOTTY));

	report("write() de 7 bytes -> EINVAL",
	       err_is((int)write(fd, raw, AUDIOUSB_FRAME_SIZE - 1), EINVAL));
	report("write() de 9 bytes -> EINVAL",
	       err_is((int)write(fd, raw, AUDIOUSB_FRAME_SIZE + 1), EINVAL));
	memset(raw, 0, sizeof(raw));
	raw[AUDIOUSB_FRAME_SIZE - 1] = AUDIOUSB_ROWS + 1;
	report("write() con un nivel fuera de rango -> EINVAL",
	       err_is((int)write(fd, raw, AUDIOUSB_FRAME_SIZE), EINVAL));

	report("ioctl con puntero de usuario nulo -> EFAULT (sin oops)",
	       err_is(ioctl(fd, AUDIOUSB_IOC_SET_FRAME, (void *)0), EFAULT));
	report("write() con puntero de usuario invalido -> EFAULT (sin oops)",
	       err_is((int)write(fd, badptr, AUDIOUSB_FRAME_SIZE), EFAULT));

	if (loopback)
		report("read() sin datos pendientes -> ETIMEDOUT (no se cuelga)",
		       err_is((int)read(fd, buf, sizeof(buf)), ETIMEDOUT));
}

static void test_stress(int fd, int n)
{
	struct audiousb_frame f;
	unsigned char pkt[1 + AUDIOUSB_FRAME_SIZE], buf[64];
	struct timespec a, b;
	double us, sum = 0, max = 0;
	char name[96];
	int i, j, ok = 0;

	pkt[0] = AUDIOUSB_CMD_FRAME;
	for (i = 0; i < n; i++) {
		for (j = 0; j < AUDIOUSB_COLS; j++)
			f.levels[j] = (i + j) % (AUDIOUSB_ROWS + 1);
		memcpy(&pkt[1], f.levels, sizeof(f.levels));

		clock_gettime(CLOCK_MONOTONIC, &a);
		if (ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &f) < 0)
			break;
		if (loopback) {
			ssize_t r = read(fd, buf, sizeof(buf));

			if (r != (ssize_t)sizeof(pkt) || memcmp(buf, pkt, sizeof(pkt)))
				break;
		}
		clock_gettime(CLOCK_MONOTONIC, &b);

		us = (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3;
		sum += us;
		if (us > max)
			max = us;
		ok++;
	}

	printf("  latencia %s: media %.0f us, maxima %.0f us (%d iteraciones)\n",
	       loopback ? "ida y vuelta (envio+eco)" : "de envio", ok ? sum / ok : 0, max, ok);
	snprintf(name, sizeof(name), "estres: %d frames consecutivos correctos", n);
	report(name, ok == n);
}

/* Varios procesos usando el mismo dispositivo: no debe haber errores inesperados */
static void test_concurrent(const char *path, int nproc, int iters)
{
	pid_t pids[8];
	char name[96];
	int i, st, bad = 0;

	if (nproc > 8)
		nproc = 8;

	for (i = 0; i < nproc; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			struct audiousb_frame f;
			unsigned char buf[64];
			int k, fd = open(path, O_RDWR);

			if (fd < 0)
				_exit(2);
			memset(&f, 0, sizeof(f));
			for (k = 0; k < iters; k++) {
				ssize_t r;

				f.levels[0] = (i + k) % (AUDIOUSB_ROWS + 1);
				/* un timeout (1 s) es aceptable con el firmware loopback */
				if (ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &f) < 0 && errno != ETIMEDOUT)
					_exit(1);
				if (loopback) {
					r = read(fd, buf, sizeof(buf));
					(void)r;
				}
			}
			close(fd);
			_exit(0);
		}
	}
	for (i = 0; i < nproc; i++) {
		if (pids[i] < 0 || waitpid(pids[i], &st, 0) < 0 ||
		    !WIFEXITED(st) || WEXITSTATUS(st) != 0)
			bad++;
	}
	snprintf(name, sizeof(name), "concurrencia: %d procesos x %d envios sin errores inesperados",
		 nproc, iters);
	report(name, bad == 0);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/audiousb0";
	unsigned char buf[64];
	int fd;

	loopback = getenv("LOOPBACK") != NULL;
	printf("== test_driver: %s (modo %s) ==\n", path,
	       loopback ? "LOOPBACK, eco verificado" : "solo codigos de retorno");

	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 2;
	}
	report("open() del nodo /dev/audiousb* correcto", 1);

	test_valid(fd);
	test_invalid(fd);
	test_stress(fd, 1000);
	test_concurrent(path, 3, 4);

	/* Vaciar ecos que hayan quedado pendientes tras la prueba concurrente */
	if (loopback)
		while (read(fd, buf, sizeof(buf)) > 0)
			;

	report("close() del nodo correcto", close(fd) == 0);

	printf("\nResultado: %d/%d pruebas OK\n", total - fails, total);
	return fails ? 1 : 0;
}