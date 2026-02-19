#include "USB.h"
#include "USBMIDI.h"

USBMIDI MIDI;

// ==========================================================
// CONFIGURACIÓN DE HARDWARE (NUEVOS PINES SEGUROS)
// ==========================================================
// Pulsadores (Conectar a GND y al Pin)
const int PIN_P1 = 4;  // Retroceso / Shift A
const int PIN_P2 = 5;  // Acción
const int PIN_P3 = 6;  // Avance / Shift B

// LEDs Indicadores (Binario)
const int PIN_LED[4] = {38, 39, 40, 41}; 

// Entradas Analógicas (Instrumentos)
const int PIN_J1 = 9;  // Potenciómetro 1
const int PIN_J2 = 8;  // Potenciómetro 2 (Asumido en GPIO 8)

// ==========================================================
// CALIBRACIÓN Y AJUSTES
// ==========================================================
// Calibración J1 (Basada en tus tests: 3.3V=Min, 0.97V=Max)
// Invertido: Valor RAW alto = MIDI 0. Valor RAW bajo = MIDI 127.
const int J1_RAW_MIN_VOLT = 4050; // Cuando el pot está al mínimo (3.3V)
const int J1_RAW_MAX_VOLT = 1150; // Cuando el pot está al máximo (0.97V)

// Calibración J2 (Asumimos la misma si usas el mismo circuito)
const int J2_RAW_MIN_VOLT = 4050; 
const int J2_RAW_MAX_VOLT = 1150;

// Tiempos
const unsigned long DEBOUNCE_TIME = 40;    // Filtro anti-rebote botones
const unsigned long DOUBLE_TAP_TIME = 350; // Tiempo para hacer doble click
const float EMA_ALPHA = 0.15;              // Suavizado pots (0.1 = suave, 0.5 = rápido)

// Identidad USB
const char* NOMBRE_DISPOSITIVO = "AgroPedal";
const char* FABRICANTE = "Sagarrabanana";

// ==========================================================
// VARIABLES GLOBALES
// ==========================================================
int banco_actual = 0; // 0 a 15

// CLASE PARA GESTIÓN DE BOTONES (Con doble click y hold)
class Boton {
  private:
    int pin;
    bool estadoFisicoAnterior = HIGH; // INPUT_PULLUP: HIGH = Soltado
    unsigned long ultimoCambio = 0;
    
    // Logica doble tap
    int clicks = 0;
    unsigned long tiempoPrimerClick = 0;

  public:
    bool isPressed = false;      // Estado estable (para Hold/Shift)
    bool justPressed = false;    // Flanco de bajada (un solo toque)
    bool doubleTapDetected = false; // Evento doble click

    Boton(int p) : pin(p) {}

    void begin() {
      pinMode(pin, INPUT_PULLUP);
    }

    void update() {
      bool lectura = digitalRead(pin);
      unsigned long ahora = millis();
      
      justPressed = false;
      doubleTapDetected = false;

      // 1. Debouncing Hardware
      if (lectura != estadoFisicoAnterior) {
        ultimoCambio = ahora;
      }

      if ((ahora - ultimoCambio) > DEBOUNCE_TIME) {
        // El estado se ha estabilizado
        if (lectura != !isPressed) { // Nota: !isPressed porque LOW es presionado
          isPressed = !lectura; // Actualizamos estado lógico (True = Pulsado)

          if (isPressed) { 
            // --- AL PULSAR ---
            justPressed = true;
            
            // Lógica Doble Click
            if (clicks == 0) {
              clicks = 1;
              tiempoPrimerClick = ahora;
            } else {
              // Si ya teníamos un click y pulsamos otra vez...
              if (ahora - tiempoPrimerClick <= DOUBLE_TAP_TIME) {
                doubleTapDetected = true;
                clicks = 0; // Reiniciar
              } else {
                // Pasó demasiado tiempo, esto cuenta como un nuevo primer click
                clicks = 1;
                tiempoPrimerClick = ahora;
              }
            }
          }
        }
      }
      
      // Reset de doble click por tiempo expirado
      if (clicks == 1 && (ahora - tiempoPrimerClick > DOUBLE_TAP_TIME)) {
        clicks = 0;
      }
      
      estadoFisicoAnterior = lectura;
    }
};

// CLASE PARA POTENCIÓMETROS
class Potenciometro {
  private:
    int pin;
    float emaValue = 0;
    int lastMidiValue = -1;
    int ccBase;
    int rawMin, rawMax;

  public:
    Potenciometro(int p, int cc, int minR, int maxR) 
      : pin(p), ccBase(cc), rawMin(minR), rawMax(maxR) {}

    void begin() {
      pinMode(pin, INPUT);
      emaValue = analogRead(pin);
    }

