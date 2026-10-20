#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "soc/rtc.h"
#include "esp_system.h"
#include <driver/rtc_io.h>
#include <helpers/KeyValueStore.h>

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  bool inhibit_sleep = false;
  static inline portMUX_TYPE sleepMux = portMUX_INITIALIZER_UNLOCKED;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;    

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif    
  }

  void attachDynamicPrefs(KeyValueStore* prefs) { }  // no-op

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  virtual void powerOff() override;
  void enterDeepSleep(uint32_t secs);

  uint32_t getIRQGpio() override {
    return P_LORA_DIO_1; // default for SX1262
  }

  void sleep(uint32_t secs) override {
    // Skip if not allow to sleep
    if (inhibit_sleep) {
      delay(1); // Give MCU to OTA to run
      return;
    }

    // Set GPIO wakeup
    gpio_num_t wakeupPin = (gpio_num_t)getIRQGpio();    

    // Configure timer wakeup
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL); // Wake up periodically to do scheduled jobs
    }

    // Disable CPU interrupt servicing
    portENTER_CRITICAL(&sleepMux);

    // Skip sleep if there is a LoRa packet
    if (gpio_get_level(wakeupPin) == HIGH) {
      portEXIT_CRITICAL(&sleepMux);
      delay(1);
      return;
    }

    // Configure GPIO wakeup
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)wakeupPin, GPIO_INTR_HIGH_LEVEL); // Wake up when receiving a LoRa packet

    // MCU enters light sleep
    esp_light_sleep_start();

    // Avoid ISR flood during wakeup due to HIGH LEVEL interrupt
    gpio_wakeup_disable(wakeupPin);
    gpio_set_intr_type(wakeupPin, GPIO_INTR_POSEDGE);

    // Enable CPU interrupt servicing
    portEXIT_CRITICAL(&sleepMux);
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
    #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
  void beginWifiOTA(const char* id);
  void loopWifiOTA();
  void stopWifiOTAAP();
  void requestWifiOTAAPStop(uint32_t delay_ms = 2000);
  void stopWifiOTA();
  void requestWifiOTAStop(uint32_t delay_ms = 2000);
  bool isWifiOTAActive() const;
  bool isWifiSTAEnabled() const;
  void setWifiSTAEnabled(bool enabled);
  bool isWebEnabled() const;
  bool hasWebPassword() const;
  bool setWebPassword(const char* password);
  void setWebFallbackPassword(const char* password);
  void setWebEnabled(bool enabled);
  void formatWebStatus(char* reply, size_t reply_len) const;
  void formatWifiOTAStatus(char* reply, size_t reply_len) const;
  uint32_t getWifiOTASecondsRemaining() const;
  void setBootStatus(const char* status);
  void setRadioStatus(bool ok, int status);
  typedef void (*WifiOTACommandHandler)(char* command, char* reply);
  void setWifiOTACommandHandler(WifiOTACommandHandler handler);

  uint32_t getResetReason() const override {
    return esp_reset_reason();
  }

  const char* getResetReasonString(uint32_t reason) override;

  uint32_t getFreeHeap() const {
    return ESP.getFreeHeap();
  }

  uint32_t getMinFreeHeap() const {
    return ESP.getMinFreeHeap();
  }

  void setInhibitSleep(bool inhibit) {
    inhibit_sleep = inhibit;
  }
};

static RTC_NOINIT_ATTR uint32_t _rtc_backup_time;
static RTC_NOINIT_ATTR uint32_t _rtc_backup_magic;
#define RTC_BACKUP_MAGIC  0xAA55CC33
#define RTC_TIME_MIN      1772323200  // 1 Mar 2026

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      return;  // ESP-IDF preserves system time across deep sleep
    }
    // All other resets (power-on, crash, WDT, brownout) lose system time.
    // Restore from RTC backup if valid, otherwise use hardcoded seed.
    struct timeval tv;
    if (_rtc_backup_magic == RTC_BACKUP_MAGIC && _rtc_backup_time > RTC_TIME_MIN) {
      tv.tv_sec = _rtc_backup_time;
    } else {
      tv.tv_sec = RTC_TIME_MIN;
    }
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override {
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    _rtc_backup_time = time;
    _rtc_backup_magic = RTC_BACKUP_MAGIC;
  }
  void tick() override {
    time_t now;
    time(&now);
    if (now > RTC_TIME_MIN && (uint32_t)now != _rtc_backup_time) {
      _rtc_backup_time = (uint32_t)now;
      _rtc_backup_magic = RTC_BACKUP_MAGIC;
    }
  }
};

#endif
