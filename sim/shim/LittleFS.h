#pragma once
#include "FS.h"

class LittleFSClass
{
public:
    bool begin(bool = false);
    bool exists(const char *path);
    File open(const char *path, const char *mode = FILE_READ);
    bool remove(const char *path);
};
extern LittleFSClass LittleFS;
