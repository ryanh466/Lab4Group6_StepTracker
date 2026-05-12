
// ── Pin definitions ──────────────────────────────────────────
const int LED_PINS[4]  = {3, 5, 6, 9};   // LED0–LED3
const int SW_PINS[4]   = {7, 8, 10, 11}; // SW0–SW3 (tactile)
const int POWER_SW_PIN = 2;               // soft-power sense pin

// ── Timing constants ─────────────────────────────────────────
const unsigned long DEBOUNCE_MS   = 50;   // button debounce window
const unsigned long BLINK_FAST_MS = 100;  // fast blink period (speed mode)
const unsigned long BLINK_SLOW_MS = 500;  // slow blink period (idle)
const unsigned long STEP_TIMEOUT  = 2000; // ms before step cadence resets

// ── State ────────────────────────────────────────────────────
enum Mode { MODE_IDLE, MODE_STEPS, MODE_SPEED, MODE_LAP, MODE_TEST };
Mode     currentMode     = MODE_IDLE;

// Step counter
unsigned long stepCount       = 0;
unsigned long lastStepTime    = 0;
unsigned long stepIntervalMs  = 0;   // time between last two steps

// Lap / interval timer
unsigned long lapStartMs      = 0;
unsigned long lapCount        = 0;

// Button state tracking (debounce)
bool     swState[4]       = {HIGH, HIGH, HIGH, HIGH}; // HIGH = not pressed (pullup)
bool     swLastRaw[4]     = {HIGH, HIGH, HIGH, HIGH};
unsigned long swDebounceAt[4] = {0, 0, 0, 0};

// LED blink helpers
unsigned long lastBlinkMs   = 0;
bool          blinkState     = false;

// Power state
bool deviceOn = true;

// ── Forward declarations ──────────────────────────────────────
void handleButtons();
bool buttonPressed(int idx);
void updateLEDs();
void showStepCount();
void showSpeed();
void showLap();
void runLEDTest();
void setAllLEDs(bool on);
void setLED(int idx, bool on);
void blinkAll(int times, int delayMs);
void idleAnimation();

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  // LED pins as outputs
  for (int i = 0; i < 4; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Button pins as inputs with internal pull-up
  for (int i = 0; i < 4; i++) {
    pinMode(SW_PINS[i], INPUT_PULLUP);
  }

  // Power switch pin
  pinMode(POWER_SW_PIN, INPUT_PULLUP);

  // Startup animation: sweep LEDs on then off
  for (int i = 0; i < 4; i++) { setLED(i, true);  delay(120); }
  for (int i = 3; i >= 0; i--){ setLED(i, false); delay(120); }

  Serial.println(F("=== Ankle Tracker Ready ==="));
  Serial.println(F("SW0: Step counter  SW1: Speed  SW2: Lap  SW3: LED test"));
}

// ─────────────────────────────────────────────────────────────
void loop() {
  // ── Soft power check ──
  // If power switch is open (HIGH via pullup) we sleep everything
  if (digitalRead(POWER_SW_PIN) == HIGH) {
    if (deviceOn) {
      setAllLEDs(false);
      Serial.println(F("Power off."));
      deviceOn = false;
    }
    delay(100);
    return; // do nothing while powered off
  }

  if (!deviceOn) {
    deviceOn = true;
    Serial.println(F("Power on."));
    blinkAll(2, 150);
  }

  // ── Read buttons ──
  handleButtons();

  // ── Mode actions ──
  switch (currentMode) {
    case MODE_IDLE:  idleAnimation();  break;
    case MODE_STEPS: updateLEDs();     break;
    case MODE_SPEED: showSpeed();      break;
    case MODE_LAP:   showLap();        break;
    case MODE_TEST:  runLEDTest();     break;
  }
}

// ─────────────────────────────────────────────────────────────
// Button handling with debounce
// ─────────────────────────────────────────────────────────────
void handleButtons() {
  unsigned long now = millis();

  for (int i = 0; i < 4; i++) {
    bool raw = digitalRead(SW_PINS[i]);

    // Start debounce timer on raw change
    if (raw != swLastRaw[i]) {
      swDebounceAt[i] = now;
      swLastRaw[i]    = raw;
    }

    // Stable after debounce window?
    if ((now - swDebounceAt[i]) >= DEBOUNCE_MS) {
      if (raw != swState[i]) {
        swState[i] = raw;

        // Trigger on press (LOW = pressed, because INPUT_PULLUP)
        if (raw == LOW) {
          onButtonPress(i);
        }
      }
    }
  }
}

