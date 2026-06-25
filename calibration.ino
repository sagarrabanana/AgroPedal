// ==========================================================
// AGROPEDAL - Script de Calibración de Potenciómetros
// Subir este sketch para calibrar, luego volver al firmware
// ==========================================================

const int PIN_J1 = 9;
const int PIN_J2 = 8;
const unsigned long VENTANA_MS = 5000; // Reseteo de ventana cada 5s

// Valores de ventana actual
int j1_min_v, j1_max_v;
int j2_min_v, j2_max_v;

// Valores absolutos de toda la sesión
int j1_min_abs, j1_max_abs;
int j2_min_abs, j2_max_abs;

unsigned long ultimoReset = 0;
int ventana = 1;

void resetVentana() {
  int j1_actual = analogRead(PIN_J1);
  int j2_actual = analogRead(PIN_J2);
  j1_min_v = j1_max_v = j1_actual;
  j2_min_v = j2_max_v = j2_actual;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_J1, INPUT);
  pinMode(PIN_J2, INPUT);

  // Inicializar absolutos con primera lectura
  j1_min_abs = j1_max_abs = analogRead(PIN_J1);
  j2_min_abs = j2_max_abs = analogRead(PIN_J2);

  resetVentana();
  ultimoReset = millis();

  Serial.println("==============================================");
  Serial.println("  AGROPEDAL - Calibración de Potenciómetros  ");
  Serial.println("==============================================");
  Serial.println("Mueve ambos potenciómetros de extremo a extremo.");
  Serial.println("Cada 5s se muestra el resumen de la ventana.");
  Serial.println("Los valores ABSOLUTOS son los que hardcodearás.");
  Serial.println("----------------------------------------------");
}

void loop() {
  int j1 = analogRead(PIN_J1);
  int j2 = analogRead(PIN_J2);

  // Actualizar ventana actual
  j1_min_v = min(j1_min_v, j1);
  j1_max_v = max(j1_max_v, j1);
  j2_min_v = min(j2_min_v, j2);
  j2_max_v = max(j2_max_v, j2);

  // Actualizar absolutos
  j1_min_abs = min(j1_min_abs, j1);
  j1_max_abs = max(j1_max_abs, j1);
  j2_min_abs = min(j2_min_abs, j2);
  j2_max_abs = max(j2_max_abs, j2);

  // Cada 5 segundos mostrar resumen
  if (millis() - ultimoReset >= VENTANA_MS) {
    Serial.print("\n--- Ventana ");
    Serial.print(ventana);
    Serial.println(" (últimos 5s) ---");

    Serial.print("  J1: MIN="); Serial.print(j1_min_v);
    Serial.print("  MAX=");     Serial.println(j1_max_v);
    Serial.print("  J2: MIN="); Serial.print(j2_min_v);
    Serial.print("  MAX=");     Serial.println(j2_max_v);

    Serial.println("  >> ABSOLUTOS (toda la sesión) <<");
    Serial.print("  J1: MIN="); Serial.print(j1_min_abs);
    Serial.print("  MAX=");     Serial.println(j1_max_abs);
    Serial.print("  J2: MIN="); Serial.print(j2_min_abs);
    Serial.print("  MAX=");     Serial.println(j2_max_abs);

    Serial.println("\n  Para hardcodear en el firmware:");
    Serial.print("  const int J1_RAW_MIN = "); Serial.print(j1_min_abs); Serial.println(";");
    Serial.print("  const int J1_RAW_MAX = "); Serial.print(j1_max_abs); Serial.println(";");
    Serial.print("  const int J2_RAW_MIN = "); Serial.print(j2_min_abs); Serial.println(";");
    Serial.print("  const int J2_RAW_MAX = "); Serial.print(j2_max_abs); Serial.println(";");
    Serial.println("----------------------------------------------");

    resetVentana();
    ultimoReset = millis();
    ventana++;
  }

  delay(10);
}