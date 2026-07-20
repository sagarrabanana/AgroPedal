#include "USB.h"
#include "USBMIDI.h"

USBMIDI MIDI;

// ==========================================================
// CONFIGURACIÓN DE HARDWARE
// ==========================================================
const int PIN_P1 = 4;
const int PIN_P2 = 5;
const int PIN_P3 = 6;
const int PIN_LED[4] = {41, 40, 39, 38};
const int PIN_J1 = 9;
const int PIN_J2 = 8;

// ==========================================================
// CALIBRACIÓN
// Pega aquí los valores ABSOLUTOS del script de calibración
// ==========================================================
const int J1_RAW_MIN = 2300;
const int J1_RAW_MAX = 4095;
const int J2_RAW_MIN = 2300;
const int J2_RAW_MAX = 4095;

const unsigned long DEBOUNCE_TIME   = 40;
const unsigned long DOUBLE_TAP_TIME = 350;
const float EMA_ALPHA = 0.15;

const char* NOMBRE_DISPOSITIVO = "AgroPedal";
const char* FABRICANTE         = "Sagarrabanana";

const int MIDI_CH    = 2;
const int NUM_BANCOS = 16;

// ==========================================================
// CLASE BOTÓN
// ==========================================================
class Boton {
  private:
    int pin;
    bool estadoFisicoAnterior   = HIGH;
    unsigned long ultimoCambio  = 0;
    int clicks = 0;
    unsigned long tiempoPrimerClick = 0;

  public:
    bool isPressed         = false;
    bool justPressed       = false;
    bool justReleased      = false;
    bool doubleTapDetected = false;
    bool cancelarRelease   = false; // Suprime el justReleased tras doble tap

    Boton(int p) : pin(p) {}

    void begin() { pinMode(pin, INPUT_PULLUP); }

    void update() {
      bool lectura = digitalRead(pin);
      unsigned long ahora = millis();

      justPressed       = false;
      justReleased      = false;
      doubleTapDetected = false;

      if (lectura != estadoFisicoAnterior) ultimoCambio = ahora;

      if ((ahora - ultimoCambio) > DEBOUNCE_TIME) {
        bool nuevoEstado = !lectura;
        if (nuevoEstado != isPressed) {
          isPressed = nuevoEstado;

          if (isPressed) {
            justPressed = true;
            if (clicks == 0) {
              clicks = 1;
              tiempoPrimerClick = ahora;
            } else {
              if (ahora - tiempoPrimerClick <= DOUBLE_TAP_TIME) {
                doubleTapDetected = true;
                clicks = 0;
              } else {
                clicks = 1;
                tiempoPrimerClick = ahora;
              }
            }
          } else {
            // Al soltar: respetar cancelarRelease
            if (!cancelarRelease) {
              justReleased = true;
            }
            cancelarRelease = false;
          }
        }
      }

      if (clicks == 1 && (ahora - tiempoPrimerClick > DOUBLE_TAP_TIME)) {
        clicks = 0;
      }

      estadoFisicoAnterior = lectura;
    }
};

// ==========================================================
// CLASE POTENCIÓMETRO (CON FILTRADO DE RUIDO POR HISTÉRESIS)
// ==========================================================
class Potenciometro {
  private:
    int pin;
    float emaValue    = 0;
    int lastMidiValue = -1;
    int ccBase;
    int rawMin, rawMax;
    float lastEmaValue = 0; // Almacena el valor analógico de la última transmisión MIDI

  public:
    Potenciometro(int p, int cc, int minR, int maxR)
      : pin(p), ccBase(cc), rawMin(minR), rawMax(maxR) {}

    void begin() {
      pinMode(pin, INPUT);
      emaValue = analogRead(pin);
      lastEmaValue = emaValue;
    }

    void update(int banco) {
      int raw = analogRead(pin);
      emaValue = (EMA_ALPHA * raw) + ((1.0 - EMA_ALPHA) * emaValue);

      int midiVal = map((int)emaValue, rawMin, rawMax, 0, 127);
      midiVal = constrain(midiVal, 0, 127);

      // --- FILTRADO POR HISTÉRESIS ---
      // El valor de 15.0 funciona como umbral de tolerancia al ruido sobre la señal analógica directa.
      // Si hay demasiado ruido aún, se puede subir ligeramente (por ejemplo, a 18.0 o 20.0).
      float threshold = 15.0; 
      float diff = (emaValue > lastEmaValue) ? (emaValue - lastEmaValue) : (lastEmaValue - emaValue);

      // El mensaje se transmite si el cambio supera el umbral,
      // o bien si alcanza los límites absoluto superior o inferior (para no perder el 0 ni el 127).
      if (diff > threshold || (midiVal == 0 && lastMidiValue != 0) || (midiVal == 127 && lastMidiValue != 127)) {
        if (midiVal != lastMidiValue) {
          lastMidiValue = midiVal;
          lastEmaValue = emaValue; // Se establece el nuevo punto de referencia estable
          
          int finalCC = constrain(ccBase + banco, 0, 127);
          MIDI.controlChange(finalCC, midiVal, MIDI_CH);
        }
      }
    }
};

