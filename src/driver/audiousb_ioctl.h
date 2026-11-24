/*
 * audiousb_ioctl.h - Contrato entre el driver, la biblioteca (libaudio.a)
 * y el firmware de la Raspberry Pi Pico.
 *
 */
#ifndef AUDIOUSB_IOCTL_H
#define AUDIOUSB_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Geometria de la matriz de LEDs */
#define AUDIOUSB_COLS        8
#define AUDIOUSB_ROWS        8
#define AUDIOUSB_FRAME_SIZE  AUDIOUSB_COLS   /* un byte (0..ROWS) por columna */

#define AUDIOUSB_REFRESH_MIN_MS  1
#define AUDIOUSB_REFRESH_MAX_MS  1000

/*
 * Protocolo USB host -> Pico (transferencia bulk OUT).
 * Cada transferencia es un paquete: [cmd][payload...]
 *
 *   FRAME  : 0x01 + 8 bytes (nivel 0..8 por columna)
 *   POINT  : 0x02 + x + y + on(0/1)
 *   CLEAR  : 0x03
 *   TIMING : 0x04 + refresh_ms (uint16 little-endian) + decay (uint8)
 */
#define AUDIOUSB_CMD_FRAME   0x01
#define AUDIOUSB_CMD_POINT   0x02
#define AUDIOUSB_CMD_CLEAR   0x03
#define AUDIOUSB_CMD_TIMING  0x04

struct audiousb_frame {
	__u8 levels[AUDIOUSB_COLS];
};

struct audiousb_point {
	__u8 x;     /* columna 0..COLS-1 */
	__u8 y;     /* fila 0..ROWS-1 */
	__u8 on;    /* 1 = encender, 0 = apagar */
};

struct audiousb_timing {
	__u16 refresh_ms;   /* periodo de refresco */
	__u8  decay;        /* atenuacion (decay) de las barras */
	__u8  reserved;
};

#define AUDIOUSB_IOC_MAGIC  'A'

#define AUDIOUSB_IOC_SET_FRAME   _IOW(AUDIOUSB_IOC_MAGIC, 1, struct audiousb_frame)
#define AUDIOUSB_IOC_SET_POINT   _IOW(AUDIOUSB_IOC_MAGIC, 2, struct audiousb_point)
#define AUDIOUSB_IOC_CLEAR       _IO(AUDIOUSB_IOC_MAGIC, 3)
#define AUDIOUSB_IOC_SET_TIMING  _IOW(AUDIOUSB_IOC_MAGIC, 4, struct audiousb_timing)

#endif /* AUDIOUSB_IOCTL_H */