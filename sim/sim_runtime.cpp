// Desktop runtime for the Gluco-Monitor firmware: provides the Arduino/ESP32 services the
// firmware expects (time, Serial, touch over "I2C", WiFi, LittleFS, SNTP) and an SDL window
// that plays the part of the 320x480 AXS15231B touch panel.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <esp_sntp.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

// Firmware entry points and the few globals the simulator needs to read/write.
void setup();
void loop();
extern int8_t rotation;           // Ecran/Gestion.cpp
extern int sim_glucose_offset;    // sim_data.cpp
extern bool sim_force_reading;    // sim_data.cpp

// ---------------------------------------------------------------------------------------
// Panel + window
// ---------------------------------------------------------------------------------------
static const int PANEL_W = 320, PANEL_H = 480; // native (portrait) panel
static uint16_t g_panel[PANEL_W * PANEL_H];
static bool g_dirty = true;
static uint32_t g_backlight = 255;

static bool g_headless = false;
static int g_scale = 2;
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static SDL_Texture *g_texture = nullptr;
static int g_view_w = 0, g_view_h = 0;
static uint32_t g_lastPresent = 0;
static int g_shots = 0;

static char **g_argv = nullptr;

bool Arduino_AXS15231B::begin(int32_t) { return true; }

void Arduino_AXS15231B::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h)
{
    for (int16_t j = 0; j < h; j++)
    {
        int py = y + j;
        if (py < 0 || py >= PANEL_H)
            continue;
        for (int16_t i = 0; i < w; i++)
        {
            int px = x + i;
            if (px < 0 || px >= PANEL_W)
                continue;
            g_panel[py * PANEL_W + px] = bitmap[j * w + i];
        }
    }
    g_dirty = true;
}

// Panel pixel -> what the user sees. Inverse of Arduino_Canvas::writePixelPreclipped().
static void viewSize(int &w, int &h)
{
    bool landscape = (rotation == 1 || rotation == 3);
    w = landscape ? PANEL_H : PANEL_W;
    h = landscape ? PANEL_W : PANEL_H;
}

