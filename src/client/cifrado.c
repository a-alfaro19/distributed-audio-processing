#include "cifrado.h"

#include <errno.h>

int aplicar_xor(uint8_t *datos, size_t cantidad,
                const uint8_t *clave, size_t longitud_clave,
                uint64_t desplazamiento)
{
    if (datos == NULL || clave == NULL || longitud_clave == 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* El desplazamiento indica donde empezar en la clave para este bloque */
    size_t posicion_clave = desplazamiento % longitud_clave;

    for (size_t i = 0; i < cantidad; ++i)
    {
        /* Aplicar XOR dos veces recupera el byte original */
        datos[i] ^= clave[posicion_clave];
        if (++posicion_clave == longitud_clave)
            posicion_clave = 0;
    }

    return 0;
}
