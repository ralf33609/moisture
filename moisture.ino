// # claude version 0.106
// DIY Moisture Sensor - ESP32-C3 - OpenMQTTGateway / HHCCJCY10 Emulation
// GPIO4 = Moisture ADC, GPIO2 = VCC Messung (parasitärer Pullup)
// GPIO5 = Signal LED extern rot (500ms), LED_BUILTIN = blaue interne LED (500ms)
// GPIO1 = Sensor Power (HIGH während awake)
// GPIO3 = GND für Sensor-Steckverbinder (permanent LOW)
// Vorlauf 1000ms für Einschwingzeit, Senden 60s, Deep Sleep 1 Minute (Entwicklungsphase)
// lux = fortlaufender Paketzähler 1000-2000 (RTC, überlebt Deep Sleep)

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

const int MOISTURE_PIN  = 4;      // GPIO4 (ADC1_CH4), kein Strapping-Pin
const int LED_PIN       = 5;      // GPIO5, Signal-LED extern rot
const int LED_PIN2      = LED_BUILTIN; // interne blaue LED
const int SENSOR_PWR    = 1;      // GPIO1, Sensor Power (HIGH während awake)
const int SENSOR_GND    = 3;      // GPIO3, Sensor GND (permanent LOW)
const int ADC_SAMPLES   = 10;
const int WARMUP_MS     = 1000;   // Vorlauf für TL555 Einschwingzeit
const int ADVERTISE_MS  = 20000;  // 60 Sekunden senden (Faktor 3 × vorher)
const uint64_t SLEEP_US = 60ULL * 1000000ULL;  // 1 Minute (Entwicklungsphase)

// Paketzähler im RTC-Speicher (überlebt Deep Sleep)
RTC_DATA_ATTR uint32_t lux = 1000;

// HHCCJCY10 Payload Layout (9 Bytes, big-endian):
// [0]     moi   uint8   0-255
// [1-2]   tempc int16   /10 = °C
// [3-5]   lux   uint24  Paketzähler 1000-2000
// [6]     batt  uint8   VCC in 0.1V Schritten (z.B. 28 = 2.8V)
//               VCC 0-3300mV → /100 → 0-33 (direkt ablesbar als Volt×10)
// [7-8]   fer   uint16  µS/cm

uint8_t readMoisture() {
  pinMode(MOISTURE_PIN, INPUT);
  analogSetAttenuation(ADC_11db);
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(MOISTURE_PIN);
    delay(10);
  }
  return (uint8_t)((sum / ADC_SAMPLES) / 16);
}

void signalLed(int ms) {
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(LED_PIN2, HIGH);
  delay(ms);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN2, LOW);
}

void setup() {
  Serial.begin(115200);

  // Sensor GND permanent LOW
  pinMode(SENSOR_GND, OUTPUT);
  digitalWrite(SENSOR_GND, LOW);

  // Sensor einschalten
  pinMode(SENSOR_PWR, OUTPUT);
  digitalWrite(SENSOR_PWR, HIGH);

  // Signal-LED kurz aufleuchten (500ms) → visuelles Lebenszeichen
  signalLed(500);

  // Vorlauf für TL555 Einschwingzeit
  delay(WARMUP_MS);

  uint8_t  moi     = readMoisture();
  int16_t  tempc10 = 250;   // Dummy 25.0°C
  // VCC-Messung via GPIO2 parasitärem Pullup (kein externer Spannungsteiler)
  // analogReadMilliVolts liefert 0-3300mV → /100 → 0-33 (Volt×10, z.B. 28 = 2.8V)
  uint8_t  batt    = (uint8_t)(analogReadMilliVolts(A2) / 100);
  uint16_t fer     = 0;

  Serial.printf("[DEBUG] moi: %u | tempc: %.1f°C | batt: %u (= %u.%uV) | lux: %u\n",
                moi, tempc10 / 10.0, batt, batt / 10, batt % 10, lux);

  uint8_t sd[9];
  sd[0] = moi;
  sd[1] = (uint8_t)((tempc10 >> 8) & 0xFF);
  sd[2] = (uint8_t)(tempc10 & 0xFF);
  sd[3] = (uint8_t)((lux >> 16) & 0xFF);
  sd[4] = (uint8_t)((lux >> 8) & 0xFF);
  sd[5] = (uint8_t)(lux & 0xFF);
  sd[6] = batt;
  sd[7] = (uint8_t)(fer >> 8);
  sd[8] = (uint8_t)(fer & 0xFF);

  BLEDevice::init("Flower care");
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();

  BLEAdvertisementData oData;
  oData.setCompleteServices(BLEUUID((uint16_t)0xFD50));

  String sdStr = "";
  for (int i = 0; i < sizeof(sd); i++) sdStr += (char)sd[i];
  oData.setServiceData(BLEUUID((uint16_t)0xFD50), sdStr);

  pAdv->setAdvertisementData(oData);
  pAdv->start();

  Serial.println("[DEBUG] Sende Paket 60s...");
  delay(ADVERTISE_MS);
  pAdv->stop();

  // Paketzähler erhöhen, bei 2001 zurück auf 1000
  lux++;
  if (lux > 2000) lux = 1000;

  Serial.println("[DEBUG] Deep Sleep 1 Minute...");
  esp_sleep_enable_timer_wakeup(SLEEP_US);
  esp_deep_sleep_start();
}

void loop() {}
