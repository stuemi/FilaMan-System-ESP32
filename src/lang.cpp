#include "lang.h"
#include "config.h"
#include <Preferences.h>

Lang currentLang = LANG_EN;

// =====================================================================
// English strings
// =====================================================================
static const char EN_DISPLAY_INIT[]      = "Display init";
static const char EN_WIFI_INIT[]         = "WiFi init";
static const char EN_WEBSERVER_INIT[]    = "Webserver init";
static const char EN_API_INIT[]          = "API init";
static const char EN_NFC_INIT[]          = "NFC init";
static const char EN_SEARCHING_SCALE[]   = "Searching scale";
static const char EN_INIT_DONE[]         = "Setup finished";

static const char EN_TARE_SCALE[]        = "TARE Scale";
static const char EN_SCALE_NOT_CAL[]     = "Scale not calibrated";
static const char EN_SCALE_CAL[]         = "Scale Cal.";
static const char EN_EMPTY_SCALE[]       = "Empty Scale";
static const char EN_PLACE_WEIGHT[]      = "Place the weight";
static const char EN_REMOVE_WEIGHT[]     = "Remove weight";
static const char EN_COMPLETED[]         = "Completed";
static const char EN_CAL_ERROR[]         = "Calibration error";
static const char EN_HX711_NOT_FOUND[]   = "HX711 not found";

static const char EN_READING[]           = "Reading";
static const char EN_SPOOL_TAG[]         = "Spool Tag";
static const char EN_WEIGHING[]          = "Weighing...";
static const char EN_WEIGHT_STABLE[]     = "Weight stable";
static const char EN_SENDING[]           = "Sending...";
static const char EN_DETECTING_TAG[]     = "Detecting tag";

static const char EN_NOT_REGISTERED[]    = "Not Registered";
static const char EN_API_CONN_LOST[]     = "API Connection Lost";
static const char EN_API_ERROR[]         = "API Error";
static const char EN_API_OFFLINE[]       = "API offline";
static const char EN_ASSIGN_SPOOL[]      = "Assign Spool in Web-UI";
static const char EN_WEIGHT_SENT_REST[]  = "Weight sent, rest:";

static const char EN_FAILURE[]           = "Failure";
static const char EN_FAILURE_EXCL[]      = "Failure!";
static const char EN_NO_RFID_BOARD[]     = "No RFID Board found";

static const char EN_WIFI_CONFIG[]       = "WiFi Config Mode";
static const char EN_WIFI_NOT_CONN[]     = "WiFi not connected Check Portal";
static const char EN_WIFI_RECONN[]       = "WiFi reconnecting";

static const char EN_UPDATE[]            = "Update";
static const char EN_DOWNLOAD[]          = "Download";
static const char EN_BAMBU_SPOOL_DETECTED[] = "Bambu Spool, use AMS";

// =====================================================================
// German strings
// =====================================================================
static const char DE_DISPLAY_INIT[]      = "Display init";
static const char DE_WIFI_INIT[]         = "WiFi init";
static const char DE_WEBSERVER_INIT[]    = "Webserver init";
static const char DE_API_INIT[]          = "API init";
static const char DE_NFC_INIT[]          = "NFC init";
static const char DE_SEARCHING_SCALE[]   = "Suche Waage";
static const char DE_INIT_DONE[]         = "Setup abgeschlossen";

static const char DE_TARE_SCALE[]        = "Waage tarieren";
static const char DE_SCALE_NOT_CAL[]     = "Waage nicht kalibriert";
static const char DE_SCALE_CAL[]         = "Kalibrieren";
static const char DE_EMPTY_SCALE[]       = "Waage leeren";
static const char DE_PLACE_WEIGHT[]      = "Gewicht auflegen";
static const char DE_REMOVE_WEIGHT[]     = "Gewicht entfernen";
static const char DE_COMPLETED[]         = "Abgeschlossen";
static const char DE_CAL_ERROR[]         = "Kalibrierungsfehler";
static const char DE_HX711_NOT_FOUND[]   = "HX711 nicht gefunden";

static const char DE_READING[]           = "Lesen";
static const char DE_SPOOL_TAG[]         = "Spulen-Tag";
static const char DE_WEIGHING[]          = "Wiegen...";
static const char DE_WEIGHT_STABLE[]     = "Gewicht stabil";
static const char DE_SENDING[]           = "Senden...";
static const char DE_DETECTING_TAG[]     = "Tag erkennen";

static const char DE_NOT_REGISTERED[]    = "Nicht registriert";
static const char DE_API_CONN_LOST[]     = "API-Verbindung weg";
static const char DE_API_ERROR[]         = "API-Fehler";
static const char DE_API_OFFLINE[]       = "API offline";
static const char DE_ASSIGN_SPOOL[]      = "Spule im Web-UI zuweisen";
static const char DE_WEIGHT_SENT_REST[]  = "Gesendet, Rest:";

