// Stand-ins for the parts of the firmware that talk to the outside world:
// LibreLinkUp / Dexcom Share (replaced by a synthetic glucose curve) and the web server.
// Everything else (screens, settings, WiFi setup flow, ...) runs the real firmware code.
#include <Arduino.h>
#include "Config.h"
#include "Heure.h"
#include "Dexcom.h"
#include "Libreview.h"
#include "Server.h"
#include "Ecran/pageMessages.h"
#include "Langues/Langue.h"

int sim_glucose_offset = 0;   // Up/Down keys in the window
bool sim_force_reading = false;

// Smooth, meal-like curve in mg/dL for a given unix time.
static int curve(time_t t)
{
    double h = fmod((double)t, 86400.0) / 3600.0;
    double v = 125 + 55 * sin(2 * M_PI * h / 4.3) + 28 * sin(2 * M_PI * h / 1.7 + 1.0);
    return (int)constrain((int)lround(v), 40, 400);
}

static int8_t arrowFromSlope(double mgPerMin)
{
    if (mgPerMin <= -3) return -1; // double down
    if (mgPerMin <= -2) return 1;
    if (mgPerMin <= -1) return 2;
    if (mgPerMin < 1) return 3;
    if (mgPerMin < 2) return 4;
    if (mgPerMin < 3) return 5;
    return 6;                      // double up
}

static void pushPoint(int value, time_t t)
{
    if (pointCountGly >= MAX_POINTS)
    {
        for (int i = 1; i < pointCountGly; i++)
        {
            glucoseValues[i - 1] = glucoseValues[i];
            glucoseHeure[i - 1] = glucoseHeure[i];
        }
        pointCountGly--;
    }
    glucoseValues[pointCountGly] = value;
    glucoseHeure[pointCountGly] = (unsigned long)t;
    pointCountGly++;
}

static void fillHistory(int stepMin, int spanHours)
{
    time_t now;
    time(&now);
    pointCountGly = 0;
    for (long back = spanHours * 60L; back > 0; back -= stepMin)
    {
        time_t t = now - back * 60;
        pushPoint(curve(t) + (back > 30 ? 0 : sim_glucose_offset), t); // offset only touches "now"
    }
}

static void newReading(int stepMin, int spanHours)
{
    time_t now;
    time(&now);
    if (pointCountGly == 0)
        fillHistory(stepMin, spanHours);
    int v = curve(now) + sim_glucose_offset;
    v = constrain(v, 40, 400);
    double slope = (v - (curve(now - 900) + sim_glucose_offset)) / 15.0;

    GlycemieVal = v;
    Glycemie = String(v);
    TrendArrow = arrowFromSlope(slope);
    lastGlyUnixTime = (unsigned long)now;
    pushPoint(v, now);
    lastReceptionGlycMillis = millis();
    lastGlycOkMillis = millis();
    EcranPrintln(HEURE + T("LastGlyco") + formatGlucoseValue(GlycemieVal) + " " + getGlucoseUnitLabel() + " " + T("le") + unixToTimestamp(now));
}

static void simSensor(bool haveAccount, int stepMin, int spanHours, unsigned long periodMs)
{
    if (sim_force_reading)
    {
        sim_force_reading = false;
        lastDemandeGlycMillis = 0;
    }
    if (millis() - lastReceptionGlycMillis > periodMs || lastDemandeGlycMillis == 0)
    {
        lastDemandeGlycMillis = millis();
        if (haveAccount)
            newReading(stepMin, spanHours);
        else
            EcranPrintln(T("LinkUpIndefini"));
        lastReceptionGlycMillis = millis();
    }
}

void LectureGlycemie() { simSensor(libreEmail != "" && librePass != "", 15, 12, 60000); }
void LectureDexcom() { simSensor(dexcomUsername != "" && dexcomPassword != "", 5, 24, 60000); }
bool loginLibreLinkUp() { return true; }
bool loginDexcomShare() { return true; }
void getDexcomReadings() {}
void clearLibreViewCache() {}
void clearDexcomCache() {}

void Init_Server() {}
