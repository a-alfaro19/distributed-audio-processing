import argparse
import os
from pathlib import Path
import socket
import tempfile


def recibir_exactos(conexion, cantidad):
    # TCP puede entregar los bytes en varias lecturas.
    datos = bytearray()
    while len(datos) < cantidad:
        bloque = conexion.recv(cantidad - len(datos))
        if not bloque:
            raise ValueError("El cliente cerró antes de enviar todos los bytes.")
        datos.extend(bloque)
    return datos


def descifrar(datos, clave, desplazamiento):
    # Se usa la pos del byte en el mensaje completo.
    return bytes(
        byte ^ clave[(desplazamiento + i) % len(clave)]
        for i, byte in enumerate(datos)
    )


def guardar_wav(conexion, clave, salida):
    cabecera = descifrar(recibir_exactos(conexion, 8), clave, 0)
    tamano = int.from_bytes(cabecera, "big")
    if tamano < 12:
        raise ValueError("El tamaño recibido no corresponde a un WAV.")

    firma = descifrar(recibir_exactos(conexion, 12), clave, 8)
    if firma[:4] != b"RIFF" or firma[8:] != b"WAVE":
        raise ValueError("No se obtuvo la firma WAV. Revise la clave.")

    # Publicamos el archivo de salida cuando se recupera completo.
    with tempfile.NamedTemporaryFile(dir=salida.parent, delete=False) as archivo:
        temporal = Path(archivo.name)
        try:
            archivo.write(firma)
            guardados = len(firma)
            while guardados < tamano:
                bloque = recibir_exactos(conexion, min(8192, tamano - guardados))
                archivo.write(descifrar(bloque, clave, 8 + guardados))
                guardados += len(bloque)
            archivo.close()
            temporal.replace(salida)
        finally:
            temporal.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description="Recibe un WAV para probar el cliente.")
    parser.add_argument("puerto", type=int)
    parser.add_argument("salida", type=Path)
    args = parser.parse_args()
    clave = os.environb.get(b"AUDIO_XOR_KEY")
    if not clave:
        parser.error("Definí AUDIO_XOR_KEY en esta terminal.")

    try:
        # Escucha en la misma maquina y atiende una sola transferencia.
        with socket.create_server(("127.0.0.1", args.puerto)) as servidor:
            servidor.settimeout(60)
            print(f"Escuchando en 127.0.0.1:{servidor.getsockname()[1]}", flush=True)
            conexion, _ = servidor.accept()
            with conexion:
                conexion.settimeout(10)
                guardar_wav(conexion, clave, args.salida)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")

    print(f"WAV recuperado en {args.salida}")


if __name__ == "__main__":
    main()
