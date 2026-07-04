#include <Adafruit_NeoPixel.h>
#include <MIDIUSB.h>

// --- HARDWARE ---
#define PIN_LEDS      2      
#define NUM_LEDS      16     
#define PIN_MUX_S0    6      
#define PIN_MUX_S1    5
#define PIN_MUX_S2    4
#define PIN_MUX_S3    3
#define PIN_SIGNAL    A0     
#define PIN_SW_UP     7      
#define PIN_SW_DOWN   8      

// --- AJUSTES ---
#define PAD_THRESHOLD 500    
#define MAX_BANKS     4

// Configuración de Color Base (Naranja Cálido Oscuro)
// Ajusta esto para el brillo en reposo
const int BASE_R = 40;
const int BASE_G = 10;
const int BASE_B = 0;

// Intensidad del Flash (Cuánto brillo se añade al pulsar)
const int FLASH_AMOUNT = 120; 

// --- MAPEO DIRECTO: Pad Lógico -> LED Físico ---
const int padToLedMap[16] = {
  0,  8,  4,  12, 
  2,  10, 6,  14, 
  1,  9,  5,  13, 
  3,  11, 7,  15
};

Adafruit_NeoPixel strip(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

// Estado Global
int currentBank = 0;         
bool padState[NUM_LEDS];     
bool lastPadState[NUM_LEDS]; 
int midiVelocity = 127;      

// Variable del Flash Global
int flashValue = 0; // 0 a FLASH_AMOUNT
unsigned long lastFadeTime = 0;

// Menú
int lastSwUpState = LOW;
int lastSwDownState = LOW;
unsigned long lastDebounceTime = 0;
unsigned long bankChangeTime = 0;

void setup() {
  // Configuración de Pines lo más rápida posible
  pinMode(PIN_MUX_S0, OUTPUT); pinMode(PIN_MUX_S1, OUTPUT);
  pinMode(PIN_MUX_S2, OUTPUT); pinMode(PIN_MUX_S3, OUTPUT);
  pinMode(PIN_SW_UP, INPUT);   pinMode(PIN_SW_DOWN, INPUT);

  strip.begin();
  strip.setBrightness(127); // Brillo global del hardware (0-255)
  
  // Pintar estado inicial
  updateLEDs(); 
}

void loop() {
  // 1. GESTIÓN DEL FLASH (Desvanecimiento)
  // Lo hacemos al principio para calcular el nivel de brillo actual
  handleFlashFade();

  // 2. LEER BOTONES MENÚ
  readMenuButtons();

  // 3. LEER PADS (El núcleo del programa)
  readPads(); 
}

// --- LÓGICA DE PADS (OPTIMIZADA) ---
void readPads() {
  bool needsUpdate = false; // Solo actualizamos LEDs si algo cambió

  for (int i = 0; i < NUM_LEDS; i++) {
    // Selección Mux Rápida
    digitalWrite(PIN_MUX_S0, (i & 1));
    digitalWrite(PIN_MUX_S1, (i & 2));
    digitalWrite(PIN_MUX_S2, (i & 4));
    digitalWrite(PIN_MUX_S3, (i & 8));
    delayMicroseconds(5); // Espera mínima para señal estable
    
    int val = analogRead(PIN_SIGNAL);
    bool isPressed = (val < PAD_THRESHOLD);
    
    // Si detectamos un cambio de estado en el botón
    if (isPressed != lastPadState[i]) {
      padState[i] = isPressed;
      
      if (isPressed) {
        // --- NOTE ON ---
        sendMIDI(0x90, getNoteNumber(i), midiVelocity);
        
        // DISPARAR FLASH INSTANTÁNEO
        // Al pulsar, subimos el brillo global al máximo inmediatamente
        flashValue = FLASH_AMOUNT; 
        
      } else {
        // --- NOTE OFF ---
        sendMIDI(0x80, getNoteNumber(i), 0);
      }
      
      // Marcar que necesitamos repintar la tira de LEDs YA
      needsUpdate = true;
    }
    lastPadState[i] = isPressed;
  }

  // Si hubo cualquier actividad (Pulsar o Soltar), actualizamos visuales INMEDIATAMENTE
  // Esto garantiza latencia visual cercana a 0
  if (needsUpdate) {
    updateLEDs();
    MidiUSB.flush();
  } else {
    // Si no hubo cambios visuales, solo aseguramos que salga el MIDI si quedó algo pendiente
    MidiUSB.flush();
  }
}

// --- GESTIÓN DEL FADE (Suavizado) ---
void handleFlashFade() {
  // Reducir el flash cada 5ms para un fade suave pero rápido
  if (flashValue > 0 && (millis() - lastFadeTime > 5)) {
    flashValue -= 4; // Velocidad del fade (ajustar este 4 para más lento/rápido)
    if (flashValue < 0) flashValue = 0;
    
    lastFadeTime = millis();
    updateLEDs(); // Actualizar visualmente el desvanecimiento
  }
}

// --- PINTAR LEDS (LA PARTE VISUAL) ---
void updateLEDs() {
  for(int i=0; i<NUM_LEDS; i++) {
    int physLed = padToLedMap[i];
    int r, g, b;

    // A. Si es el Pad Pulsado -> ROJO PURO
    if (padState[i]) {
      r = 255;
      g = 0;
      b = 0;
    } 
    // B. Si es el resto -> NARANJA BASE + FLASH
    else {
      // Calculamos el color sumando el valor del flash.
      // Para mantener el naranja, sumamos mucho al Rojo y poco al Verde.
      // Base: (40, 10, 0) -> Flash Max: (160, 40, 0)
      
      r = BASE_R + flashValue; 
      g = BASE_G + (flashValue / 4); // Dividimos por 4 para que no se ponga amarillo/blanco
      b = BASE_B; // El azul se queda en 0 para mantener la calidez
      
      // Limitar a 255 por seguridad
      if (r > 255) r = 255;
      if (g > 255) g = 255;
    }

    // C. Indicador de Banco (Sobrescribe en Blanco)
    if (millis() - bankChangeTime < 500) {
       int targetLed = -1;
       if (currentBank == 0) targetLed = 0;
       if (currentBank == 1) targetLed = 3;
       if (currentBank == 2) targetLed = 12;
       if (currentBank == 3) targetLed = 15;
       
       if (physLed == targetLed) {
         r = 255; g = 255; b = 255;
       }
    }

    strip.setPixelColor(physLed, r, g, b);
  }
  strip.show();
}

// --- BOTONES MENÚ ---
void readMenuButtons() {
  // Lectura rápida sin delays bloqueantes
  if (millis() - lastDebounceTime < 100) return;

  int rUp = digitalRead(PIN_SW_UP);
  int rDown = digitalRead(PIN_SW_DOWN);

  if (rUp == HIGH && lastSwUpState == LOW) {
     if (currentBank < MAX_BANKS - 1) {
       currentBank++;
       bankChangeTime = millis();
       updateLEDs(); // Feedback visual inmediato al cambiar banco
     }
     lastDebounceTime = millis();
  }
  
  if (rDown == HIGH && lastSwDownState == LOW) {
     if (currentBank > 0) {
       currentBank--;
       bankChangeTime = millis();
       updateLEDs(); // Feedback visual inmediato
     }
     lastDebounceTime = millis();
  }
  lastSwUpState = rUp;
  lastSwDownState = rDown;
}

// --- MIDI ---
byte getNoteNumber(int padIndex) {
  return 36 + (currentBank * 16) + padIndex;
}

void sendMIDI(byte command, byte note, byte velocity) {
  midiEventPacket_t event = {0x09, command, note, velocity};
  if (command == 0x80) event.header = 0x08;
  MidiUSB.sendMIDI(event);
}