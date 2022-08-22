#include <Arduino.h>
#include <HomeSpan.h>
#include <HeatPump.h>

// Pairing Code: 466-37-726

const int STATUS_PIN = 2;
const int CONTROL_PIN = 27;

HeatPump heatPump;

boolean updating = false;
long updateTime = millis();

// Thermostat
/*
0 ”Celsius”
1 ”Fahrenheit”
2-255 ”Reserved”
*/
SpanCharacteristic * temperatureDisplayUnits;

// value in centigrade
SpanCharacteristic * currentTemperature;
SpanCharacteristic * targetTemperature;

/*
0 ”Off.”
1 ”Heat. The Heater is currently on.”
2 ”Cool. Cooler is currently on.”
3-255 ”Reserved”
*/
SpanCharacteristic * currentHeatingCoolingState;
/*
0 ”Off”
1 ”Heat. If the current temperature is below the target temperature then turn on heating.”
2 ”Cool. If the current temperature is above the target temperature then turn on cooling.”
3 ”Auto. Turn on heating or cooling to maintain temperature within the heating and cooling threshold of the target temperature.”
4-255 ”Reserved”
*/
SpanCharacteristic * targetHeatingCoolingState;
SpanCharacteristic * coolingThresholdTemperature;
SpanCharacteristic * heatingThresholdTemperature;


int getCurrentHeatingCoolingState(HeatPump heatPump) {
  String modeSetting = heatPump.getModeSetting();
  
  if (modeSetting == "HEAT") return 1;
  if (modeSetting == "COOL") return 2;
  return 0;
}

int getTargetHeatingCoolingState(HeatPump heatPump) {
  String modeSetting = heatPump.getModeSetting();
  
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
  if (fanRotationSpeed < 100.0*1.0/6.0) return "AUTO";
  if (fanRotationSpeed < 100.0*2.0/6.0) return "QUIET";
  if (fanRotationSpeed < 100.0*3.0/6.0) return "1";
  if (fanRotationSpeed < 100.0*4.0/6.0) return "2";
  if (fanRotationSpeed < 100.0*5.0/6.0) return "3";
  if (fanRotationSpeed <= 100.0*6.0/6.0) return "4";

  return "AUTO";
}

int getFanRotationSpeed(HeatPump heatPump) {
  String fanSpeed = heatPump.getFanSpeed();

  if (fanSpeed == "AUTO") return 100.0*1.0/6.0;
  if (fanSpeed == "QUIET") return 100.0*2.0/6.0;
  if (fanSpeed == "1") return 100.0*3.0/6.0;
  if (fanSpeed == "2") return 100.0*4.0/6.0;
  if (fanSpeed == "3") return 100.0*5.0/6.0;
  if (fanSpeed == "4") return 100.0*6.0/6.0;

  return 0;
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
    updating = true;

    updateTime = millis() + 1000;

    return true;
  }

  void loop() {
  }
};

// Fan
SpanCharacteristic * fanActive;
SpanCharacteristic * fanRotationSpeed;

struct FanController : Service::Fan {

  FanController() : Service::Fan() {
    fanActive = new Characteristic::Active();
    fanRotationSpeed = new Characteristic::RotationSpeed();
  }

  boolean update() {
    updating = true;

    updateTime = millis() + 1000;

    return true;
  }

  void loop() {
  }
};

// Slat
/*
0 ”Fixed”
1 ”Jammed”
2 ”Swinging”
*/
SpanCharacteristic * currentSlatState;
/*
0 ”Horizontal”
1 ”Vertical”
*/
SpanCharacteristic * slatType;
/*
0 ”Swing disabled”
1 ”Swing enabled”
*/
SpanCharacteristic * swingMode;
// tilt angle between -90 and 90 degrees
SpanCharacteristic * currentTiltAngle;
SpanCharacteristic * targetTiltAngle;

int getCurrentSlatState(HeatPump heatPump) {
  if (heatPump.getVaneSetting() == "SWING") return 2;
  return 0;
}

int getSwingMode(HeatPump heatPump) {
  if (heatPump.getVaneSetting() == "SWING") return 1;
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

struct SlatController : Service::Slat {
  SlatController() : Service::Slat() {
    currentSlatState = new Characteristic::CurrentSlatState();
    slatType = new Characteristic::SlatType(1);
    swingMode = new Characteristic::SwingMode();
    currentTiltAngle = new Characteristic::CurrentTiltAngle();
    targetTiltAngle = new Characteristic::TargetTiltAngle();
  }

  boolean update() {
    updating = true;

    updateTime = millis() + 1000;

    return true;
  }
};

ThermostatController * thermostatController;
FanController * fanController;
SlatController * slatController;

TaskHandle_t h_HK_poll;

void HK_poll(void *pvParameters)
{
  for (;;)
  {
    homeSpan.poll();
  } //loop
} //task

void setup()
{
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

void loop()
{
  if (updating && updateTime < millis()) {
    heatpumpSettings settings = heatPump.getSettings();

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

    int swingModeVal = swingMode->getNewVal();
    int targetTiltAngleVal = targetTiltAngle->getNewVal();

    String vaneSetting = getVaneSetting(swingModeVal, targetTiltAngleVal);
    settings.vane = vaneSetting.c_str();

    LOG0("new settings: {power: %s, mode: %s, temperature: %f, fan: %s, vane: %s }\n", settings.power, settings.mode, settings.temperature, settings.fan, settings.vane);
    heatPump.setSettings(settings);

    updating = false;
  }

  if (!updating) {
    delay(1000);
    if (updating) return;

    heatPump.sync();
    
    heatpumpSettings settings = heatPump.getSettings();
    LOG0("current settings:\n");
    LOG0("power: ");
    LOG0(settings.power);
    LOG0("\nmode: ");
    LOG0(settings.mode);
    LOG0("\ntemperature: ");
    LOG0(settings.temperature);
    LOG0("\nfan: ");
    LOG0(settings.fan);
    LOG0("\n");

    currentTemperature->setVal(heatPump.getRoomTemperature());
    targetTemperature->setVal(max(10.0f, heatPump.getTemperature()));
    currentHeatingCoolingState->setVal(getCurrentHeatingCoolingState(heatPump));
    targetHeatingCoolingState->setVal(getTargetHeatingCoolingState(heatPump));

    currentSlatState->setVal(getCurrentSlatState(heatPump));
    swingMode->setVal(getSwingMode(heatPump));
    // currentTiltAngle->setVal(0);
    // targetTiltAngle->setVal(0);

    fanRotationSpeed->setVal(getFanRotationSpeed(heatPump));
  }
}
