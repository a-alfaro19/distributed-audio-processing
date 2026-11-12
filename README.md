# Cliente y cifrado de WAV

| Archivo | Qué hace |
|---|---|
| `src/cliente.c` | Abre el WAV, revisa la firma RIFF/WAVE y envía el tamaño y el audio por bloques. |
| `src/cifrado.c` | Aplica XOR con la clave que recibe. La misma función sirve para descifrar. |
| `include/cifrado.h` | Declara la función de cifrado para que la puedan usar otros módulos. |
| `Pruebas/receptor_prueba.py` | Recibe un archivo en la misma máquina y guarda el WAV recuperado. |

## Probar en Linux

Desde esta carpeta, ejecutá `make`. Usá cualquier WAV con encabezado RIFF/WAVE. En el ejemplo se llama `audio.wav`.

En la primera terminal:

```bash
export AUDIO_XOR_KEY='clave-demo'
python3 Pruebas/receptor_prueba.py 5000 recuperado.wav
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