    void update(int modificador) {
      int raw = analogRead(pin);
      
      // Filtro Suavizado
      emaValue = (EMA_ALPHA * raw) + ((1.0 - EMA_ALPHA) * emaValue);
      
      // Mapeo (Gestiona la inversión automáticamente según rawMin/rawMax)
      int midiVal = map((int)emaValue, rawMin, rawMax, 0, 127);
      midiVal = constrain(midiVal, 0, 127); // Limites de seguridad

      // Enviar solo si cambia
      if (midiVal != lastMidiValue) {
        lastMidiValue = midiVal;
        
        // Cálculo CC: Base + (Banco * 5) + Modificador
        // Ejemplo J1: Base 10 + (Banco 1 * 5) + Shift 1 = CC 16
        int finalCC = ccBase + (banco_actual * 5) + modificador;
        if(finalCC > 127) finalCC = 127;

        MIDI.controlChange(finalCC, midiVal, 1);
      }
    }
};

// --- INSTANCIAS ---
Boton btn1(PIN_P1);
Boton btn2(PIN_P2);
Boton btn3(PIN_P3);

// J1 empieza en CC 10. J2 empieza en CC 20 (para dejar espacio a los modificadores)
// Nota: Banco * 5 significa que cada banco ocupa 5 CCs.
// Banco 0: J1 usa CC 10,11,12. J2 usa CC 20,21,22.
// Banco 1: J1 usa CC 15,16,17. J2 usa CC 25,26,27. 
// (Asegurate de que no se solapen si J2 estuviera muy cerca de J1, aqui hay margen).
Potenciometro pot1(PIN_J1, 10, J1_RAW_MIN_VOLT, J1_RAW_MAX_VOLT);
Potenciometro pot2(PIN_J2, 20, J2_RAW_MIN_VOLT, J2_RAW_MAX_VOLT);

// --- FUNCIONES AUXILIARES ---
void actualizarLeds() {
  for (int i = 0; i < 4; i++) {
    // Extraer bit: (banco >> i) & 1
    int estado = (banco_actual >> i) & 1;
    digitalWrite(PIN_LED[i], estado);
  }
}

void animacionArranque() {
  // Efecto visual rápido para confirmar encendido
  for(int i = 0; i < 4; i++) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  for(int i = 2; i >= 0; i--) { digitalWrite(PIN_LED[i], HIGH); delay(80); digitalWrite(PIN_LED[i], LOW); }
  
  // Parpadeo final todos juntos
  for(int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], HIGH);
  delay(200);
  for(int i = 0; i < 4; i++) digitalWrite(PIN_LED[i], LOW);
  delay(100);
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {
  // Config USB
  USB.productName(NOMBRE_DISPOSITIVO);
  USB.manufacturerName(FABRICANTE);
  USB.VID(0x303A);
  USB.PID(0x80AD);
  USB.begin();
  MIDI.begin();

  // Config Pines
  btn1.begin();
  btn2.begin();
  btn3.begin();
  pot1.begin();
  pot2.begin();
  
  for(int i=0; i<4; i++) {
    pinMode(PIN_LED[i], OUTPUT);
    digitalWrite(PIN_LED[i], LOW);
  }

  // Animación y estado inicial
  animacionArranque();
  actualizarLeds();
}

// ==========================================================
// LOOP PRINCIPAL
// ==========================================================
void loop() {
  // 1. Leer Botones
  btn1.update();
  btn2.update();
  btn3.update();

  // 2. Gestión de Bancos (Doble Click)
  bool bancoCambio = false;
  
  if (btn3.doubleTapDetected) {
    banco_actual++;
    if (banco_actual > 15) banco_actual = 0;
    bancoCambio = true;
  }
  
  if (btn1.doubleTapDetected) {
    banco_actual--;
    if (banco_actual < 0) banco_actual = 15;
    bancoCambio = true;
  }
  
  if (bancoCambio) actualizarLeds();

  // 3. Determinar Modificador (Shift)
  // P1 pulsado = Shift 1 | P3 pulsado = Shift 2
  int modificador = 0;
  if (btn1.isPressed) modificador = 1;
  else if (btn3.isPressed) modificador = 2;

  // 4. Leer Pots y Enviar MIDI
  pot1.update(modificador);
  
  // Descomenta la siguiente linea cuando conectes J2
  // pot2.update(modificador); 

  // 5. Botón Acción P2
  // Envía un CC momentáneo (127 al pulsar, 0 al soltar)
  // CC Base 30 + (Banco*5) + Modificador
  static bool p2_lastState = false;
  if (btn2.isPressed != p2_lastState) {
    p2_lastState = btn2.isPressed;
    
    int p2_CC = 30 + (banco_actual * 5) + modificador;
    int velocity = btn2.isPressed ? 127 : 0;
    
    MIDI.controlChange(p2_CC, velocity, 1);
  }

  // Pequeña pausa para evitar saturar la CPU
  delay(2);
}
