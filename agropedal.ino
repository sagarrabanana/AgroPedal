#include "USB.h"
#include "USBMIDI.h"

USBMIDI MIDI;

// ==========================================================
// CONFIGURACIÓN DE HARDWARE
// ==========================================================
const int PIN_P1 = 4;
const int PIN_P2 = 5;
const int PIN_P3 = 6;
const int PIN_LED[4] = {38, 39, 40, 41};
const int PIN_J1 = 9;
const int PIN_J2 = 8;

// ==========================================================
// CALIBRACIÓN POR DEFECTO
// rawMin = lectura ADC en posición mínima (voltaje alto ~3.3V)
// rawMax = lectura ADC en posición máxima (voltaje bajo ~0.97V)
// El mapeo es invertido: rawMin→MIDI 0, rawMax→MIDI 127
// rawMax puede ajustarse en tiempo real si el pot no llega al extremo físico.
// ==========================================================
const int J1_RAW_MIN_DEFAULT = 4050;
const int J1_RAW_MAX_DEFAULT = 1150;
const int J2_RAW_MIN_DEFAULT = 4050;
const int J2_RAW_MAX_DEFAULT = 1150;

// ==========================================================
// TIEMPOS Y AJUSTES
// ==========================================================
const unsigned long DEBOUNCE_TIME   = 40;
const unsigned long DOUBLE_TAP_TIME = 350;
const float         EMA_ALPHA       = 0.15;

// ==========================================================
// IDENTIDAD USB
// ==========================================================
const char* NOMBRE_DISPOSITIVO = "AgroPedal";
const char* FABRICANTE         = "Sagarrabanana";

// ==========================================================
// CONFIGURACIÓN MIDI
// ==========================================================
const int MIDI_CHANNEL = 1;
const int NUM_BANCOS   = 16;
const int NUM_BOTONES  = 3;

// Canal neutro: CCs activos cuando no hay ningún entorno activado
const int POT1_CC_NEUTRO = 1;
const int POT2_CC_NEUTRO = 2;

// CCs de los pots por entorno
// índice_entorno = banco * NUM_BOTONES + índice_botón  (0..47)
// pot1 CC = ENV_CC_BASE + índice_entorno * 2   (10..104)
// pot2 CC = ENV_CC_BASE + índice_entorno * 2 + 1 (11..105)
const int ENV_CC_BASE = 10;

// Señal MIDI de cada botón (Note On al abrir y al cerrar el entorno)
// nota = BTN_NOTE_BASE + índice_entorno  (36..83 → C2..B5)
const int BTN_NOTE_BASE = 36;

// ==========================================================
// CLASE BOTÓN (debounce + doble tap)
// ==========================================================
class Boton {
  private:
    int pin;
    bool estadoFisicoAnterior = HIGH;
    unsigned long ultimoCambio    = 0;
    int           clicks           = 0;
    unsigned long tiempoPrimerClick = 0;

  public:
    bool isPressed         = false;
    bool justPressed       = false;
    bool doubleTapDetected = false;

    Boton(int p) : pin(p) {}

    void begin() {
      pinMode(pin, INPUT_PULLUP);
    }

    void update() {
      bool lectura = digitalRead(pin);
      unsigned long ahora = millis();

      justPressed       = false;
      doubleTapDetected = false;

      if (lectura != estadoFisicoAnterior) ultimoCambio = ahora;

      if ((ahora - ultimoCambio) > DEBOUNCE_TIME) {
        if (lectura != !isPressed) {
          isPressed = !lectura;

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
          }
        }
      }

      if (clicks == 1 && (ahora - tiempoPrimerClick > DOUBLE_TAP_TIME)) clicks = 0;

      estadoFisicoAnterior = lectura;
    }
};

// ==========================================================
// CLASE POTENCIÓMETRO
// rawMax es mutable: puede recalibrarse en tiempo real.
// ==========================================================
class Potenciometro {
  private:
    int   pin;
    float emaValue     = 0;
    int   lastMidiValue = -1;
    int   rawMin;
    int   rawMax;

  public:
    Potenciometro(int p, int minR, int maxR)
      : pin(p), rawMin(minR), rawMax(maxR) {}

    void begin() {
      pinMode(pin, INPUT);
      emaValue = analogRead(pin);
    }

    // Captura la lectura EMA actual como nuevo rawMax (calibración de máximo)
    void calibrarMax() {
      rawMax = (int)emaValue;
    }

    // Fuerza reenvío del valor actual en el próximo update
    void resetEnvio() {
      lastMidiValue = -1;
    }

    void update(int cc) {
      int raw = analogRead(pin);
      emaValue = (EMA_ALPHA * raw) + ((1.0 - EMA_ALPHA) * emaValue);

      int midiVal = map((int)emaValue, rawMin, rawMax, 0, 127);
      midiVal = constrain(midiVal, 0, 127);

      if (midiVal != lastMidiValue) {
        lastMidiValue = midiVal;
        MIDI.controlChange(cc, midiVal, MIDI_CHANNEL);
      }
    }
};

// ==========================================================
// INSTANCIAS
// ==========================================================
Boton btn1(PIN_P1);
Boton btn2(PIN_P2);
Boton btn3(PIN_P3);

