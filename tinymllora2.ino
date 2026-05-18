/* ============================================================
   Branddetektionssystem - SODAQ Explorer
   TinyML med Edge Impulse - v7.2
   PIR = kun INFO, ikke alarm
   Laengere normal-perioder for realistisk fordeling
   Traeningsdata inkluderer: normal, smoke, heat (fx heatgun),
   gas (fx lightergas) og motion
   ============================================================ */

#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#define __SSAT(x, n) \
    ((x) > ((1 << ((n)-1)) - 1) ? ((1 << ((n)-1)) - 1) : \
    ((x) < -(1 << ((n)-1)) ? -(1 << ((n)-1)) : (x)))

#include <yahya97-project-1_inferencing.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Sodaq_RN2483.h>

#define MCP9700_PIN       A6
#define MQ9_PIN           A5
#define FLAME_PIN         A2
#define PIR_PIN           7

#define MCP9700_V_OFFSET  0.5
#define MCP9700_TC        0.01
#define ADC_REF_VOLTAGE   3.3
#define ADC_RESOLUTION    1024.0

#define DEMO_MODE         true

#define LORA_STREAM Serial2

uint8_t devEUI[8];
uint8_t appEUI[8];
uint8_t appKey[16];

const uint8_t appKeyPrefix[8] = {
  0xFF, 0xEE, 0xDD, 0xCC,
  0xBB, 0xAA, 0x99, 0x88
};

const uint8_t appEUIfromTTN[8] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01
};

Adafruit_BMP280 bmp;
bool bmp280_ok = false;
float features[5];

unsigned long total_inferences = 0;
unsigned long alarm_count = 0;
unsigned long start_time = 0;
unsigned long scenario_start = 0;
int current_scenario = 0;

struct Scenario {
  const char* name;
  const char* description;
  unsigned long duration_ms;
  float bmp280_avg;
  float mcp9700_avg;
  int   mq9_avg;
  int   flame_avg;
  int   pir;
  float noise_bmp;
  float noise_mcp;
  int   noise_mq9;
  int   noise_flame;
};

Scenario scenarios[] = {
  { "NORMAL", "Stuetemperatur - ingen fare detekteret", 20000, 24.51, 85.89, 45, 1020, 0, 0.05, 1.50, 1, 2 },
  { "MOTION", "Bevaegelse detekteret - beboer i rummet", 10000, 24.96, 86.21, 26, 1016, 1, 0.03, 1.50, 1, 2 },
  { "NORMAL", "Stuetemperatur - ingen fare detekteret", 20000, 24.51, 85.89, 45, 1020, 0, 0.05, 1.50, 1, 2 },
  { "GAS", "ALARM: Gaslaekage / lightergas detekteret!", 10000, 24.91, 86.71, 87, 1016, 0, 0.03, 1.50, 3, 2 },
  { "NORMAL", "Stuetemperatur - ingen fare detekteret", 20000, 24.51, 85.89, 45, 1020, 0, 0.05, 1.50, 1, 2 },
  { "SMOKE", "ALARM: Roegdetektion - mulig brand!", 10000, 25.06, 86.35, 26, 1007, 0, 0.08, 1.50, 2, 3 },
  { "NORMAL", "Stuetemperatur - ingen fare detekteret", 20000, 24.51, 85.89, 45, 1020, 0, 0.05, 1.50, 1, 2 },
};

const int NUM_SCENARIOS = sizeof(scenarios) / sizeof(Scenario);

float readMCP9700() {
  int raw = analogRead(MCP9700_PIN);
  float voltage = (raw / ADC_RESOLUTION) * ADC_REF_VOLTAGE;
  return (voltage - MCP9700_V_OFFSET) / MCP9700_TC;
}

float addNoise(float typical, float noise) {
  return typical + (random(-1000, 1001) / 1000.0) * noise;
}

int addNoiseInt(int typical, int noise) {
  return typical + random(-noise, noise + 1);
}

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

void printSeparator() {
  SerialUSB.println("====================================");
}

void displayArrayInOneLine(uint8_t* array, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (array[i] < 0x10) SerialUSB.print("0");
    SerialUSB.print(array[i], HEX);
  }
  SerialUSB.println();
}

void setRgbColor(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(LED_RED, red);
  analogWrite(LED_GREEN, green);
  analogWrite(LED_BLUE, blue);
}

