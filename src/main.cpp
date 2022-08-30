#include <Arduino.h>
#include <HomeSpan.h>
#include <HeatPump.h>
#include <config.h>

// Pairing Code: 466-37-726

HeatPump heatPump;

boolean isUpdating = false;
unsigned long nextUpdateTime = millis();
unsigned long nextPollTime = millis();

// Thermostat
/*
0 ”Celsius”
1 ”Fahrenheit”
2-255 ”Reserved”
*/
SpanCharacteristic *temperatureDisplayUnits;

// value in centigrade
SpanCharacteristic *currentTemperature;
SpanCharacteristic *targetTemperature;

/*
0 ”Off.”
1 ”Heat. The Heater is currently on.”
2 ”Cool. Cooler is currently on.”
3-255 ”Reserved”
*/
SpanCharacteristic *currentHeatingCoolingState;
/*
0 ”Off”
1 ”Heat. If the current temperature is below the target temperature then turn on heating.”
2 ”Cool. If the current temperature is above the target temperature then turn on cooling.”
3 ”Auto. Turn on heating or cooling to maintain temperature within the heating and cooling threshold of the target temperature.”
4-255 ”Reserved”
*/
SpanCharacteristic *targetHeatingCoolingState;
SpanCharacteristic *coolingThresholdTemperature;
SpanCharacteristic *heatingThresholdTemperature;

// Fan
SpanCharacteristic *fanActive;
SpanCharacteristic *fanRotationSpeed;

// Slat
/*
0 ”Fixed”
1 ”Jammed”
2 ”Swinging”
*/
SpanCharacteristic *currentSlatState;
/*
0 ”Horizontal”
1 ”Vertical”
*/
SpanCharacteristic *slatType;
/*
0 ”Swing disabled”
1 ”Swing enabled”
*/
SpanCharacteristic *swingMode;
// tilt angle between -90 and 90 degrees
SpanCharacteristic *currentTiltAngle;
SpanCharacteristic *targetTiltAngle;

int getCurrentHeatingCoolingState(String powerSetting, String modeSetting) {
    if (powerSetting == "OFF") return 0;
    if (modeSetting == "HEAT") return 1;
    if (modeSetting == "COOL") return 2;
    return 0;
}

int getTargetHeatingCoolingState(String powerSetting, String modeSetting) {
    if (powerSetting == "OFF") return 0;
    if (modeSetting == "HEAT") return 1;
    if (modeSetting == "COOL") return 2;
    if (modeSetting == "AUTO") return 3;
    return 0;
}

String getPowerSetting(int heatingCoolingState) {
    if (heatingCoolingState == 0) return "OFF";
    else return "ON";
}

String getModeSetting(int heatingCoolingState) {
    if (heatingCoolingState == 1) return "HEAT";
    if (heatingCoolingState == 2) return "COOL";
    return "AUTO";
}

String getFanSpeed(int fanRotationSpeed) {
    if (fanRotationSpeed < 100.0 * 1.0 / 6.0) return "AUTO";
    if (fanRotationSpeed < 100.0 * 2.0 / 6.0) return "QUIET";
    if (fanRotationSpeed < 100.0 * 3.0 / 6.0) return "1";
    if (fanRotationSpeed < 100.0 * 4.0 / 6.0) return "2";
    if (fanRotationSpeed < 100.0 * 5.0 / 6.0) return "3";
    if (fanRotationSpeed <= 100.0 * 6.0 / 6.0) return "4";

    return "AUTO";
}

int getFanRotationSpeed(String fanSpeed) {
    if (fanSpeed == "AUTO") return 100.0 * 1.0 / 6.0;
    if (fanSpeed == "QUIET") return 100.0 * 2.0 / 6.0;
    if (fanSpeed == "1") return 100.0 * 3.0 / 6.0;
    if (fanSpeed == "2") return 100.0 * 4.0 / 6.0;
    if (fanSpeed == "3") return 100.0 * 5.0 / 6.0;
    if (fanSpeed == "4") return 100.0 * 6.0 / 6.0;

    return 0;
}

int getCurrentSlatState(String vaneSetting) {
    if (vaneSetting == "SWING") return 2;
    return 0;
}

int getSwingMode(String vaneSetting) {
    if (vaneSetting == "SWING") return 1;
    return 0;
}

String getVaneSetting(int swingMode, int targetTiltAngle) {
    if (swingMode == 1) return "SWING";
    if (targetTiltAngle < 0) return "AUTO";
    if (targetTiltAngle < 15) return "1";
    if (targetTiltAngle < 30) return "2";
    if (targetTiltAngle < 45) return "3";
    if (targetTiltAngle < 60) return "4";
    if (targetTiltAngle < 91) return "5";

    return "AUTO";
}

void printHPValues(float roomTemperature, heatpumpSettings settings) {
    LOG0("current settings:\n");
    LOG0("  power=");
    LOG0(settings.power);
    LOG0("\n");
    LOG0("  mode=");
    LOG0(settings.mode);
    LOG0("\n");
    LOG0("  current temperature=");
    LOG0(roomTemperature);
    LOG0("\n");
    LOG0("  target temperature=");
    LOG0(settings.temperature);
    LOG0("\n");
    LOG0("  fan=");
    LOG0(settings.fan);
    LOG0("\n");
}

void printHKValues() {
    LOG0("HK Values:\n");
    LOG0("  currentTemperature=%f\n", currentTemperature->getVal<float>());
    LOG0("  targetTemperature=%f\n", targetTemperature->getVal<float>());
    LOG0("  currentHeatingCoolingState=%d\n", currentHeatingCoolingState->getVal());
    LOG0("  targetHeatingCoolingState=%d\n", targetHeatingCoolingState->getVal());
    LOG0("  fanRotationSpeed=%d\n", fanRotationSpeed->getVal());
    LOG0("  currentSlatState=%d\n", currentSlatState->getVal());
    LOG0("  swingMode=%d\n", swingMode->getVal());
    LOG0("\n");
}

