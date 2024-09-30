#include <Arduino.h>
#include <HomeSpan.h>
#include <HeatPump.h>
#include <config.h>
#include <map>

// Pairing Code: 466-37-726

HeatPump heatPump;

boolean isUpdating = false;
// nextUpdateTime tracks a timestamp for when the homekit update cycle should run
unsigned long nextUpdateTime = millis();
// nextPollTime tracks a timestamp for when the next heatpump settings poll should run
unsigned long nextPollTime = millis();
unsigned long nextTempPollTime = millis();

// Thermostat
/*
0 "Celsius"
1 "Fahrenheit"
2-255 "Reserved"
*/
SpanCharacteristic *temperatureDisplayUnits;

// value in centigrade
SpanCharacteristic *currentTemperature;
SpanCharacteristic *targetTemperature;

/*
0 "Off."
1 "Heat. The Heater is currently on."
2 "Cool. Cooler is currently on."
3-255 "Reserved"
*/
SpanCharacteristic *currentHeatingCoolingState;
/*
0 "Off"
1 "Heat. If the current temperature is below the target temperature then turn on heating."
2 "Cool. If the current temperature is above the target temperature then turn on cooling."
3 "Auto. Turn on heating or cooling to maintain temperature within the heating and cooling threshold of the target temperature."
4-255 "Reserved"
*/
SpanCharacteristic *targetHeatingCoolingState;
SpanCharacteristic *coolingThresholdTemperature;
SpanCharacteristic *heatingThresholdTemperature;

// Fan
SpanCharacteristic *fanActive;
SpanCharacteristic *fanRotationSpeed;

// Slat
/*
0 "Fixed"
1 "Jammed"
2 "Swinging"
*/
SpanCharacteristic *currentSlatState;
/*
0 "Horizontal"
1 "Vertical"
*/
SpanCharacteristic *slatType;
/*
0 "Swing disabled"
1 "Swing enabled"
*/
SpanCharacteristic *swingMode;
// tilt angle between -90 and 90 degrees
SpanCharacteristic *currentTiltAngle;
SpanCharacteristic *targetTiltAngle;

int getCurrentHeatingCoolingState(const String &powerSetting, const String &modeSetting) {
    if (powerSetting == "OFF") return 0;
    if (modeSetting == "HEAT") return 1;
    if (modeSetting == "COOL") return 2;
    return 0;
}

float getTargetTemperature() {
    return targetTemperature->getNewVal<float>();
}

int getTargetHeatingCoolingState(const String &powerSetting, const String &modeSetting) {
    if (powerSetting == "OFF") return 0;
    if (modeSetting == "HEAT") return 1;
    if (modeSetting == "COOL") return 2;
    // not sure how to implement auto, so just set to OFF
    if (modeSetting == "AUTO") return 0;
    return 0;
}

const char* getPowerSetting() {
    const int targetHeatingCoolingStateVal = targetHeatingCoolingState->getNewVal();

    if (targetHeatingCoolingStateVal == 0) return "OFF";
    return "ON";
}

const char* getModeSetting() {
    const int targetHeatingCoolingStateVal = targetHeatingCoolingState->getNewVal();

    if (targetHeatingCoolingStateVal == 1) return "HEAT";
    if (targetHeatingCoolingStateVal == 2) return "COOL";
    return "AUTO";
}

/**
 * Gets the current fan speed (for HP).
 *
 * @return HP fan speed
 */
const char* getFanSpeed() {
    const int fanRotationSpeedVal = fanRotationSpeed->getNewVal();

    if (fanRotationSpeedVal == 0) return "QUIET";
    if (fanRotationSpeedVal <= 1) return "AUTO";
    if (fanRotationSpeedVal <= 2) return "1";
    if (fanRotationSpeedVal <= 3) return "2";
    if (fanRotationSpeedVal <= 4) return "3";
    if (fanRotationSpeedVal <= 5) return "4";

    return "AUTO";
}

/**
 * Gets the fan speed percentage, based on the HP value.
 * @param fanSpeed
 * @return
 */
int getFanRotationSpeed(const String &fanSpeed) {
    if (fanSpeed == "QUIET") return 0;
    if (fanSpeed == "AUTO") return 1;
    if (fanSpeed == "1") return 2;
    if (fanSpeed == "2") return 3;
    if (fanSpeed == "3") return 4;
    if (fanSpeed == "4") return 5;

    return 0;
}

int getCurrentSlatState(const String &vaneSetting) {
    if (vaneSetting == "SWING") return 2;
    return 0;
}

