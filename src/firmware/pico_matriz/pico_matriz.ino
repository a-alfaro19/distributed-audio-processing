// Firmware del visualizador 8x8: Raspberry Pi Pico W + modulo MAX7219.
//
// Recibe por USB (interfaz vendor, endpoint bulk OUT) los comandos que envia
// el driver audiousb y los dibuja en la matriz. El protocolo esta definido en
// src/driver/audiousb_ioctl.h; los valores de abajo deben coincidir con el.
//
// Arduino IDE: placa "Raspberry Pi Pico W" (nucleo de Earle Philhower) y
// Herramientas > USB Stack > "Adafruit TinyUSB".
// Conexiones y alimentacion: docs/hardware/matriz.md

#include <Adafruit_TinyUSB.h>

// ---- Pines (GPIO de la Pico W hacia el modulo) ----
const uint8_t PIN_DIN = 19;
const uint8_t PIN_CLK = 18;
const uint8_t PIN_CS = 17;

// ---- Identificacion USB: debe coincidir con VENDOR_ID/PRODUCT_ID del driver ----
const uint16_t USB_VID = 0x2E8A;
const uint16_t USB_PID = 0x4001;

// ---- Protocolo (audiousb_ioctl.h) ----
const uint8_t CMD_FRAME = 0x01;
const uint8_t CMD_POINT = 0x02;
const uint8_t CMD_CLEAR = 0x03;
const uint8_t CMD_TIMING = 0x04;
const uint8_t CMD_PATTERN = 0x05;
const uint8_t FILAS = 8;
const uint8_t COLUMNAS = 8;
const uint8_t MAX_PAQUETE = 1 + FILAS;

// ---- MAX7219 ----
const uint8_t REG_DECODE = 0x09;
const uint8_t REG_INTENSITY = 0x0A;
const uint8_t REG_SCAN_LIMIT = 0x0B;
const uint8_t REG_SHUTDOWN = 0x0C;
const uint8_t REG_TEST = 0x0F;
// Brillo 0..15. Con 4 la matriz consume ~90 mA con los 64 LEDs encendidos
// (ver presupuesto de corriente en docs/hardware/matriz.md).
const uint8_t BRILLO = 4;

const uint16_t REFRESCO_INICIAL_MS = 50;

// Imagen que se muestra al encender, hasta que llegue el primer comando.
const uint8_t IMAGEN_INICIO[FILAS] = {
  0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00
};

Adafruit_USBD_WebUSB usb_vendor;

enum Modo { MODO_PATRON, MODO_BARRAS };

Modo modo = MODO_PATRON;
uint8_t patron[FILAS];        // fila 0 arriba, bit 7 = columna 0
float altura[COLUMNAS];       // altura mostrada de cada barra
uint8_t objetivo[COLUMNAS];   // ultimo nivel recibido por FRAME
uint16_t refresco_ms = REFRESCO_INICIAL_MS;
uint8_t decay = 0;
uint32_t ultimo_tick = 0;

uint8_t paquete[MAX_PAQUETE];
uint8_t recibidos = 0;

// ---------------- MAX7219 ----------------

void escribir_registro(uint8_t reg, uint8_t valor) {
  digitalWrite(PIN_CS, LOW);
  shiftOut(PIN_DIN, PIN_CLK, MSBFIRST, reg);
  shiftOut(PIN_DIN, PIN_CLK, MSBFIRST, valor);
  digitalWrite(PIN_CS, HIGH);
}

void iniciar_max7219() {
  escribir_registro(REG_SHUTDOWN, 0);
  escribir_registro(REG_TEST, 0);
  escribir_registro(REG_DECODE, 0);
  escribir_registro(REG_SCAN_LIMIT, 7);
  escribir_registro(REG_INTENSITY, BRILLO);
  for (uint8_t c = 1; c <= COLUMNAS; c++) escribir_registro(c, 0);
  escribir_registro(REG_SHUTDOWN, 1);
}

// Cableado del modulo: columna C1..C8 = registros 1..8, fila 1 = bit 7.
// Por eso hay que trasponer la imagen (que viene por filas).
void dibujar(const uint8_t filas[FILAS]) {
  for (uint8_t c = 0; c < COLUMNAS; c++) {
    uint8_t columna = 0;
    for (uint8_t r = 0; r < FILAS; r++) {
      uint8_t bit = (filas[r] >> (7 - c)) & 1;
      columna |= bit << (7 - r);
    }
    escribir_registro(c + 1, columna);
  }
}

// ---------------- Modos de dibujo ----------------