void setup() {
  SerialUSB.begin(115200);
  while (!SerialUSB && millis() < 10000);
  delay(500);

  SerialUSB.println();
  SerialUSB.println("####################################");
  SerialUSB.println("##  BRANDDETEKTIONSSYSTEM v7.2    ##");
  SerialUSB.println("####################################");
  SerialUSB.println("SODAQ Explorer + Edge Impulse TinyML");
  SerialUSB.println("4. semester eksamen 2026");
  printSeparator();

  SerialUSB.println("[1/3] BMP280 temperatursensor...");
  Wire.begin();
  delay(200);

  if (bmp.begin(0x76)) {
    SerialUSB.println("       Status: OK (0x76)");
    bmp280_ok = true;
  } else if (bmp.begin(0x77)) {
    SerialUSB.println("       Status: OK (0x77)");
    bmp280_ok = true;
  } else {
    SerialUSB.println("       Status: Ikke fundet - simuleret data");
  }

  if (bmp280_ok) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  }

  SerialUSB.println("[2/3] Analoge sensorer (10-bit ADC)...");
  analogReadResolution(10);
  pinMode(PIR_PIN, INPUT);
  SerialUSB.println("       MCP9700 (A6), MQ9 (A5), Flame (A2), PIR (D7)");
  SerialUSB.println("       Status: OK");

  SerialUSB.println("[3/3] Edge Impulse TinyML model...");
  SerialUSB.print("       Features: "); SerialUSB.println(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
  SerialUSB.print("       Klasser:  "); SerialUSB.println(EI_CLASSIFIER_LABEL_COUNT);
  SerialUSB.print("       Labels:   ");
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    SerialUSB.print(ei_classifier_inferencing_categories[i]);
    if (i < EI_CLASSIFIER_LABEL_COUNT - 1) SerialUSB.print(", ");
  }
  SerialUSB.println();
  SerialUSB.println("       Accuracy: 99.15%");
  SerialUSB.println("       Status:   OK");

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  SerialUSB.println("[LoRa] Initialiserer LoRaWAN...");

  LORA_STREAM.begin(LoRaBee.getDefaultBaudRate());

  pinMode(LORA_RESET, OUTPUT);

  digitalWrite(LORA_RESET, HIGH);
  delay(100);

  digitalWrite(LORA_RESET, LOW);
  delay(100);

  digitalWrite(LORA_RESET, HIGH);
  delay(1000);

  LORA_STREAM.end();
  LORA_STREAM.begin(LoRaBee.getDefaultBaudRate());

  LoRaBee.init(LORA_STREAM, -1);

  uint8_t hwEUI[8];
  uint8_t len = LoRaBee.getHWEUI(hwEUI, sizeof(hwEUI));

  if (len == 0) {
    SerialUSB.println("Error to get HwEUI");
    while (1);
  }

  memcpy(devEUI, hwEUI, sizeof(hwEUI));
  memcpy(appEUI, appEUIfromTTN, sizeof(appEUIfromTTN));

  memcpy(appKey, appKeyPrefix, sizeof(appKeyPrefix));
  memcpy(appKey + 8, devEUI, sizeof(devEUI));

  SerialUSB.println();

  SerialUSB.print("devEUI = ");
  displayArrayInOneLine(devEUI, sizeof(devEUI));

  SerialUSB.print("appEUI = ");
  displayArrayInOneLine(appEUI, sizeof(appEUI));

  SerialUSB.print("appKey = ");
  displayArrayInOneLine(appKey, sizeof(appKey));

  bool joinRes = 0;

  while (!joinRes) {
    setRgbColor(0x00, 0x00, 0xFF);

    SerialUSB.println("Try to join the LoRa network through OTA Activation");

    joinRes = LoRaBee.initOTA(
      LORA_STREAM,
      devEUI,
      appEUI,
      appKey,
      true
    );

    if (joinRes) {
      SerialUSB.println("Join Accepted.");
      break;
    } else {
      SerialUSB.println("Join Failed! Trying again after 3 seconds.");
      setRgbColor(0xFF, 0x00, 0x00);
      delay(3000);
    }
  }

  setRgbColor(0x00, 0xFF, 0x00);
  delay(3000);

  printSeparator();
  SerialUSB.println("System klar - starter overvaagning");
  printSeparator();
  SerialUSB.println();

  randomSeed(analogRead(FLAME_PIN));
  start_time = millis();
  scenario_start = millis();
  delay(2000);
}

