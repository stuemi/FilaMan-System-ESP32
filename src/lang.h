#ifndef LANG_H
#define LANG_H

#include <Arduino.h>

// Supported languages
enum Lang : uint8_t {
    LANG_EN = 0,
    LANG_DE = 1,
    LANG_COUNT
};

// String IDs for all translatable display texts
enum StringID : uint8_t {
    // Boot / Init
    STR_DISPLAY_INIT,
    STR_WIFI_INIT,
    STR_WEBSERVER_INIT,
    STR_API_INIT,
    STR_NFC_INIT,
    STR_SEARCHING_SCALE,
    STR_INIT_DONE,

    // Scale
    STR_TARE_SCALE,
    STR_SCALE_NOT_CALIBRATED,
    STR_SCALE_CAL,
    STR_EMPTY_SCALE,
    STR_PLACE_WEIGHT,
    STR_REMOVE_WEIGHT,
    STR_COMPLETED,
    STR_CALIBRATION_ERROR,
    STR_HX711_NOT_FOUND,

    // NFC / Spool
    STR_READING,
    STR_SPOOL_TAG,
    STR_WEIGHING,
    STR_WEIGHT_STABLE,
    STR_SENDING,
    STR_DETECTING_TAG,

    // Connection / API
    STR_NOT_REGISTERED,
    STR_API_CONN_LOST,
    STR_API_ERROR,
    STR_API_OFFLINE,
    STR_WEIGHT_SENT_REST,

    // Errors
    STR_FAILURE,
    STR_FAILURE_EXCL,
    STR_NO_RFID_BOARD,

    // WiFi
    STR_WIFI_CONFIG_MODE,
    STR_WIFI_NOT_CONNECTED,
    STR_WIFI_RECONNECTING,

    // OTA
    STR_UPDATE,
    STR_DOWNLOAD,

    // No-Scale mode
    STR_NOSCALE_MODE,
    STR_NOSCALE_PROMPT,

    STR_COUNT  // must be last
};

// Current language (default: English)
extern Lang currentLang;

// Get translated string by ID
const char* tr(StringID id);

// Load language setting from NVS
void loadLanguage();

// Save language setting to NVS
void saveLanguage(Lang lang);

// Get current language as string ("en" or "de")
const char* getLangCode();

#endif