int getSwingMode(const String &vaneSetting) {
    if (vaneSetting == "SWING") return 1;
    return 0;
}

const char* getVaneSetting() {
    const int swingModeVal = swingMode->getNewVal();
    const int targetTiltAngleVal = targetTiltAngle->getNewVal();

    if (swingModeVal == 1) return "SWING";
    if (targetTiltAngleVal < 0) return "AUTO";
    if (targetTiltAngleVal < 15) return "1";
    if (targetTiltAngleVal < 30) return "2";
    if (targetTiltAngleVal < 45) return "3";
    if (targetTiltAngleVal < 60) return "4";
    if (targetTiltAngleVal < 91) return "5";

    return "AUTO";
}

/**
 * Prints out current heatpump values.
 *
 * @param settings Heat pump settings object
 */
void printHPValues(const heatpumpSettings &settings) {
    LOG0("current heatpump settings:\n");
    LOG0("  power=" + String(settings.power) + "\n");
    LOG0("  mode=" + String(settings.mode) + "\n");
    LOG0("  target temperature=" + String(settings.temperature) + "\n");
    LOG0("  fan=" + String(settings.fan) + "\n");
    LOG0("  vane=" + String(settings.vane) + "\n");
}

/**
 * Prints out current homekit values.
 */
void printHKValues() {
    LOG0("HK Values:\n");
    LOG0("  currentTemperature=%f\n", currentTemperature->getVal<float>());
    LOG0("  targetTemperature=%f\n", targetTemperature->getVal<float>());
    LOG0("  currentHeatingCoolingState=%d\n", currentHeatingCoolingState->getVal());
    LOG0("  targetHeatingCoolingState=%d\n", targetHeatingCoolingState->getVal());
    LOG0("  fanRotationSpeed=%d\n", fanRotationSpeed->getVal());
    LOG0("  currentSlatState=%d\n", currentSlatState->getVal());
    LOG0("  targetTiltAngle=%d\n", targetTiltAngle->getVal());
    LOG0("  swingMode=%d\n", swingMode->getVal());
    LOG0("\n");
}

/**
 * Updates the homekit characteristics based on the new values read from the heat pump.
 * Use this method to read in changes that would have been made by a remote.
 *
 * @param settings Heat pump settings object
 */
void updateValues(const heatpumpSettings &settings) {
    targetTemperature->setVal(max(10.0f, settings.temperature));
    currentHeatingCoolingState->setVal(getCurrentHeatingCoolingState(settings.power, settings.mode));
    targetHeatingCoolingState->setVal(getTargetHeatingCoolingState(settings.power, settings.mode));

    fanRotationSpeed->setVal(getFanRotationSpeed(settings.fan));

    currentSlatState->setVal(getCurrentSlatState(settings.vane));
    swingMode->setVal(getSwingMode(settings.vane));
}

void delayHPPolling() {
    // delay next polling so it doesn't overwrite new settings
    nextPollTime = millis() + HP_POLL_DELAY;
}

void queueUpdate() {
    isUpdating = true;

    // sets the next update time into the future to provide a buffer for any additional state changes
    nextUpdateTime = millis() + HK_UPDATE_DEBOUNCE;

    delayHPPolling();
}

struct ThermostatController final : Service::Thermostat {
    ThermostatController() {
        temperatureDisplayUnits = new Characteristic::TemperatureDisplayUnits(1); // 1 = Fahrenheit
        currentTemperature = new Characteristic::CurrentTemperature();
        targetTemperature = new Characteristic::TargetTemperature();
        currentHeatingCoolingState = new Characteristic::CurrentHeatingCoolingState();
        targetHeatingCoolingState = new Characteristic::TargetHeatingCoolingState();
        targetHeatingCoolingState->setRange(0, 2);
        coolingThresholdTemperature = new Characteristic::CoolingThresholdTemperature();
        heatingThresholdTemperature = new Characteristic::HeatingThresholdTemperature();
    }

    boolean update() override {
        queueUpdate();

        return true;
    }