void dibujar_barras() {
  uint8_t filas[FILAS] = {0};
  for (uint8_t c = 0; c < COLUMNAS; c++) {
    uint8_t h = (uint8_t)(altura[c] + 0.5f);
    for (uint8_t r = FILAS - h; r < FILAS; r++) filas[r] |= 0x80 >> c;
  }
  dibujar(filas);
}

void mostrar_patron(const uint8_t *filas) {
  modo = MODO_PATRON;
  memcpy(patron, filas, FILAS);
  dibujar(patron);
}

void recibir_niveles(const uint8_t *niveles) {
  if (modo != MODO_BARRAS) {
    modo = MODO_BARRAS;
    for (uint8_t c = 0; c < COLUMNAS; c++) altura[c] = 0;
  }
  for (uint8_t c = 0; c < COLUMNAS; c++) {
    objetivo[c] = min(niveles[c], FILAS);
    // Sube de golpe; sin decay tambien baja de golpe.
    if (objetivo[c] >= altura[c] || decay == 0) altura[c] = objetivo[c];
  }
  dibujar_barras();
}

// Cada tick las barras recorren decay/255 de lo que les falta para bajar.
void atenuar_barras() {
  bool cambio = false;
  for (uint8_t c = 0; c < COLUMNAS; c++) {
    float diferencia = altura[c] - objetivo[c];
    if (diferencia <= 0) continue;
    float nueva = altura[c] - diferencia * decay / 255.0f;
    if (nueva - objetivo[c] < 0.5f) nueva = objetivo[c];
    if ((uint8_t)(nueva + 0.5f) != (uint8_t)(altura[c] + 0.5f)) cambio = true;
    altura[c] = nueva;
  }
  if (cambio) dibujar_barras();
}

// ---------------- Protocolo USB ----------------

// Largo total del comando (incluye el byte de comando); 0 si es desconocido.
uint8_t largo_comando(uint8_t cmd) {
  switch (cmd) {
    case CMD_FRAME: return 1 + COLUMNAS;
    case CMD_PATTERN: return 1 + FILAS;
    case CMD_POINT: return 4;
    case CMD_TIMING: return 4;
    case CMD_CLEAR: return 1;
    default: return 0;
  }
}

void ejecutar(const uint8_t *p) {
  switch (p[0]) {
    case CMD_FRAME:
      recibir_niveles(&p[1]);
      break;
    case CMD_PATTERN:
      mostrar_patron(&p[1]);
      break;
    case CMD_CLEAR: {
      const uint8_t vacio[FILAS] = {0};
      mostrar_patron(vacio);
      break;
    }
    case CMD_POINT: {
      uint8_t x = p[1], y = p[2];
      if (x >= COLUMNAS || y >= FILAS) return;
      uint8_t filas[FILAS];
      memcpy(filas, patron, FILAS);
      if (p[3]) filas[y] |= 0x80 >> x;
      else filas[y] &= ~(0x80 >> x);
      mostrar_patron(filas);
      break;
    }
    case CMD_TIMING: {
      uint16_t ms = p[1] | (p[2] << 8);
      if (ms >= 1 && ms <= 1000) refresco_ms = ms;
      decay = p[3];
      break;
    }
  }
}

// El endpoint vendor entrega un flujo de bytes: se arma cada paquete
// segun el largo que indica su primer byte.
void procesar_usb() {
  while (usb_vendor.available()) {
    int byte_leido = usb_vendor.read();
    if (byte_leido < 0) return;

    if (recibidos == 0 && largo_comando((uint8_t)byte_leido) == 0) continue;  // basura: se descarta
    paquete[recibidos++] = (uint8_t)byte_leido;

    if (recibidos == largo_comando(paquete[0])) {
      ejecutar(paquete);
      recibidos = 0;
    }
  }
}

// ---------------- Arduino ----------------

void setup() {
  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  iniciar_max7219();
  mostrar_patron(IMAGEN_INICIO);

  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setID(USB_VID, USB_PID);
  TinyUSBDevice.setManufacturerDescriptor("CE4303");
  TinyUSBDevice.setProductDescriptor("Visualizador de audio 8x8");
  usb_vendor.begin();

  // Re-enumerar para que el host vea el VID/PID y la interfaz vendor nuevos.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  procesar_usb();

  uint32_t ahora = millis();
  if (modo == MODO_BARRAS && decay > 0 && ahora - ultimo_tick >= refresco_ms) {
    ultimo_tick = ahora;
    atenuar_barras();
  }
}