void onButtonPress(int idx) {
  Serial.print(F("Button SW"));
  Serial.print(idx);
  Serial.println(F(" pressed"));

  switch (idx) {

    // ── SW0: Step counter ──────────────────────────────────
    case 0:
      if (currentMode != MODE_STEPS) {
        currentMode = MODE_STEPS;
        stepCount   = 0;
        Serial.println(F("Mode: STEP COUNTER (press again to count steps)"));
        blinkAll(1, 200);
      } else {
        // Each press = one step
        unsigned long now = millis();
        if (lastStepTime > 0) {
          stepIntervalMs = now - lastStepTime;
        }
        lastStepTime = now;
        stepCount++;
        Serial.print(F("Steps: "));
        Serial.println(stepCount);
        updateLEDs(); // immediately refresh LED bar
      }
      break;

    // ── SW1: Speed indicator ───────────────────────────────
    case 1:
      if (currentMode != MODE_SPEED) {
        currentMode    = MODE_SPEED;
        stepIntervalMs = 0;
        lastStepTime   = 0;
        Serial.println(F("Mode: SPEED — tap SW1 rhythmically to show cadence"));
        blinkAll(1, 200);
      } else {
        // Tap to measure cadence (steps/min shown via LED count)
        unsigned long now = millis();
        if (lastStepTime > 0) {
          stepIntervalMs = now - lastStepTime;
        }
        lastStepTime = now;
        stepCount++;
        Serial.print(F("Cadence interval ms: "));
        Serial.println(stepIntervalMs);
      }
      break;

    // ── SW2: Lap / interval timer ──────────────────────────
    case 2:
      if (currentMode != MODE_LAP) {
        currentMode  = MODE_LAP;
        lapStartMs   = millis();
        lapCount     = 0;
        Serial.println(F("Mode: LAP TIMER — press SW2 to log each lap"));
        blinkAll(1, 200);
      } else {
        unsigned long elapsed = millis() - lapStartMs;
        lapCount++;
        lapStartMs = millis(); // reset for next lap
        Serial.print(F("Lap "));
        Serial.print(lapCount);
        Serial.print(F(": "));
        Serial.print(elapsed / 1000.0, 2);
        Serial.println(F(" sec"));
        // Flash all LEDs to confirm lap logged
        blinkAll(1, 80);
      }
      break;

    // ── SW3: LED test / all blink ──────────────────────────
    case 3:
      currentMode = MODE_TEST;
      Serial.println(F("Mode: LED TEST"));
      break;
  }
}

// ─────────────────────────────────────────────────────────────
// MODE_STEPS — bar graph: 0-24 steps = 1 LED, 25-49 = 2, etc.
// ─────────────────────────────────────────────────────────────
void updateLEDs() {
  // Every 25 steps lights one more LED (max 4 LEDs = 100+ steps)
  int lit = min((int)(stepCount / 25), 4);
  for (int i = 0; i < 4; i++) {
    setLED(i, i < lit);
  }
}

// ─────────────────────────────────────────────────────────────
// MODE_SPEED — blink rate shows cadence, LED count shows speed zone
//   > 120 spm (interval < 500ms)  = fast  → 4 LEDs blink fast
//   90-120 spm                    = jog   → 3 LEDs blink medium
//   60-90  spm                    = walk  → 2 LEDs blink slow
//   < 60   spm                    = slow  → 1 LED blink slow
// ─────────────────────────────────────────────────────────────
void showSpeed() {
  unsigned long now = millis();

  // Reset if no tap for STEP_TIMEOUT
  if (lastStepTime > 0 && (now - lastStepTime) > STEP_TIMEOUT) {
    stepIntervalMs = 0;
    setAllLEDs(false);
    return;
  }

  if (stepIntervalMs == 0) {
    // No data yet — slow pulse LED0 only
    if (now - lastBlinkMs >= BLINK_SLOW_MS) {
      lastBlinkMs = now;
      blinkState  = !blinkState;
      setLED(0, blinkState);
      for (int i = 1; i < 4; i++) setLED(i, false);
    }
    return;
  }

  // Steps per minute
  float spm = 60000.0 / stepIntervalMs;
  int   lit;
  unsigned long blinkPeriod;

  if      (spm >= 120) { lit = 4; blinkPeriod = BLINK_FAST_MS; }
  else if (spm >= 90)  { lit = 3; blinkPeriod = 200; }
  else if (spm >= 60)  { lit = 2; blinkPeriod = 350; }
  else                 { lit = 1; blinkPeriod = BLINK_SLOW_MS; }

  if (now - lastBlinkMs >= blinkPeriod) {
    lastBlinkMs = now;
    blinkState  = !blinkState;
    for (int i = 0; i < 4; i++) setLED(i, blinkState && (i < lit));
  }
}

// ─────────────────────────────────────────────────────────────
// MODE_LAP — LEDs count laps (1 LED per lap, wraps at 4)
// ─────────────────────────────────────────────────────────────
void showLap() {
  int lit = lapCount % 5; // 0-4
  for (int i = 0; i < 4; i++) setLED(i, i < lit);
}

// ─────────────────────────────────────────────────────────────
// MODE_TEST — chase pattern then all on/off
// ─────────────────────────────────────────────────────────────
void runLEDTest() {
  Serial.println(F("LED test: chase..."));
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < 4; i++) {
      setAllLEDs(false);
      setLED(i, true);
      delay(150);
    }
  }
  blinkAll(3, 200);
  setAllLEDs(false);
  currentMode = MODE_IDLE;
  Serial.println(F("LED test done. Back to IDLE."));
}

// ─────────────────────────────────────────────────────────────
// MODE_IDLE — slow breathing pulse on LED0 only
// ─────────────────────────────────────────────────────────────
void idleAnimation() {
  unsigned long now = millis();
  if (now - lastBlinkMs >= BLINK_SLOW_MS) {
    lastBlinkMs = now;
    blinkState  = !blinkState;
    setLED(0, blinkState);
    for (int i = 1; i < 4; i++) setLED(i, false);
  }
}

// ─────────────────────────────────────────────────────────────
// LED helpers
// ─────────────────────────────────────────────────────────────
void setLED(int idx, bool on) {
  if (idx < 0 || idx > 3) return;
  digitalWrite(LED_PINS[idx], on ? HIGH : LOW);
}

void setAllLEDs(bool on) {
  for (int i = 0; i < 4; i++) setLED(i, on);
}

void blinkAll(int times, int periodMs) {
  for (int t = 0; t < times; t++) {
    setAllLEDs(true);  delay(periodMs);
    setAllLEDs(false); delay(periodMs);
  }
}