Potenciometro pot1(PIN_J1, J1_RAW_MIN_DEFAULT, J1_RAW_MAX_DEFAULT);
Potenciometro pot2(PIN_J2, J2_RAW_MIN_DEFAULT, J2_RAW_MAX_DEFAULT);

// ==========================================================
// ESTADO GLOBAL
// ==========================================================
int banco_actual   = 0;   // 0..15
int entorno_activo = -1;  // -1 = neutro | >=0 = índice de entorno activo

// ==========================================================
// FUNCIONES AUXILIARES — LEDs
// ==========================================================

void actualizarLeds() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN_LED[i], (banco_actual >> i) & 1);
  }
}

void animacionArranque() {
  for (int i = 0; i < 4; i++) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  for (int i = 2; i >= 0; i--) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], HIGH);
  delay(200);
  for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], LOW);
  delay(100);
}

// Tres destellos rápidos de todos los LEDs → confirmación de calibración
void animacionCalibracion() {
  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], HIGH);
    delay(60);
    for (int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], LOW);
    delay(60);
  }
  actualizarLeds(); // Restaurar estado del banco
}

// ==========================================================
// FUNCIONES AUXILIARES — MIDI / ENTORNOS
// ==========================================================

int calcularEntorno(int boton_index) {
  return banco_actual * NUM_BOTONES + boton_index;
}

void getCCsPots(int &cc1, int &cc2) {
  if (entorno_activo < 0) {
    cc1 = POT1_CC_NEUTRO;
    cc2 = POT2_CC_NEUTRO;
  } else {
    cc1 = ENV_CC_BASE + entorno_activo * 2;
    cc2 = ENV_CC_BASE + entorno_activo * 2 + 1;
  }
}

// Envía la señal MIDI (apertura o cierre) asociada a un índice de entorno
void enviarSeñalEntorno(int idx) {
  int nota = BTN_NOTE_BASE + idx;
  MIDI.noteOn(nota,  127, MIDI_CHANNEL);
  MIDI.noteOff(nota, 0,   MIDI_CHANNEL);
}

// Gestiona el toggle de entorno al pulsar un botón:
//  - Si el entorno pulsado ya está activo → lo cierra (señal de cierre → neutro)
//  - Si hay otro entorno activo → cierra el anterior (señal cierre) + abre el nuevo (señal apertura)
//  - Si no hay ninguno activo → abre el nuevo (señal apertura)
void toggleEntorno(int boton_index) {
  int idx = calcularEntorno(boton_index);

  if (entorno_activo == idx) {
    // Cerrar el entorno activo
    enviarSeñalEntorno(idx);
    entorno_activo = -1;

  } else {
    // Si había otro entorno abierto, cerrarlo primero
    if (entorno_activo >= 0) {
      enviarSeñalEntorno(entorno_activo);
    }
    // Abrir el nuevo entorno
    enviarSeñalEntorno(idx);
    entorno_activo = idx;
  }

  pot1.resetEnvio();
  pot2.resetEnvio();
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
  // 1. Leer botones
  btn1.update();
  btn2.update();
  btn3.update();

  // 2. Cambio de banco (doble tap P1 ← | P3 →)
  //    Cambia de banco y resetea al canal neutro.
  bool bancoCambio = false;

  if (btn1.doubleTapDetected) {
    banco_actual = (banco_actual - 1 + NUM_BANCOS) % NUM_BANCOS;
    entorno_activo = -1;
    pot1.resetEnvio();
    pot2.resetEnvio();
    bancoCambio = true;
  }
  if (btn3.doubleTapDetected) {
    banco_actual = (banco_actual + 1) % NUM_BANCOS;
    entorno_activo = -1;
    pot1.resetEnvio();
    pot2.resetEnvio();
    bancoCambio = true;
  }
  if (bancoCambio) actualizarLeds();

  // 3. Calibración de máximo de potenciómetros (combinaciones con P2)
  //    P1 mantenido + P2 pulsado → calibra máximo de pot1
  //    P3 mantenido + P2 pulsado → calibra máximo de pot2
  //    Si se dispara la calibración, se bloquea el toggle de entorno de P2.
  bool calibracionRealizada = false;

  if (btn1.isPressed && btn2.justPressed && !btn2.doubleTapDetected) {
    pot1.calibrarMax();
    pot1.resetEnvio();
    animacionCalibracion();
    calibracionRealizada = true;
  } else if (btn3.isPressed && btn2.justPressed && !btn2.doubleTapDetected) {
    pot2.calibrarMax();
    pot2.resetEnvio();
    animacionCalibracion();
    calibracionRealizada = true;
  }

  // 4. Toggle de entorno (pulsación simple, sin doble tap ni calibración)
  if (!calibracionRealizada) {
    if (btn1.justPressed && !btn1.doubleTapDetected) toggleEntorno(0);
    if (btn2.justPressed && !btn2.doubleTapDetected) toggleEntorno(1);
    if (btn3.justPressed && !btn3.doubleTapDetected) toggleEntorno(2);
  }

  // 5. Actualizar pots con el CC del canal activo (neutro o entorno)
  int cc1, cc2;
  getCCsPots(cc1, cc2);
  pot1.update(cc1);
  pot2.update(cc2);

  delay(2);
}
