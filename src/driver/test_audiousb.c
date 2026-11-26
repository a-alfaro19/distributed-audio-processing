/*
 * test_audiousb.c - Prueba del driver sin necesitar libaudio.a
 * Uso: ./test_audiousb [/dev/audiousb0]
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include "audiousb_ioctl.h"

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/audiousb0";
	struct audiousb_frame frame = { .levels = { 1, 2, 3, 4, 5, 6, 7, 8 } };
	struct audiousb_point point = { .x = 3, .y = 4, .on = 1 };
	struct audiousb_timing timing = { .refresh_ms = 50, .decay = 10 };
	struct audiousb_frame bad = { .levels = { 9, 0, 0, 0, 0, 0, 0, 0 } };
	struct audiousb_pattern pattern = {
		.rows = { 0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C } };
	int fd = open(path, O_RDWR);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &frame) < 0)
		perror("SET_FRAME");
	sleep(1);

	if (ioctl(fd, AUDIOUSB_IOC_SET_PATTERN, &pattern) < 0)
		perror("SET_PATTERN");
	sleep(1);

	if (ioctl(fd, AUDIOUSB_IOC_SET_TIMING, &timing) < 0)
		perror("SET_TIMING");

	if (ioctl(fd, AUDIOUSB_IOC_CLEAR) < 0)
		perror("CLEAR");
	if (ioctl(fd, AUDIOUSB_IOC_SET_POINT, &point) < 0)
		perror("SET_POINT");
	sleep(1);

	/* Debe fallar con EINVAL: nivel fuera de rango */
	if (ioctl(fd, AUDIOUSB_IOC_SET_FRAME, &bad) < 0)
		printf("nivel invalido rechazado: %s (esperado)\n", strerror(errno));

	/* Escritura cruda de un frame de 8 bytes */
	if (write(fd, "\x08\x07\x06\x05\x04\x03\x02\x01", AUDIOUSB_FRAME_SIZE) < 0)
		perror("write");

	close(fd);
	return 0;
}