    /**
     * This loop handles the update logic for the thermostat and all accessories (fan and slat).
     */
    void loop() override {
        // if it is time to poll temperature
        if (nextTempPollTime < millis()) {
            // schedule next update time
            nextTempPollTime = millis() + HP_TEMP_POLL_DELAY;

            // read current state from heat pump
            heatPump.sync();

            // get current room temperature (this value is not part of settings)
            const float roomTemperature = heatPump.getRoomTemperature();
            LOG0("roomTemperature=%f\n", roomTemperature);

            if (roomTemperature > 0.0) {
                // save the current temperature
                currentTemperature->setVal(roomTemperature);
            }
        }

        // if update marked as in progress, and it is time to update
        if (isUpdating && nextUpdateTime < millis()) {
            // delay polling so there are no conflicts
            delayHPPolling();

            // read current state from heat pump
            heatPump.sync();
            // get heat pump settings
            heatpumpSettings settings = heatPump.getSettings();

            LOG0("-- start HK Update--\n");

            printHKValues();

            // start thermostat update
            settings.power = getPowerSetting();
            settings.mode = getModeSetting();
            settings.temperature = getTargetTemperature();
            // end thermostat update

            // start fan update

            settings.fan = getFanSpeed();
            fanRotationSpeed->setVal(getFanRotationSpeed(getFanSpeed()));
            // end fan update

            // start slat update
            settings.vane = getVaneSetting();
            // end slat update

            LOG0("new HP Settings:\n");
            printHPValues(settings);

            heatPump.setSettings(settings);
            heatPump.update();

            isUpdating = false;
            LOG0("-- end HK update --\n");
        }

        // if update not currently in progress, and it is time to poll
        if (!isUpdating && nextPollTime < millis()) {
            LOG0("-- start heatpump update--\n");
            delayHPPolling();

            // read current state from heat pump
            heatPump.sync();
            // get heat pump settings
            const heatpumpSettings settings = heatPump.getSettings();

            LOG0("updating HK values from heat pump\n");
            updateValues(settings);

            LOG0("read HP Settings:\n");
            printHPValues(settings);

            printHKValues();
            LOG0("-- end heatpump update--\n");
        }
    }
};

struct FanController final : Service::Fan {
    FanController() {
        fanActive = new Characteristic::Active();
        fanRotationSpeed = new Characteristic::RotationSpeed();
        fanRotationSpeed->setRange(0, 5);
    }

    boolean update() override {
        queueUpdate();

        return true;
    }

    void loop() override {
    }
};

struct SlatController final : Service::Slat {
    SlatController() {
        currentSlatState = new Characteristic::CurrentSlatState();
        slatType = new Characteristic::SlatType(1);
        swingMode = new Characteristic::SwingMode();
        currentTiltAngle = new Characteristic::CurrentTiltAngle();
        targetTiltAngle = new Characteristic::TargetTiltAngle();
    }

    boolean update() override {
        queueUpdate();

        return true;
    }
};

ThermostatController *thermostatController;
FanController *fanController;
SlatController *slatController;

TaskHandle_t h_HK_poll;
TaskHandle_t h_main_loop;

[[noreturn]] void HK_poll(void *pvParameters) {
    for (;;) {
        homeSpan.poll();
    } // loop
} // task

void setup() {
    Serial.begin(115200);

    homeSpan.setLogLevel(1);
    if (STATUS_PIN > 0) homeSpan.setStatusPin(STATUS_PIN);
    if (CONTROL_PIN > 0) homeSpan.setControlPin(CONTROL_PIN);
    homeSpan.begin(Category::Thermostats);

    new SpanAccessory();

    new Service::AccessoryInformation();
    new Characteristic::Name("Thermostat");
    new Characteristic::Manufacturer("HomeSpan");
    new Characteristic::SerialNumber("AK-0002");
    new Characteristic::Model("Thermostat");
    new Characteristic::FirmwareRevision("0.9");
    new Characteristic::Identify();

    new Service::HAPProtocolInformation();
    new Characteristic::Version("1.1.0");

    if (!heatPump.connect(&Serial2)) {
        Serial.printf("failed to connect to the heat pump");
    }
    heatPump.enableExternalUpdate();
    // heatPump.setSettings({ //set some default settings
    //   "ON",  /* ON/OFF */
    //   "FAN", /* HEAT/COOL/FAN/DRY/AUTO */
    //   26,    /* Between 16 and 31 */
    //   "4",   /* Fan speed: 1-4, AUTO, or QUIET */
    //   "3",   /* Air direction (vertical): 1-5, SWING, or AUTO */
    //   "|"    /* Air direction (horizontal): <<, <, |, >, >>, <>, or SWING */
    // });

    thermostatController = new ThermostatController();
    fanController = new FanController();
    slatController = new SlatController();

    xTaskCreatePinnedToCore(
        HK_poll, /* Task function. */
        "HK_poll", /* name of task. */
        10000, /* Stack size of task */
        nullptr, /* parameter of the task */
        1, /* priority of the task */
        &h_HK_poll, /* Task handle to keep track of created task */
        0); /* pin task to core 0 */

    delay(1000);
}

void loop() {
}
