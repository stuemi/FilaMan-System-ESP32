#ifndef COMMONFS_H
#define COMMONFS_H

#include <Arduino.h>
#include <LittleFS.h>

String readFile(const char* filename);
void initializeFileSystem();

#endif
