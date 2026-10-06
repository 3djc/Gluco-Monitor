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
extern int16_t PageActu;          // Ecran/Gestion.cpp
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
static int g_scale = 1;
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static SDL_Texture *g_texture = nullptr;
static int g_view_w = 0, g_view_h = 0;
static SDL_Rect g_dst = {0, 0, 0, 0}; // where the screen is drawn inside the window (renderer pixels)
static float g_dstScale = 1.0f;
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
        SDL_SetWindowSize(g_window, w * g_scale, h * g_scale);
        g_view_w = w;
        g_view_h = h;
    }
    std::vector<uint32_t> px;
    renderView(px, w, h, true);
    SDL_UpdateTexture(g_texture, nullptr, px.data(), w * 4);
    // Fit the screen into the window keeping the aspect ratio. This is done by hand (not with
    // SDL_RenderSetLogicalSize) because SDL silently rescales mouse events when a logical size is
    // set, which would make the window->screen mapping below apply twice.
    int ow, oh;
    SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
    float sc = std::min((float)ow / w, (float)oh / h);
    g_dst.w = (int)(w * sc);
    g_dst.h = (int)(h * sc);
    g_dst.x = (ow - g_dst.w) / 2;
    g_dst.y = (oh - g_dst.h) / 2;
    g_dstScale = sc;
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, &g_dst);
    SDL_RenderPresent(g_renderer);
}

// ---------------------------------------------------------------------------------------
// Input: mouse -> touch point, keyboard shortcuts, optional script
// ---------------------------------------------------------------------------------------
static bool g_touchDown = false;
static uint32_t g_releaseAt = 0; // a tap is held for a minimum time: the firmware only polls touch every 20 ms
static int g_touchX = 0, g_touchY = 0; // in the logical (rotated) screen coordinates
static bool g_debugInput = false;

// The firmware recognises a swipe as >50 px (pages) / >10 px (fixed pages) of movement between two
// touch polls 20 ms apart - a fast finger flick. A mouse drag is far slower, so drags are amplified
// beyond a small dead zone (taps with a little jitter stay taps), and the arrow keys inject a flick.
static const int DRAG_DEADZONE = 12;
static const int DRAG_GAIN = 6;
static int g_anchorX = 0, g_anchorY = 0;
static int g_flickDx = 0, g_flickDy = 0;
static uint32_t g_flickAt = 0;

static int16_t g_releasePage = 0;
static uint32_t g_pressAt = 0;
static void touchPress()
{
    g_pressAt = SDL_GetTicks();
    g_touchDown = true;
}
static void touchRelease()
{
    g_touchDown = false;
    g_releasePage = PageActu;
    // Minimum press time, counted from the press: 50 ms so the firmware's 20 ms poll can't miss a
    // quick tap; 340 ms on the settings menu (page 1), which only reacts to a press held > 300 ms.
    // Kept short otherwise so a tap doesn't outlive the firmware's key-highlight delay and repeat.
    uint32_t minHold = PageActu == 1 ? 340 : 50;
    uint32_t now = SDL_GetTicks();
    g_releaseAt = std::max(now, g_pressAt + minHold);
}
static bool touchActive()
{
    if (g_touchDown)
        return true;
    // Don't let the held-over tap fall through onto the next page after a navigation.
    return SDL_GetTicks() < g_releaseAt && PageActu == g_releasePage;
}

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

static void startFlick(int dx, int dy)
{
    g_flickDx = dx;
    g_flickDy = dy;
    g_flickAt = SDL_GetTicks() ? SDL_GetTicks() : 1;
}

static void updateFlick()
{
    if (!g_flickAt)
        return;
    int vw, vh;
    viewSize(vw, vh);
    uint32_t t = SDL_GetTicks() - g_flickAt;
    if (t < 40) // finger lands in the middle...
    {
        g_touchX = vw / 2;
        g_touchY = vh / 2;
        g_touchDown = true;
    }
    else if (t < 100) // ...and moves quickly
    {
        g_touchX = vw / 2 + g_flickDx;
        g_touchY = vh / 2 + g_flickDy;
        g_touchDown = true;
    }
    else
    {
        g_flickAt = 0;
        touchRelease();
    }
}

static void handleKey(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_RIGHT: // next page (finger moves left); on sub-pages: back to the settings menu
        startFlick(-100, 0);
        break;
    case SDLK_LEFT:
        startFlick(100, 0);
        break;
    case SDLK_PAGEDOWN: // scroll content up
        startFlick(0, -45);
        break;
    case SDLK_PAGEUP:
        startFlick(0, 45);
        break;
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

static void screenToWindow(int x, int y, int &wx, int &wy);
static void runScript()
{
    uint32_t now = SDL_GetTicks();
    while (g_scriptPos < g_script.size() && g_script[g_scriptPos].at <= now)
    {
        const ScriptEvent &ev = g_script[g_scriptPos++];
        if (ev.action == "down" || ev.action == "move")
        {
            sscanf(ev.arg.c_str(), "%d,%d", &g_touchX, &g_touchY);
            if (!g_touchDown)
                touchPress();
        }
        else if (ev.action == "up")
            touchRelease();
        else if (ev.action == "mdown" || ev.action == "mmove" || ev.action == "mup")
        {
            // Pushes a genuine SDL mouse event (screen coords scaled to window coords): exercises the
            // same path as a real mouse. Needs a window, e.g. SDL_VIDEODRIVER=dummy.
            int x = 0, y = 0;
            sscanf(ev.arg.c_str(), "%d,%d", &x, &y);
            SDL_Event se;
            memset(&se, 0, sizeof(se));
            int wx, wy;
            screenToWindow(x, y, wx, wy);
            if (ev.action == "mmove")
            {
                se.type = SDL_MOUSEMOTION;
                se.motion.windowID = SDL_GetWindowID(g_window); // like a real event: SDL's own event watchers see it
                se.motion.state = SDL_BUTTON_LMASK;
                se.motion.x = wx;
                se.motion.y = wy;
            }
            else
            {
                se.type = ev.action == "mdown" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                se.button.windowID = SDL_GetWindowID(g_window);
                se.button.button = SDL_BUTTON_LEFT;
                se.button.x = wx;
                se.button.y = wy;
            }
            SDL_PushEvent(&se);
        }
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
            else if (ev.arg == "left") handleKey(SDLK_LEFT);
            else if (ev.arg == "right") handleKey(SDLK_RIGHT);
            else if (ev.arg == "pageup") handleKey(SDLK_PAGEUP);
            else if (ev.arg == "pagedown") handleKey(SDLK_PAGEDOWN);
        }
        else if (ev.action == "quit")
            exit(0);
    }
}