// ==========================================================
// INSTANCIAS
// ==========================================================
Boton btn1(PIN_P1);
Boton btn2(PIN_P2);
Boton btn3(PIN_P3);

Potenciometro pot1(PIN_J1, 10, J1_RAW_MIN, J1_RAW_MAX);
Potenciometro pot2(PIN_J2, 26, J2_RAW_MIN, J2_RAW_MAX);

int banco_actual = 0;

// ==========================================================
// AUXILIARES
// ==========================================================
void actualizarLeds() {
  for (int i = 0; i < 4; i++)
    digitalWrite(PIN_LED[i], (banco_actual >> i) & 1);
}

void animacionArranque() {
  for (int i = 0; i < 4; i++) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  for (int i = 2; i >= 0; i--) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], HIGH);
  delay(200);
  for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], LOW);
  delay(100);
}

int notaBoton(int offsetBoton) {
  return constrain(36 + (banco_actual * 3) + offsetBoton, 0, 127);
}

// Nota de un botón en un banco concreto (para revertir antes de cambiar)
int notaBotonEnBanco(int banco, int offsetBoton) {
  return constrain(36 + (banco * 3) + offsetBoton, 0, 127);
}

void cambiarBanco(int nuevo_banco) {
  banco_actual = nuevo_banco;
  actualizarLeds();
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {
  USB.productName(NOMBRE_DISPOSITIVO);
  USB.manufacturerName(FABRICANTE);
  USB.VID(0x303A);
  USB.PID(0x80AD);
  USB.begin();
  MIDI.begin();

  btn1.begin();
  btn2.begin();
  btn3.begin();
  pot1.begin();
  pot2.begin();

  for (int i = 0; i < 4; i++) {
    pinMode(PIN_LED[i], OUTPUT);
    digitalWrite(PIN_LED[i], LOW);
  }

  animacionArranque();
  actualizarLeds();
}

// ==========================================================
// LOOP PRINCIPAL
// ==========================================================
void loop() {
  btn1.update();
  btn2.update();
  btn3.update();

  // --- GESTIÓN DE BANCOS ---
  if (btn3.doubleTapDetected) {
    // 1. Identificar la nota que se acaba de enviar en el banco actual
    int notaOriginal = notaBotonEnBanco(banco_actual, 2);
    
    // 2. Simular una pulsación completa en el banco de origen para "deshacer" el toggle en el plugin
    MIDI.noteOn(notaOriginal, 127, MIDI_CH);
    MIDI.noteOff(notaOriginal, 0, MIDI_CH);
    
    btn3.cancelarRelease = true; // Evita enviar el Note Off físico en el nuevo banco al soltar
    btn3.justPressed     = false; // Evita enviar un Note On accidental en el nuevo banco

    cambiarBanco((banco_actual + 1) % NUM_BANCOS);
  }

  if (btn1.doubleTapDetected) {
    // 1. Identificar la nota que se acaba de enviar en el banco actual
    int notaOriginal = notaBotonEnBanco(banco_actual, 0);
    
    // 2. Simular una pulsación completa en el banco de origen para "deshacer" el toggle en el plugin
    MIDI.noteOn(notaOriginal, 127, MIDI_CH);
    MIDI.noteOff(notaOriginal, 0, MIDI_CH);

    btn1.cancelarRelease = true; // Evita enviar el Note Off físico en el nuevo banco al soltar
    btn1.justPressed     = false; // Evita enviar un Note On accidental en el nuevo banco

    cambiarBanco((banco_actual + 15) % NUM_BANCOS);
  }

  // --- P1: MOMENTÁNEO ---
  if (btn1.justPressed)  MIDI.noteOn (notaBoton(0), 127, MIDI_CH);
  if (btn1.justReleased) MIDI.noteOff(notaBoton(0),   0, MIDI_CH);

  // --- P2: MOMENTÁNEO ---
  if (btn2.justPressed)  MIDI.noteOn (notaBoton(1), 127, MIDI_CH);
  if (btn2.justReleased) MIDI.noteOff(notaBoton(1),   0, MIDI_CH);

  // --- P3: MOMENTÁNEO ---
  if (btn3.justPressed)  MIDI.noteOn (notaBoton(2), 127, MIDI_CH);
  if (btn3.justReleased) MIDI.noteOff(notaBoton(2),   0, MIDI_CH);

  // --- POTENCIÓMETROS ---
  pot1.update(banco_actual);
  pot2.update(banco_actual);

  delay(2);
}