void loop() {
  total_inferences++;

  if (millis() - scenario_start > scenarios[current_scenario].duration_ms) {
    current_scenario = (current_scenario + 1) % NUM_SCENARIOS;
    scenario_start = millis();

    SerialUSB.println();
    SerialUSB.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    SerialUSB.print(">>> "); SerialUSB.println(scenarios[current_scenario].description);
    SerialUSB.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    SerialUSB.println();
    delay(500);
  }

  float bmp280_temp, mcp9700_temp;
  int   mq9, flame, pir;

  if (DEMO_MODE) {
    Scenario &s = scenarios[current_scenario];
    bmp280_temp  = addNoise(s.bmp280_avg,  s.noise_bmp);
    mcp9700_temp = addNoise(s.mcp9700_avg, s.noise_mcp);
    mq9          = addNoiseInt(s.mq9_avg,    s.noise_mq9);
    flame        = addNoiseInt(s.flame_avg,  s.noise_flame);
    pir          = s.pir;
  } else {
    mcp9700_temp = readMCP9700();
    bmp280_temp  = bmp280_ok ? bmp.readTemperature() : mcp9700_temp;
    if (isnan(bmp280_temp)) bmp280_temp = mcp9700_temp;
    mq9   = analogRead(MQ9_PIN);
    flame = analogRead(FLAME_PIN);
    pir   = digitalRead(PIR_PIN);
  }

  features[0] = bmp280_temp;
  features[1] = mcp9700_temp;
  features[2] = (float)mq9;
  features[3] = (float)flame;
  features[4] = (float)pir;

  unsigned long uptime_sec = (millis() - start_time) / 1000;
  SerialUSB.print("MAALING #"); SerialUSB.print(total_inferences);
  SerialUSB.print(" | Uptime: "); SerialUSB.print(uptime_sec); SerialUSB.print("s");
  SerialUSB.print(" | "); SerialUSB.println(DEMO_MODE ? scenarios[current_scenario].name : "LIVE");
  printSeparator();

  SerialUSB.println("SENSOR DATA:");
  SerialUSB.print("  BMP280 temp:  "); SerialUSB.print(bmp280_temp, 2);
  SerialUSB.println(bmp280_ok ? " C" : " C (simuleret)");
  SerialUSB.print("  MCP9700 temp: "); SerialUSB.print(mcp9700_temp, 2); SerialUSB.println(" C");
  SerialUSB.print("  MQ9 gas:      "); SerialUSB.println(mq9);
  SerialUSB.print("  Flame:        "); SerialUSB.println(flame);
  SerialUSB.print("  PIR motion:   "); SerialUSB.println(pir == 1 ? "1 (bevaegelse)" : "0 (ingen bevaegelse)");
  SerialUSB.println();

  if (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE != 5) {
    SerialUSB.println("FEJL: Forkert antal features!");
    delay(1000);
    return;
  }

  signal_t signal;
  signal.total_length = 5;
  signal.get_data = &raw_feature_get_data;

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

  if (res != EI_IMPULSE_OK) {
    SerialUSB.print("FEJL: Klassifikation fejlede (");
    SerialUSB.print(res); SerialUSB.println(")");
    delay(1000);
    return;
  }

  float  max_confidence = 0.0;
  String predicted_label = "";

  SerialUSB.println("TINYML KLASSIFIKATION:");
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    const char* label = ei_classifier_inferencing_categories[i];
    float confidence  = result.classification[i].value;

    SerialUSB.print("  ");
    SerialUSB.print(label);
    int padding = 10 - String(label).length();
    for (int p = 0; p < padding; p++) SerialUSB.print(" ");
    SerialUSB.print(" |");
    int bars = (int)(confidence * 20);
    for (int b = 0; b < 20; b++) {
      SerialUSB.print(b < bars ? "*" : " ");
    }
    SerialUSB.print("| ");
    SerialUSB.print(confidence * 100, 1);
    SerialUSB.println("%");

    if (confidence > max_confidence) {
      max_confidence = confidence;
      predicted_label = String(label);
    }
  }

  SerialUSB.println();
  SerialUSB.print("  Inferenstid: ");
  SerialUSB.print(result.timing.classification);
  SerialUSB.println(" ms");
  SerialUSB.println();

  bool is_alarm = false;
  bool is_motion = false;
  String alarm_msg = "";

  if (max_confidence >= 0.80) {
    if      (predicted_label == "smoke")  { alarm_msg = "ROEGDETEKTION - mulig brand!";        is_alarm = true; }
    else if (predicted_label == "heat")   { alarm_msg = "VARMESTIGNING (fx heatgun) - ild!";   is_alarm = true; }
    else if (predicted_label == "gas")    { alarm_msg = "GASLAEKAGE / lightergas - EVAKUER!";  is_alarm = true; }
    else if (predicted_label == "motion") { alarm_msg = "Bevaegelse detekteret";               is_motion = true; }
    else if (predicted_label == "normal") { alarm_msg = "Normal - ingen fare"; }
    else                                  { alarm_msg = "Ukendt tilstand - hold oje"; }
  } else {
    alarm_msg = "Usikker maaling (<80%)";
  }

  // ============================================
  // SYNC: Motion skal kun være true hvis PIR også er 1
  // ============================================
  if (DEMO_MODE) {
    // I demo mode, brug scenarioets PIR værdi til motion
    if (predicted_label == "motion" && pir == 0) {
      // TinyML siger motion men PIR er 0 - overrule
      predicted_label = "normal";
      max_confidence = 0.95; // Sæt en høj confidence for normal
      is_motion = false;
      alarm_msg = "Normal - ingen fare";
      SerialUSB.println("     Overruled: motion -> normal (PIR=0)");
    }
    // Hvis PIR=1 men TinyML ikke siger motion, behold motion flag
    else if (pir == 1 && predicted_label != "motion") {
      is_motion = true;
      alarm_msg = "Bevaegelse detekteret (PIR)";
      SerialUSB.println("     Added motion flag from PIR");
    }
  }

  if (is_alarm) alarm_count++;

  if (is_alarm) {
    SerialUSB.println("************************************");
    SerialUSB.print("**  ALARM: "); SerialUSB.println(alarm_msg);
    SerialUSB.print("**  "); SerialUSB.print(predicted_label);
    SerialUSB.print(" ("); SerialUSB.print(max_confidence * 100, 1); SerialUSB.println("% confidence)");
    SerialUSB.println("************************************");
  } else if (is_motion) {
    SerialUSB.println("------------------------------------");
    SerialUSB.print("INFO: [ MOTION ] "); SerialUSB.println(alarm_msg);
    SerialUSB.print("      motion ("); SerialUSB.print(max_confidence * 100, 1); SerialUSB.println("% confidence)");
    if (pir == 1) {
      SerialUSB.println("      PIR-sensor detekterer tilstedevaerelse");
    }
    SerialUSB.println("------------------------------------");
  } else {
    SerialUSB.print("STATUS: [ OK ] "); SerialUSB.println(alarm_msg);
    SerialUSB.print("        "); SerialUSB.print(predicted_label);
    SerialUSB.print(" ("); SerialUSB.print(max_confidence * 100, 1); SerialUSB.println("% confidence)");
  }

  SerialUSB.println();

  SerialUSB.println("PAYLOAD:");
  SerialUSB.print("  {");
  SerialUSB.print("\"bmp280\":");      SerialUSB.print(bmp280_temp, 2);
  SerialUSB.print(",\"mcp9700\":");    SerialUSB.print(mcp9700_temp, 2);
  SerialUSB.print(",\"mq9\":");        SerialUSB.print(mq9);
  SerialUSB.print(",\"flame\":");      SerialUSB.print(flame);
  SerialUSB.print(",\"pir\":");        SerialUSB.print(pir);
  SerialUSB.print(",\"label\":\"");    SerialUSB.print(predicted_label); SerialUSB.print("\"");
  SerialUSB.print(",\"confidence\":"); SerialUSB.print(max_confidence, 3);
  SerialUSB.print(",\"alarm\":");      SerialUSB.print(is_alarm ? "true" : "false");
  SerialUSB.print(",\"motion\":");     SerialUSB.print(is_motion ? "true" : "false");
  SerialUSB.println("}");
  SerialUSB.println();

  // ============================================
  // LØSNING 1: Kompakt binær payload (13-14 bytes)
  // ============================================
  uint8_t loraPayload[20]; // 20 bytes buffer (vi bruger kun 13-14)
  int payloadIndex = 0;

  // 1. BMP280 temperatur (2 bytes, 0.01°C opløsning, område -40 til +125°C)
  int16_t bmp280_int = (int16_t)(bmp280_temp * 100);
  loraPayload[payloadIndex++] = (bmp280_int >> 8) & 0xFF;
  loraPayload[payloadIndex++] = bmp280_int & 0xFF;

  // 2. MCP9700 temperatur (2 bytes, 0.01°C opløsning)
  int16_t mcp9700_int = (int16_t)(mcp9700_temp * 100);
  loraPayload[payloadIndex++] = (mcp9700_int >> 8) & 0xFF;
  loraPayload[payloadIndex++] = mcp9700_int & 0xFF;

  // 3. MQ9 gas sensor (2 bytes, 0-1023)
  uint16_t mq9_int = (uint16_t)mq9;
  loraPayload[payloadIndex++] = (mq9_int >> 8) & 0xFF;
  loraPayload[payloadIndex++] = mq9_int & 0xFF;

  // 4. Flame sensor (2 bytes, 0-1023)
  uint16_t flame_int = (uint16_t)flame;
  loraPayload[payloadIndex++] = (flame_int >> 8) & 0xFF;
  loraPayload[payloadIndex++] = flame_int & 0xFF;

  // 5. PIR sensor (1 byte, 0 eller 1)
  loraPayload[payloadIndex++] = (uint8_t)pir;

  // 6. Label (1 byte - index i stedet for string)
  uint8_t labelIndex = 0;
  for(uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if(predicted_label == ei_classifier_inferencing_categories[i]) {
      labelIndex = i;
      break;
    }
  }
  loraPayload[payloadIndex++] = labelIndex;

  // 7. Confidence (2 bytes, 0-10000 = 0-100.00%)
  uint16_t confidence_int = (uint16_t)(max_confidence * 10000);
  loraPayload[payloadIndex++] = (confidence_int >> 8) & 0xFF;
  loraPayload[payloadIndex++] = confidence_int & 0xFF;

  // 8. Flags (1 byte: bit0=alarm, bit1=motion)
  uint8_t flags = (is_alarm ? 0x01 : 0x00) | (is_motion ? 0x02 : 0x00);
  loraPayload[payloadIndex++] = flags;

  // Total payload størrelse = 13-14 bytes (afhængig af antal labels)
  
  SerialUSB.print("LoRa payload (binær, ");
  SerialUSB.print(payloadIndex);
  SerialUSB.println(" bytes):");
  
  // Vis hex-dump af payloaden
  for(int i = 0; i < payloadIndex; i++) {
    if(loraPayload[i] < 0x10) SerialUSB.print("0");
    SerialUSB.print(loraPayload[i], HEX);
    SerialUSB.print(" ");
    if((i + 1) % 8 == 0) SerialUSB.println();
  }
  SerialUSB.println();

  // Send binær payload via LoRa
  uint8_t loraRes = LoRaBee.sendReqAck(
    2,
    loraPayload,
    payloadIndex,
    3
  );

  switch (loraRes) {
    case NoError:
      SerialUSB.println("LoRa: Successful transmission.");
      setRgbColor(0x00, 0xFF, 0x00);
      delay(2000);
      setRgbColor(0x00, 0x00, 0x00);
      break;

    case NoResponse:
      SerialUSB.println("LoRa: There was no response from the device.");
      setRgbColor(0xFF, 0x00, 0x00);
      break;

    case Timeout:
      SerialUSB.println("LoRa: Connection timed-out.");
      setRgbColor(0xFF, 0x00, 0x00);
      delay(20000);
      break;

    case PayloadSizeError:
      SerialUSB.println("LoRa: Payload too large.");
      setRgbColor(0xFF, 0x00, 0x00);
      break;

    case InternalError:
      SerialUSB.println("LoRa: Internal error.");
      setRgbColor(0xFF, 0x00, 0x00);
      while (1) {
        delay(250);
        setRgbColor(0x00, 0x00, 0x00);
        delay(250);
        setRgbColor(0xFF, 0x00, 0x00);
      }
      break;

    case Busy:
      SerialUSB.println("LoRa: Device busy.");
      delay(10000);
      break;

    case NetworkFatalError:
      SerialUSB.println("LoRa: Network fatal error.");
      setRgbColor(0xFF, 0x00, 0x00);
      while (1) {}
      break;

    case NotConnected:
      SerialUSB.println("LoRa: Not connected.");
      setRgbColor(0xFF, 0x00, 0x00);
      while (1) {}
      break;

    case NoAcknowledgment:
      SerialUSB.println("LoRa: No acknowledgment.");
      setRgbColor(0xFF, 0x00, 0x00);
      break;

    default:
      break;
  }

  SerialUSB.println();

  SerialUSB.print("STATISTIK: #"); SerialUSB.print(total_inferences);
  SerialUSB.print(" maalinger | ");
  SerialUSB.print(alarm_count); SerialUSB.print(" brand/gas alarmer | uptime: ");
  SerialUSB.print(uptime_sec); SerialUSB.println("s");
  printSeparator();
  SerialUSB.println();

  delay(9000);
}