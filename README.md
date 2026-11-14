# Cliente y cifrado de WAV

| Archivo | Qué hace |
|---|---|
| `src/client/cliente.c` | Abre el WAV, revisa la firma RIFF/WAVE y envía el tamaño y el audio por bloques. |
| `src/client/cifrado.c` | Aplica XOR con la clave que recibe. La misma función sirve para descifrar. |
| `include/client/cifrado.h` | Declara la función de cifrado para que la puedan usar otros módulos. |
| `pruebas/receptor_prueba.py` | Recibe un archivo en la misma máquina y guarda el WAV recuperado. |

## Probar en Linux

Desde esta carpeta, ejecutá `make`. Usá cualquier WAV con encabezado RIFF/WAVE. En el ejemplo se llama `audio.wav`.

En la primera terminal:

```bash
export AUDIO_XOR_KEY='clave-demo'
python3 pruebas/receptor_prueba.py 5000 recuperado.wav
```

En la segunda terminal, desde la misma carpeta:

```bash
export AUDIO_XOR_KEY='clave-demo'
./cliente 127.0.0.1 5000 audio.wav
```

Cuando el receptor muestre «WAV recuperado», compará los archivos:

```bash
cmp audio.wav recuperado.wav && echo "Archivos idénticos"
```

# Analisis de audio para la matriz de 8 x 8

El modulo recibe entre 1 y 2048 muestras PCM mono de 16 bits y su frecuencia
de muestreo. Aplica Hann y una FFT de 2048 puntos. Agrupa la energia por
frecuencia y entrega ocho alturas de 0 a 8.

| Columna | Rango o dato |
|---|---|
| 1 | 20 a 100 Hz |
| 2 | 100 a 250 Hz |
| 3 | 250 a 500 Hz |
| 4 | 500 a 1000 Hz |
| 5 | 1000 a 2000 Hz |
| 6 | 2000 a 6000 Hz |
| 7 | 6000 a 20000 Hz |
| 8 | Volumen general (RMS) |

Cada rango empieza en su limite inferior y llega hasta el comienzo de la
siguiente banda. Se usa una escala fija de -48 a 0 dBFS y se redondea al
nivel mas cercano. Las alturas se pueden comparar de una ventana a otra.

## Prueba local

Desde esta carpeta:

```bash
make probar-fft
```

La prueba genera silencio y un tono por banda. Para 440 Hz debe destacar
la tercera columna entre las siete bandas. La octava tambien puede
encenderse porque representa el volumen total.

## Probar el WAV recuperado

Desde esta carpeta, inicia el receptor en una terminal:

```bash
AUDIO_XOR_KEY=clave-demo python3 pruebas/receptor_prueba.py 5000 recuperado.wav
```

Cuando el receptor este escuchando, envia el WAV desde otra terminal:

```bash
make cliente prueba_wav
AUDIO_XOR_KEY=clave-demo ./cliente 127.0.0.1 5000 cancion_prueba.wav
```

Cuando aparezca el mensaje de WAV recuperado, compara los archivos y corre
el analisis:

```bash
cmp cancion_prueba.wav recuperado.wav && ./prueba_wav recuperado.wav
```