static void renderView(std::vector<uint32_t> &out, int w, int h, bool applyBacklight)
{
    out.assign((size_t)w * h, 0xFF000000);
    for (int row = 0; row < PANEL_H; row++)
    {
        for (int col = 0; col < PANEL_W; col++)
        {
            int x, y;
            switch (rotation)
            {
            case 1: x = row;               y = PANEL_W - 1 - col; break;
            case 2: x = PANEL_W - 1 - col; y = PANEL_H - 1 - row; break;
            case 3: x = PANEL_H - 1 - row; y = col;               break;
            default: x = col;              y = row;               break;
            }
            uint16_t c = g_panel[row * PANEL_W + col];
            uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
            uint32_t b = (c & 0x1F) * 255 / 31;
            if (applyBacklight)
            {
                r = r * g_backlight / 255;
                g = g * g_backlight / 255;
                b = b * g_backlight / 255;
            }
            out[(size_t)y * w + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

static void saveScreenshot(const char *path)
{
    int w, h;
    viewSize(w, h);
    std::vector<uint32_t> px;
    renderView(px, w, h, false);
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(px.data(), w, h, 32, w * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (s)
    {
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0); // plain 24-bit BMP opens everywhere
        SDL_SaveBMP(rgb ? rgb : s, path);
        if (rgb)
            SDL_FreeSurface(rgb);
        SDL_FreeSurface(s);
        fprintf(stderr, "[sim] screenshot saved: %s\n", path);
    }
}

static void present()
{
    g_lastPresent = SDL_GetTicks();
    g_dirty = false;
    if (g_headless || !g_renderer)
        return;
    int w, h;
    viewSize(w, h);
    if (w != g_view_w || h != g_view_h)
    {
        if (g_texture)
            SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_RenderSetLogicalSize(g_renderer, w, h);
        SDL_SetWindowSize(g_window, w * g_scale, h * g_scale);
        g_view_w = w;
        g_view_h = h;
    }
    std::vector<uint32_t> px;
    renderView(px, w, h, true);
    SDL_UpdateTexture(g_texture, nullptr, px.data(), w * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

// ---------------------------------------------------------------------------------------
// Input: mouse -> touch point, keyboard shortcuts, optional script
// ---------------------------------------------------------------------------------------
static bool g_touchDown = false;
static int g_touchX = 0, g_touchY = 0; // in the logical (rotated) screen coordinates

struct ScriptEvent
{
    uint32_t at;
    std::string action;
    std::string arg;
};
static std::vector<ScriptEvent> g_script;
static size_t g_scriptPos = 0;

static void parseScript(const std::string &s)
{
    size_t i = 0;
    while (i < s.size())
    {
        size_t e = s.find(';', i);
        if (e == std::string::npos)
            e = s.size();
        std::string item = s.substr(i, e - i);
        i = e + 1;
        size_t c = item.find(':');
        if (c == std::string::npos)
            continue;
        ScriptEvent ev;
        ev.at = (uint32_t)atol(item.substr(0, c).c_str());
        std::string rest = item.substr(c + 1);
        size_t eq = rest.find('=');
        ev.action = rest.substr(0, eq);
        ev.arg = eq == std::string::npos ? "" : rest.substr(eq + 1);
        g_script.push_back(ev);
    }
}

static void handleKey(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_UP:
        sim_glucose_offset += 10;
        sim_force_reading = true;
        break;
    case SDLK_DOWN:
        sim_glucose_offset -= 10;
        sim_force_reading = true;
        break;
    case SDLK_HOME:
        sim_glucose_offset = 0;
        sim_force_reading = true;
        break;
    case SDLK_s:
    {
        char name[64];
        snprintf(name, sizeof(name), "screenshot-%d.bmp", ++g_shots);
        saveScreenshot(name);
        break;
    }
    case SDLK_r:
        ESP.restart();
        break;
    case SDLK_q:
    case SDLK_ESCAPE:
        exit(0);
    }
}

static void runScript()
{
    uint32_t now = SDL_GetTicks();
    while (g_scriptPos < g_script.size() && g_script[g_scriptPos].at <= now)
    {
        const ScriptEvent &ev = g_script[g_scriptPos++];
        if (ev.action == "down" || ev.action == "move")
        {
            sscanf(ev.arg.c_str(), "%d,%d", &g_touchX, &g_touchY);
            g_touchDown = true;
        }
        else if (ev.action == "up")
            g_touchDown = false;
        else if (ev.action == "shot")
        {
            if (g_dirty)
                present();
            saveScreenshot(ev.arg.c_str());
        }
        else if (ev.action == "key")
        {
            if (ev.arg == "up") handleKey(SDLK_UP);
            else if (ev.arg == "down") handleKey(SDLK_DOWN);
            else if (ev.arg == "home") handleKey(SDLK_HOME);
        }
        else if (ev.action == "quit")
            exit(0);
    }
}

static void toLogical(int wx, int wy, int &lx, int &ly)
{
    float fx = wx, fy = wy;
    if (g_renderer)
        SDL_RenderWindowToLogical(g_renderer, wx, wy, &fx, &fy);
    lx = (int)fx;
    ly = (int)fy;
}

static void pump()
{
    if (!g_headless)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
                exit(0);
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    toLogical(e.button.x, e.button.y, g_touchX, g_touchY);
                    g_touchDown = true;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT)
                    g_touchDown = false;
                break;
            case SDL_MOUSEMOTION:
                if (g_touchDown)
                    toLogical(e.motion.x, e.motion.y, g_touchX, g_touchY);
                break;
            case SDL_KEYDOWN:
                if (!e.key.repeat)
                    handleKey(e.key.keysym.sym);
                break;
            }
        }
    }
    runScript();
    if (g_dirty && SDL_GetTicks() - g_lastPresent >= 16)
        present();
}

// "I2C" read of the touch controller. Logical screen coordinates are converted back to the raw
// panel coordinates the firmware's getTouchPoint() expects (inverse of its rotation switch).
size_t TwoWire::requestFrom(uint8_t, size_t len)
{
    pump();
    memset(buf, 0, sizeof(buf));
    if (g_touchDown)
    {
        int rawX = 0, rawY = 0;
        switch (rotation)
        {
        case 0: rawX = g_touchX;           rawY = g_touchY;           break;
        case 1: rawX = PANEL_W - g_touchY; rawY = g_touchX;           break;
        case 2: rawX = PANEL_W - g_touchX; rawY = PANEL_H - g_touchY; break;
        case 3: rawX = g_touchY;           rawY = PANEL_H - g_touchX; break;
        }
        rawX = constrain(rawX, 0, PANEL_W);
        rawY = constrain(rawY, 0, PANEL_H);
        buf[1] = 1;
        buf[2] = (rawX >> 8) & 0x0F;
        buf[3] = rawX & 0xFF;
        buf[4] = (rawY >> 8) & 0x0F;
        buf[5] = rawY & 0xFF;
    }
    pos = 0;
    n = len < sizeof(buf) ? len : sizeof(buf);
    return n;
}
int TwoWire::read() { return pos < n ? buf[pos++] : -1; }
TwoWire Wire;

// ---------------------------------------------------------------------------------------
// Arduino core services
// ---------------------------------------------------------------------------------------
static struct timespec g_t0 = {0, 0};

unsigned long millis()
{
    if (g_t0.tv_sec == 0)
        clock_gettime(CLOCK_MONOTONIC, &g_t0);
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long)((t.tv_sec - g_t0.tv_sec) * 1000L + (t.tv_nsec - g_t0.tv_nsec) / 1000000L);
}
unsigned long micros() { return millis() * 1000UL; }
int64_t esp_timer_get_time() { return (int64_t)micros(); }

