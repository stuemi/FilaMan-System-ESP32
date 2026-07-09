#ifndef NFC_H
#define NFC_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

typedef enum{
    NFC_IDLE,
    NFC_READING,
    NFC_READ_SUCCESS,
    NFC_READ_ERROR
} nfcReaderStateType;

void startNfc();
void scanRfidTask(void * parameter);

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String activeTagUuid;
extern volatile nfcReaderStateType nfcReaderState;
extern bool tagProcessed;
extern bool isBambuTag;

#endif