void updateValues(float roomTemperature, heatpumpSettings settings) {
    currentTemperature->setVal(roomTemperature);
    targetTemperature->setVal(max(10.0f, settings.temperature));
    currentHeatingCoolingState->setVal(getCurrentHeatingCoolingState(settings.power, settings.mode));
    targetHeatingCoolingState->setVal(getTargetHeatingCoolingState(settings.power, settings.mode));

    fanRotationSpeed->setVal(getFanRotationSpeed(settings.fan));

    currentSlatState->setVal(getCurrentSlatState(settings.vane));
    swingMode->setVal(getSwingMode(settings.vane));
}

struct ThermostatController : Service::Thermostat {

    ThermostatController() : Service::Thermostat() {
        temperatureDisplayUnits = new Characteristic::TemperatureDisplayUnits(1);
        currentTemperature = new Characteristic::CurrentTemperature();
        targetTemperature = new Characteristic::TargetTemperature();
        currentHeatingCoolingState = new Characteristic::CurrentHeatingCoolingState();
        targetHeatingCoolingState = new Characteristic::TargetHeatingCoolingState();
        coolingThresholdTemperature = new Characteristic::CoolingThresholdTemperature();
        heatingThresholdTemperature = new Characteristic::HeatingThresholdTemperature();
    }

    boolean update() {
        isUpdating = true;

        nextUpdateTime = millis() + 1000;

        return true;
    }

    void loop() {
        heatPump.sync();
        float roomTemperature = heatPump.getRoomTemperature();
        heatpumpSettings settings = heatPump.getSettings();

        if (isUpdating && nextUpdateTime < millis()) {
            LOG0("-- start HK Update--\n");

            printHPValues(roomTemperature, settings);
            printHKValues();

            float targetTemperatureVal = targetTemperature->getNewVal<float>();
            int targetHeatingCoolingStateVal = targetHeatingCoolingState->getNewVal();

            LOG0("Updating Thermostat\n");
            LOG0("targetTemperature=%f\n", targetTemperatureVal);
            LOG0("targetHeatingCoolingState=%i\n", targetHeatingCoolingStateVal);

            String powerSetting = getPowerSetting(targetHeatingCoolingStateVal);
            settings.power = powerSetting.c_str();

            String modeSetting = getModeSetting(targetHeatingCoolingStateVal);
            settings.mode = modeSetting.c_str();

            settings.temperature = targetTemperatureVal;

            int fanActiveVal = fanActive->getNewVal();
            int fanRotationSpeedVal = fanRotationSpeed->getNewVal();

            LOG0("Updating Fan\n");
            LOG0("fanActive=%i\n", fanActiveVal);
            LOG0("fanRotationSpeed=%i\n", fanRotationSpeedVal);

            String fanSpeed = getFanSpeed(fanRotationSpeedVal);
            settings.fan = fanSpeed.c_str();
            fanRotationSpeed->setVal(getFanRotationSpeed(fanSpeed));

            int swingModeVal = swingMode->getNewVal();
            int targetTiltAngleVal = targetTiltAngle->getNewVal();

            String vaneSetting = getVaneSetting(swingModeVal, targetTiltAngleVal);
            settings.vane = vaneSetting.c_str();

            heatPump.setSettings(settings);
            heatPump.update();

            // delay next polling so it doesn't overwrite new settings
            nextPollTime = millis() + HP_POLL_DELAY;
            isUpdating = false;
            LOG0("-- end HK update --\n");
        }

        if (!isUpdating && nextPollTime < millis()) {
            nextPollTime = millis() + HP_POLL_TIME_MS;

            printHPValues(roomTemperature, settings);

            if (roomTemperature == 0.0) {
                // sensors aren't ready
                return;
            }

            updateValues(roomTemperature, settings);

            printHKValues();
        }
    }
};

struct FanController : Service::Fan {

    FanController() : Service::Fan() {
        fanActive = new Characteristic::Active();
        fanRotationSpeed = new Characteristic::RotationSpeed();
    }

    boolean update() {
        isUpdating = true;

        nextUpdateTime = millis() + 1000;

        return true;
    }

    void loop() {
    }
};

struct SlatController : Service::Slat {
    SlatController() : Service::Slat() {
        currentSlatState = new Characteristic::CurrentSlatState();
        slatType = new Characteristic::SlatType(1);
        swingMode = new Characteristic::SwingMode();
        currentTiltAngle = new Characteristic::CurrentTiltAngle();
        targetTiltAngle = new Characteristic::TargetTiltAngle();
    }

    boolean update() {
        isUpdating = true;

        nextUpdateTime = millis() + 1000;

        return true;
    }
};

ThermostatController *thermostatController;
FanController *fanController;
SlatController *slatController;

TaskHandle_t h_HK_poll;
TaskHandle_t h_main_loop;

void HK_poll(void *pvParameters) {
    for (;;) {
        homeSpan.poll();
    } //loop
} //task

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

    heatPump.connect(&Serial2);
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
            HK_poll,    /* Task function. */
            "HK_poll",  /* name of task. */
            10000,      /* Stack size of task */
            NULL,       /* parameter of the task */
            1,          /* priority of the task */
            &h_HK_poll, /* Task handle to keep track of created task */
            0);         /* pin task to core 0 */

    delay(1000);
}

void loop() {
}