void delay(uint32_t ms)
{
    uint32_t end = millis() + ms;
    for (;;)
    {
        pump();
        uint32_t now = millis();
        if (now >= end)
            break;
        uint32_t step = end - now;
        usleep((step > 5 ? 5 : step) * 1000);
    }
}
void delayMicroseconds(uint32_t us) { usleep(us); }

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    const long run = in_max - in_min;
    if (run == 0)
        return 0;
    return (x - in_min) * (out_max - out_min) / run + out_min;
}

uint32_t analogReadMilliVolts(uint8_t) { return 2980 + (rand() % 8); } // ~3.96 V battery after the divider
void ledcWrite(uint8_t, uint32_t duty) { g_backlight = duty > 255 ? 255 : duty; g_dirty = true; }

// Serial <-> terminal
SimSerial Serial;
size_t SimSerial::write(uint8_t c) { return fwrite(&c, 1, 1, stdout); }
size_t SimSerial::write(const uint8_t *b, size_t n) { fflush(stdout); return fwrite(b, 1, n, stdout); }
static int g_peek = -1;
int SimSerial::available()
{
    pump();
    if (g_peek >= 0)
        return 1;
    struct pollfd p = {0, POLLIN, 0};
    if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN))
    {
        unsigned char c;
        if (::read(0, &c, 1) == 1)
        {
            g_peek = c;
            return 1;
        }
    }
    return 0;
}
int SimSerial::read()
{
    if (available() == 0)
        return -1;
    int c = g_peek;
    g_peek = -1;
    return c;
}

EspClass ESP;
void EspClass::restart()
{
    fprintf(stderr, "[sim] ESP.restart()\n");
    fflush(stdout);
    execv(g_argv[0], g_argv);
    exit(1);
}

ArduinoOTAClass ArduinoOTA;

// SNTP: the host clock is already right, so "sync" immediately.
static sntp_sync_time_cb_t g_sntpCb = nullptr;
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t cb) { g_sntpCb = cb; }
void configTzTime(const char *tz, const char *, const char *)
{
    setenv("TZ", tz, 1);
    tzset();
    if (g_sntpCb)
        g_sntpCb(nullptr);
}

// ---------------------------------------------------------------------------------------
// WiFi: pretends to find a few networks and "connects" 1.5 s after begin().
// SIM_WIFI_FAIL=1 makes the connection never succeed.
// ---------------------------------------------------------------------------------------
static const char *kNets[] = {"Home-WiFi", "Livebox-4F2A", "FreeWifi_secure", "Neighbour_5G", "Cafe Guest", "ESP32-Setup"};
static const int kRssi[] = {-48, -61, -70, -78, -84, -90};
static unsigned long g_connectAt = 0;
WiFiClass WiFi;