static const char DE_FAILURE[]           = "Fehler";
static const char DE_FAILURE_EXCL[]      = "Fehler!";
static const char DE_NO_RFID_BOARD[]     = "Kein RFID-Board";

static const char DE_WIFI_CONFIG[]       = "WiFi Konfig-Modus";
static const char DE_WIFI_NOT_CONN[]     = "WiFi nicht verbunden Portal prüfen";
static const char DE_WIFI_RECONN[]       = "WiFi Neuverbindung";

static const char DE_UPDATE[]            = "Update";
static const char DE_DOWNLOAD[]          = "Download";
static const char DE_BAMBU_SPOOL_DETECTED[] = "Bambu Spule, AMS nutzen";

// =====================================================================
// String table: [StringID][Lang]
// =====================================================================
static const char* const stringTable[STR_COUNT][LANG_COUNT] = {
    // Boot / Init
    { EN_DISPLAY_INIT,     DE_DISPLAY_INIT },
    { EN_WIFI_INIT,        DE_WIFI_INIT },
    { EN_WEBSERVER_INIT,   DE_WEBSERVER_INIT },
    { EN_API_INIT,         DE_API_INIT },
    { EN_NFC_INIT,         DE_NFC_INIT },
    { EN_SEARCHING_SCALE,  DE_SEARCHING_SCALE },
    { EN_INIT_DONE,        DE_INIT_DONE },

    // Scale
    { EN_TARE_SCALE,       DE_TARE_SCALE },
    { EN_SCALE_NOT_CAL,    DE_SCALE_NOT_CAL },
    { EN_SCALE_CAL,        DE_SCALE_CAL },
    { EN_EMPTY_SCALE,      DE_EMPTY_SCALE },
    { EN_PLACE_WEIGHT,     DE_PLACE_WEIGHT },
    { EN_REMOVE_WEIGHT,    DE_REMOVE_WEIGHT },
    { EN_COMPLETED,        DE_COMPLETED },
    { EN_CAL_ERROR,        DE_CAL_ERROR },
    { EN_HX711_NOT_FOUND,  DE_HX711_NOT_FOUND },

    // NFC / Spool
    { EN_READING,          DE_READING },
    { EN_SPOOL_TAG,        DE_SPOOL_TAG },
    { EN_WEIGHING,         DE_WEIGHING },
    { EN_WEIGHT_STABLE,    DE_WEIGHT_STABLE },
    { EN_SENDING,          DE_SENDING },
    { EN_DETECTING_TAG,    DE_DETECTING_TAG },

    // Connection / API
    { EN_NOT_REGISTERED,   DE_NOT_REGISTERED },
    { EN_API_CONN_LOST,    DE_API_CONN_LOST },
    { EN_API_ERROR,        DE_API_ERROR },
    { EN_API_OFFLINE,      DE_API_OFFLINE },
    { EN_ASSIGN_SPOOL,     DE_ASSIGN_SPOOL },
    { EN_WEIGHT_SENT_REST, DE_WEIGHT_SENT_REST },

    // Errors
    { EN_FAILURE,          DE_FAILURE },
    { EN_FAILURE_EXCL,     DE_FAILURE_EXCL },
    { EN_NO_RFID_BOARD,    DE_NO_RFID_BOARD },

    // WiFi
    { EN_WIFI_CONFIG,      DE_WIFI_CONFIG },
    { EN_WIFI_NOT_CONN,    DE_WIFI_NOT_CONN },
    { EN_WIFI_RECONN,      DE_WIFI_RECONN },

    // OTA
    { EN_UPDATE,           DE_UPDATE },
    { EN_DOWNLOAD,         DE_DOWNLOAD },

    // Special
    { EN_BAMBU_SPOOL_DETECTED, DE_BAMBU_SPOOL_DETECTED },
};

const char* tr(StringID id) {
    if (id >= STR_COUNT) return "???";
    return stringTable[id][currentLang];
}

void loadLanguage() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, true);
    currentLang = (Lang)preferences.getUChar(NVS_KEY_LANGUAGE, LANG_EN);
    if (currentLang >= LANG_COUNT) currentLang = LANG_EN;
    preferences.end();
    Serial.printf("Language loaded: %s\n", getLangCode());
}

void saveLanguage(Lang lang) {
    if (lang >= LANG_COUNT) return;
    currentLang = lang;
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, false);
    preferences.putUChar(NVS_KEY_LANGUAGE, (uint8_t)lang);
    preferences.end();
    Serial.printf("Language saved: %s\n", getLangCode());
}

const char* getLangCode() {
    return (currentLang == LANG_DE) ? "de" : "en";
}