// Window coordinates (points, as in mouse events) <-> screen coordinates (480x320 for rotation 1).
static void windowToScreen(int wx, int wy, int &lx, int &ly)
{
    int vw, vh, ww = 0, wh = 0, ow = 0, oh = 0;
    viewSize(vw, vh);
    if (g_window)
        SDL_GetWindowSize(g_window, &ww, &wh);
    if (g_renderer)
        SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
    float px = wx, py = wy; // renderer pixels (differs from points on high-DPI displays)
    if (ww > 0 && wh > 0 && ow > 0 && oh > 0)
    {
        px = wx * (float)ow / ww;
        py = wy * (float)oh / wh;
    }
    if (g_dst.w > 0)
    {
        lx = (int)((px - g_dst.x) / g_dstScale);
        ly = (int)((py - g_dst.y) / g_dstScale);
    }
    else
    {
        lx = (int)px;
        ly = (int)py;
    }
    lx = constrain(lx, 0, vw - 1);
    ly = constrain(ly, 0, vh - 1);
}

static void screenToWindow(int x, int y, int &wx, int &wy)
{
    int ww = 0, wh = 0, ow = 0, oh = 0;
    if (g_window)
        SDL_GetWindowSize(g_window, &ww, &wh);
    if (g_renderer)
        SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
    float px = g_dst.x + x * g_dstScale, py = g_dst.y + y * g_dstScale;
    wx = (ow > 0 && ww > 0) ? (int)(px * ww / ow) : (int)px;
    wy = (oh > 0 && wh > 0) ? (int)(py * wh / oh) : (int)py;
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
                    windowToScreen(e.button.x, e.button.y, g_anchorX, g_anchorY);
                    g_touchX = g_anchorX;
                    g_touchY = g_anchorY;
                    touchPress();
                    if (g_debugInput)
                    {
                        int ww, wh, ow, oh;
                        SDL_GetWindowSize(g_window, &ww, &wh);
                        SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
                        fprintf(stderr, "[sim] mouse down window=(%d,%d) -> screen=(%d,%d)  windowSize=%dx%d rendererOutput=%dx%d view=%dx%d\n",
                                e.button.x, e.button.y, g_touchX, g_touchY, ww, wh, ow, oh, g_view_w, g_view_h);
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    touchRelease();
                    if (g_debugInput)
                        fprintf(stderr, "[sim] mouse up\n");
                }
                break;
            case SDL_MOUSEMOTION:
                if (g_touchDown && !g_flickAt && !(e.motion.state & SDL_BUTTON_LMASK))
                    touchRelease(); // missed the button-up (e.g. released outside the window)
                if (g_touchDown && !g_flickAt && (e.motion.state & SDL_BUTTON_LMASK))
                {
                    int mx, my, vw, vh;
                    windowToScreen(e.motion.x, e.motion.y, mx, my);
                    viewSize(vw, vh);
                    auto amplify = [](int anchor, int now)
                    {
                        int d = now - anchor, a = abs(d) - DRAG_DEADZONE;
                        return a <= 0 ? anchor : anchor + (d < 0 ? -1 : 1) * a * DRAG_GAIN;
                    };
                    g_touchX = constrain(amplify(g_anchorX, mx), 0, vw - 1);
                    g_touchY = constrain(amplify(g_anchorY, my), 0, vh - 1);
                }
                break;
            case SDL_KEYDOWN:
                if (!e.key.repeat)
                    handleKey(e.key.keysym.sym);
                break;
            }
        }
    }
    runScript();
    updateFlick();
    if (g_dirty && SDL_GetTicks() - g_lastPresent >= 16)
        present();
}

// "I2C" read of the touch controller. Logical screen coordinates are converted back to the raw
// panel coordinates the firmware's getTouchPoint() expects (inverse of its rotation switch).
size_t TwoWire::requestFrom(uint8_t, size_t len)
{
    pump();
    memset(buf, 0, sizeof(buf));
    if (touchActive())
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
    g_debugInput = getenv("SIM_DEBUG") != nullptr;
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
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"); // don't swallow the click that focuses the window
        g_window = SDL_CreateWindow("Gluco-Monitor simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    PANEL_H * g_scale, PANEL_W * g_scale, SDL_WINDOW_RESIZABLE);
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!g_renderer)
            g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RaiseWindow(g_window);
        fprintf(stderr, "[sim] click = touch | drag or Left/Right = swipe pages | PgUp/PgDn = scroll | Up/Down = glucose +/-10 | Home = reset | S = screenshot | R = restart | Q = quit\n");
    }

    setup();
    for (;;)
        loop();
}
