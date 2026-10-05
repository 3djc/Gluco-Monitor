// LittleFS on the host: files live under $SIM_DATA_DIR (default ./data) so settings persist.
#pragma once
#include <Arduino.h>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

class File
{
public:
    File() {}
    explicit File(FILE *fp) : f(fp) {}
    operator bool() const { return f != nullptr; }
    size_t print(const String &s) { return f ? fwrite(s.c_str(), 1, s.length(), f) : 0; }
    String readString();
    void close()
    {
        if (f)
            fclose(f);
        f = nullptr;
    }

private:
    FILE *f = nullptr;
};