int WiFiClass::status()
{
    if (getenv("SIM_WIFI_FAIL"))
        return WL_DISCONNECTED;
    return (g_connectAt && millis() >= g_connectAt) ? WL_CONNECTED : WL_DISCONNECTED;
}
void WiFiClass::begin(const char *, const char *) { g_connectAt = millis() + 1500; }
void WiFiClass::begin(const char *, const char *, int32_t, const uint8_t *) { g_connectAt = millis() + 1500; }
int16_t WiFiClass::scanNetworks()
{
    delay(800);
    return sizeof(kNets) / sizeof(kNets[0]);
}
String WiFiClass::SSID(uint8_t i) { return String(kNets[i % 6]); }
int32_t WiFiClass::RSSI(uint8_t i) { return kRssi[i % 6]; }
String WiFiClass::BSSIDstr(uint8_t i)
{
    char b[24];
    snprintf(b, sizeof(b), "A4:CF:12:00:00:%02X", i);
    return String(b);
}
uint8_t *WiFiClass::BSSID(uint8_t i)
{
    static uint8_t m[6];
    uint8_t v[6] = {0xA4, 0xCF, 0x12, 0, 0, i};
    memcpy(m, v, 6);
    return m;
}

// ---------------------------------------------------------------------------------------
// LittleFS -> host directory
// ---------------------------------------------------------------------------------------
LittleFSClass LittleFS;
static std::string fsPath(const char *p)
{
    const char *dir = getenv("SIM_DATA_DIR");
    return std::string(dir ? dir : "data") + p;
}
bool LittleFSClass::begin(bool)
{
    const char *dir = getenv("SIM_DATA_DIR");
    mkdir(dir ? dir : "data", 0755);
    return true;
}
bool LittleFSClass::exists(const char *path) { return access(fsPath(path).c_str(), F_OK) == 0; }
File LittleFSClass::open(const char *path, const char *mode) { return File(fopen(fsPath(path).c_str(), mode)); }
bool LittleFSClass::remove(const char *path) { return unlink(fsPath(path).c_str()) == 0; }
String File::readString()
{
    String s;
    if (!f)
        return s;
    char b[256];
    size_t n;
    while ((n = fread(b, 1, sizeof(b) - 1, f)) > 0)
    {
        b[n] = 0;
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------------------------------
// <stdlib_noniso.h> (not in libc on macOS)
// ---------------------------------------------------------------------------------------
extern "C"
{
    static char *toBase(unsigned long long v, bool neg, char *s, int radix)
    {
        char tmp[70];
        int i = 0;
        if (radix < 2 || radix > 36)
            radix = 10;
        do
        {
            int d = v % radix;
            tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10;
            v /= radix;
        } while (v);
        int j = 0;
        if (neg)
            s[j++] = '-';
        while (i)
            s[j++] = tmp[--i];
        s[j] = 0;
        return s;
    }
    char *itoa(int v, char *s, int r) { return toBase(v < 0 && r == 10 ? -(long long)v : (unsigned)v, v < 0 && r == 10, s, r); }
    char *ltoa(long v, char *s, int r) { return toBase(v < 0 && r == 10 ? -(long long)v : (unsigned long)v, v < 0 && r == 10, s, r); }
    char *lltoa(long long v, char *s, int r) { return toBase(v < 0 && r == 10 ? 0ULL - (unsigned long long)v : (unsigned long long)v, v < 0 && r == 10, s, r); }
    char *utoa(unsigned v, char *s, int r) { return toBase(v, false, s, r); }
    char *ultoa(unsigned long v, char *s, int r) { return toBase(v, false, s, r); }
    char *ulltoa(unsigned long long v, char *s, int r) { return toBase(v, false, s, r); }
    char *dtostrf(double v, signed int width, unsigned int prec, char *s)
    {
        sprintf(s, "%*.*f", width, (int)prec, v);
        return s;
    }
}

// ---------------------------------------------------------------------------------------
// main: Arduino-style setup()/loop()
// ---------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    g_argv = argv;
    millis();
    srand((unsigned)time(nullptr));
    std::string script;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--headless"))
            g_headless = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            g_scale = atoi(argv[++i]);
    }
    if (getenv("SIM_SCRIPT"))
        script = getenv("SIM_SCRIPT");
    parseScript(script);

    if (!g_headless)
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        g_window = SDL_CreateWindow("Gluco-Monitor simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    PANEL_H * g_scale, PANEL_W * g_scale, SDL_WINDOW_RESIZABLE);
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!g_renderer)
            g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        fprintf(stderr, "[sim] click/drag = touch/swipe | Up/Down = glucose +/-10 | Home = reset | S = screenshot | R = restart | Q = quit\n");
    }

    setup();
    for (;;)
        loop();
}
