#ifndef CIFRADO_H
#define CIFRADO_H

#include <stddef.h>
#include <stdint.h>

/* Modifica los datos en el mismo bufer. Devuelve 0 si pudo hacerlo. */
int aplicar_xor(uint8_t *datos, size_t cantidad,
                const uint8_t *clave, size_t longitud_clave,
                uint64_t desplazamiento);

#endif
