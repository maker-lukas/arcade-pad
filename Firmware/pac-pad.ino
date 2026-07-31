#include <ESP32BLECombo.h>

constexpr uint8_t PIN_JOY_X = 0;
constexpr uint8_t PIN_JOY_Y = 1;

constexpr uint8_t BUTTON_COUNT = 10;
constexpr uint8_t BUTTON_PINS[BUTTON_COUNT] = {
    10, 8, 6,
    20, 9, 7,
    2, 3,
    21, 5,
};

constexpr unsigned long DEBOUNCE_MS = 20;
constexpr int ADC_MAX = 4095;
constexpr int JOYSTICK_DEADZONE = 200;
constexpr int16_t AXIS_CHANGE_THRESHOLD = 256;

ESP32BLECombo gamepad;
bool rawPressed[BUTTON_COUNT] = {};
bool stablePressed[BUTTON_COUNT] = {};
unsigned long lastChangeMs[BUTTON_COUNT] = {};
uint16_t buttonState = 0;
bool wasConnected = false;

int joyCenterX = 2048;
int joyCenterY = 2048;
int16_t currentAxisX = 0;
int16_t currentAxisY = 0;
int16_t sentAxisX = 0;
int16_t sentAxisY = 0;

int readAveragedAxis(uint8_t pin) {
    int total = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        total += analogRead(pin);
    }
    return total / 4;
}

int16_t scaleAxis(int raw, int center) {
    const int delta = raw - center;
    if (abs(delta) <= JOYSTICK_DEADZONE) {
        return 0;
    }

    if (delta > 0) {
        const int positiveRange = max(ADC_MAX - center - JOYSTICK_DEADZONE, 1);
        const int32_t value = (int32_t)(delta - JOYSTICK_DEADZONE) * 32767 / positiveRange;
        return (int16_t)constrain(value, 0L, 32767L);
    }

    const int negativeRange = max(center - JOYSTICK_DEADZONE, 1);
    const int32_t value = (int32_t)(-delta - JOYSTICK_DEADZONE) * 32767 / negativeRange;
    return (int16_t)-constrain(value, 0L, 32767L);
}

void calibrateJoystick() {
    int32_t sumX = 0;
    int32_t sumY = 0;

    Serial.println("keep the thumb stick centered");
    delay(300);
    for (uint16_t i = 0; i < 256; ++i) {
        sumX += analogRead(PIN_JOY_X);
        sumY += analogRead(PIN_JOY_Y);
        delay(2);
    }

    joyCenterX = sumX / 256;
    joyCenterY = sumY / 256;
    Serial.printf("joystick center: X=%d Y=%d\n", joyCenterX, joyCenterY);
}

void updateButtons(unsigned long now, bool connected) {
    bool stateChanged = false;

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        const bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;

        if (pressed != rawPressed[i]) {
            rawPressed[i] = pressed;
            lastChangeMs[i] = now;
        }

        if (pressed != stablePressed[i] &&
            now - lastChangeMs[i] >= DEBOUNCE_MS) {
            stablePressed[i] = pressed;
            if (pressed) {
                buttonState |= (1u << i);
            } else {
                buttonState &= ~(1u << i);
            }
            stateChanged = true;
        }
    }
    if (connected && wasConnected && stateChanged) {
        gamepad.gamepadSetButtons(buttonState);
    }
}

void updateJoystick(bool connected) {
    currentAxisX = -scaleAxis(readAveragedAxis(PIN_JOY_X), joyCenterX);
    currentAxisY = scaleAxis(readAveragedAxis(PIN_JOY_Y), joyCenterY);

    const bool returnedToCenter =
        (currentAxisX == 0 && sentAxisX != 0) ||
        (currentAxisY == 0 && sentAxisY != 0);
    const bool movedEnough =
        abs((int32_t)currentAxisX - sentAxisX) >= AXIS_CHANGE_THRESHOLD ||
        abs((int32_t)currentAxisY - sentAxisY) >= AXIS_CHANGE_THRESHOLD;

    if (connected && wasConnected && (returnedToCenter || movedEnough)) {
        gamepad.gamepadSetAxes(currentAxisX, currentAxisY);
        sentAxisX = currentAxisX;
        sentAxisY = currentAxisY;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("starting Pac Pad ");

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        rawPressed[i] = digitalRead(BUTTON_PINS[i]) == LOW;
        stablePressed[i] = rawPressed[i];
        lastChangeMs[i] = millis();
        if (stablePressed[i]) {
            buttonState |= (1u << i);
        }
    }

    analogReadResolution(12);
    calibrateJoystick();

    ESP32BLEComboConfig config;
    config.mode = ESP32BLEComboMode::GAMEPAD_ONLY;
    config.gamepadLayout = ESP32BLEGamepadLayout::GENERIC;
    config.deviceName = "Pac Pad";
    config.manufacturer = "Espressif";
    config.appearance = ESP32BLEComboAppearance::GAMEPAD;
    config.enableSecurity = true;

    if (!gamepad.begin(config)) {
        Serial.println("failed to start Pac Pad");
        return;
    }

    Serial.println("ready to pair as a game controller");
}

void loop() {
    const unsigned long now = millis();
    const bool connected = gamepad.isConnected();

    updateButtons(now, connected);
    updateJoystick(connected);

    if (connected && !wasConnected) {
        gamepad.gamepadSetButtons(buttonState);
        gamepad.gamepadSetAxes(currentAxisX, currentAxisY);
        gamepad.gamepadSetHat(ESP32BLECombo::HAT_CENTERED);
        sentAxisX = currentAxisX;
        sentAxisY = currentAxisY;
        Serial.println("gamepad connected");
    } else if (!connected && wasConnected) {
        Serial.println("gamepad disconnected");
    }

    wasConnected = connected;
    delay(5);
}