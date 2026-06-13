#include "GnssReceiver.h"

#include <Arduino.h>

namespace gnss {
namespace {

// Cardputer ADV Cap LoRa868 wiring, matching the official demo HAL.
constexpr int kRxPin = 15;
constexpr int kTxPin = 13;
constexpr uint32_t kBaudRate = 115200;
constexpr uint32_t kProbeIntervalMs = 2000;
constexpr char kFirmwareQuery[] = "$PCAS06,0*1B\r\n";

HardwareSerial gnssSerial(2);
State state;
SemaphoreHandle_t stateMutex = nullptr;
TaskHandle_t taskHandle = nullptr;
uint32_t lastProbeMs = 0;

void beginSerial() {
  gnssSerial.setRxBufferSize(1024);
  gnssSerial.begin(kBaudRate, SERIAL_8N1, kRxPin, kTxPin);
  state.resetParser();
}

void sendProbe() {
  gnssSerial.write(reinterpret_cast<const uint8_t*>(kFirmwareQuery), sizeof(kFirmwareQuery) - 1);
  lastProbeMs = millis();
}

void task(void*) {
  beginSerial();
  sendProbe();
  for (;;) {
    while (gnssSerial.available()) {
      const int value = gnssSerial.read();
      if (value < 0) break;
      if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        state.feed(static_cast<char>(value), millis(), kBaudRate);
        xSemaphoreGive(stateMutex);
      }
    }

    Snapshot snapshot;
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      snapshot = state.snapshot(millis());
      xSemaphoreGive(stateMutex);
    }
    if (!snapshot.present && millis() - lastProbeMs >= kProbeIntervalMs) sendProbe();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool begin() {
  if (taskHandle) return true;
  stateMutex = xSemaphoreCreateMutex();
  if (!stateMutex) return false;
  return xTaskCreate(task, "gnss", 4096, nullptr, 1, &taskHandle) == pdPASS;
}

Snapshot current() {
  Snapshot result;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    result = state.snapshot(millis());
    xSemaphoreGive(stateMutex);
  }
  return result;
}

}  // namespace gnss
