#include "Ecran/pageAccueil.h"
#include "Config.h"
#include <Arduino.h>
#include "Ecran/Gestion.h"
#include "time.h"
#include "Langues/Langue.h"

static bool flipCouleurs = false;
static float dtReponse = 0.0;

void Trace_Gauge(Arduino_Canvas *canva);

// Variation de glycémie (mg/dL) entre la mesure actuelle et une mesure antérieure.
// dureeSec = 0 : la mesure précédente ; sinon la mesure la plus proche de "maintenant - dureeSec" (à 7,5 min près).
static bool variationGlycemie(long dureeSec, int &delta)
{
    if (pointCountGly < 2 || lastGlyUnixTime == 0)
        return false;
    long tActuel = (long)lastGlyUnixTime;
    int meilleur = -1;
    long ecartMin = 1000000L;
    for (int i = pointCountGly - 1; i >= 0; i--)
    {
        long t = (long)glucoseHeure[i];
        if (t >= tActuel)
            continue; // La mesure actuelle (parfois enregistrée plusieurs fois)
        if (dureeSec == 0)
        {
            meilleur = i; // La plus récente des mesures antérieures
            break;
        }
        long ecart = labs(t - (tActuel - dureeSec));
        if (ecart < ecartMin)
        {
            ecartMin = ecart;
            meilleur = i;
        }
    }
    if (meilleur < 0 || (dureeSec > 0 && ecartMin > 450))
        return false;
    delta = GlycemieVal - glucoseValues[meilleur];
    return true;
}

static String formatVariation(bool valide, int deltaMgdl)
{
    if (!valide)
        return "--";
    String S = deltaMgdl > 0 ? "+" : "";
    if (glucoseUnit == GLUCOSE_UNIT_MMOLL)
        S += String(float(deltaMgdl) / 18.0f, 1);
    else
        S += String(deltaMgdl);
    return S;
}

void AccueilInit()
{
}

void AccueiLoop()
{

    CanvaAccueil->fillScreen(RGB565_BLACK);
    CanvaAccueil->setTextColor(RGB565_WHITE);
    int16_t W2 = EcranW / 2;
    int16_t C = EcranH / 2;
    int16_t R0 = EcranH / 3.5;
    int16_t Yh = EcranH / 9;
    uint16_t Couleurs[] = {RGB565_BLUE, RGB565_GREEN, RGB565_ORANGE, RGB565_RED};
    uint16_t CouleursFond[] = {C_bleuFonce, C_vertFonce, C_orangeFonce, C_rougeFonce};
    int16_t glucoseInfoColor = RGB565_WHITE;
    int seuilCoul[] = {0, 70, 180, 300, 400};
    seuilCoul[1] = targetLow;
    seuilCoul[2] = targetHigh;
    if (glucoseUnit == 1)
    { // mmol/L
        seuilCoul[3] = 16 * 18;
        seuilCoul[4] = 22 * 18;
    }
    int idxCoul = 0;
    // HEURE
    if (timeFormat == TIME_FORMAT_12H)
    {
        CanvaAccueil->setFont(u8g2_font_fub30_tf);
        Yh -= 4;
    }
    else
    {
        CanvaAccueil->setFont(u8g2_font_fub35_tf);
    }

    if (HeureValide)
        PrintDroite(CanvaAccueil, Hmn, -1, Yh, 1);

    // Charge Batterie
    if (TensionAlimentation < 3180)
    { // On affiche si batterie branchée
        uint16_t batteryColor = RGB565_GREEN;
        if (TensionAlimentation < 2950)
            batteryColor = RGB565_ORANGE;
        if (TensionAlimentation < 2800)
            batteryColor = RGB565_RED;

        CanvaAccueil->fillRect(0, 0, 20, 7, batteryColor);
        CanvaAccueil->writeFastHLine(0, 3, 24, batteryColor);
        CanvaAccueil->writeFastHLine(0, 4, 24, batteryColor);
    }

    // Affiche Glycemie
    if (Glycemie == "")
    {
        CanvaAccueil->setFont(u8g2_font_helvB18_tf);
        bool hasCredentials = (sensorType == SENSOR_LIBRE && libreEmail.length() >= 4) ||
                              (sensorType == SENSOR_DEXCOM && dexcomUsername.length() >= 4);
        if (ssid.length() == 0 || !hasCredentials)
        {
            PrintCentre(CanvaAccueil, T("ConfNul"), W2, C + 25, 1);
        }
        else
        {
            PrintCentre(CanvaAccueil, T("WaitGluco"), W2, C + 25, 1);
        }
    }
    else
    {
        bool tooOld = AgeGlycemie / 60 > 20;
        if (glucoseColor == GLUCOSE_COULEUR)
        { // Prefere valeur glycémie en couleur
            for (int c = 0; c < 4; c++)
            {
                if (GlycemieVal > seuilCoul[c])
                    idxCoul = c;
            }
            glucoseInfoColor = Couleurs[idxCoul];
        }

        glucoseInfoColor = tooOld ? RGB565(50, 50, 50) : glucoseInfoColor; // On force en gris au dela de 20mn

        if (tooOld)
        {
            CanvaAccueil->setFont(u8g2_font_helvB18_tf);
            // Get text bounds for background rectangle
            int16_t x1, y1;
            uint16_t w, h;
            String text = T("WaitGluco");
            CanvaAccueil->getTextBounds(utf8ToLatin15(text), 0, 0, &x1, &y1, &w, &h);
            // Draw black background rectangle
            int16_t rectX = W2 - w / 2 - 2;
            int16_t rectY = EcranH / 9 - h - 2;
            CanvaAccueil->fillRect(rectX, rectY, w + 4, h + 8, RGB565_BLACK);
            // Draw text
            CanvaAccueil->setTextColor(RGB565_RED);
            PrintCentre(CanvaAccueil, text, W2, EcranH / 9, 1);
        }

        CanvaAccueil->setTextColor(glucoseInfoColor);
        CanvaAccueil->setFont(u8g2_font_inb63_mn);
        PrintCentre(CanvaAccueil, formatGlucoseValue(GlycemieVal), W2, C + 25, 1);

        CanvaAccueil->setFont(u8g2_font_10x20_tf);
        PrintGauche(CanvaAccueil, getGlucoseUnitLabel(), W2 + R0, C + 20, 1);
        glucoseInfoColor = tooOld ? RGB565(50, 50, 50) : RGB565_WHITE; // Couleur de la flèche de tendance

        // Bandeau horizontal : flèche de tendance, variation depuis la dernière mesure, variation sur 15 minutes
        const int16_t BandeY = 44, BandeH = 74;
        CanvaAccueil->fillRoundRect(6, BandeY, EcranW - 12, BandeH, 10, RGB565(24, 24, 24));

        static String legendeDerniere, legende15;
        static int8_t langueLegendes = -100; // T() est coûteux : on ne retraduit que si la langue change
        if (langueLegendes != LaLangue)
        {
            legendeDerniere = T("DeltaLast");
            legende15 = T("Delta15");
            langueLegendes = LaLangue;
        }
        int deltaDerniere = 0, delta15 = 0;
        String sDerniere = formatVariation(variationGlycemie(0, deltaDerniere), deltaDerniere);
        String s15 = formatVariation(variationGlycemie(15 * 60, delta15), delta15);
        uint16_t couleurDelta = tooOld ? RGB565(50, 50, 50) : RGB565_WHITE;
        int16_t XA = 112 + (EcranW - 6 - 112) / 4;     // Centre colonne "dernière mesure"
        int16_t XB = 112 + 3 * (EcranW - 6 - 112) / 4; // Centre colonne "15 minutes"
        CanvaAccueil->drawFastVLine(112, BandeY + 10, BandeH - 20, C_grisMoyen);
        CanvaAccueil->drawFastVLine((XA + XB) / 2, BandeY + 10, BandeH - 20, C_grisMoyen);
        CanvaAccueil->setFont(u8g2_font_helvB14_tf);
        CanvaAccueil->setTextColor(RGB565_GREY);
        PrintCentre(CanvaAccueil, legendeDerniere, XA, BandeY + 22, 1);
        PrintCentre(CanvaAccueil, legende15, XB, BandeY + 22, 1);
        CanvaAccueil->setFont(u8g2_font_fub30_tf);
        CanvaAccueil->setTextColor(couleurDelta);
        PrintCentre(CanvaAccueil, sDerniere, XA, BandeY + 64, 1);
        PrintCentre(CanvaAccueil, s15, XB, BandeY + 64, 1);

        // Flèche tendance, dessinée aux 3/4 de sa taille pour tenir dans le bandeau
        int16_t X0 = 62;
        int16_t Y0 = BandeY + BandeH / 2;
        int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
        int16_t offset = 30;
        bool doubleFleche = false;
        bool flecheDefinie = true;
        switch (TrendArrow)
        {
        case -1: // DoubleDown
            doubleFleche = true;
            // fall through
        case 1: // Flèche vers le bas fort
            x0 = -20;
            y0 = 0;
            x1 = 0;
            y1 = 20;
            x2 = 20;
            y2 = 0;
            x3 = -10;
            y3 = -50;
            x4 = +10;
            y4 = -50;
            break;
        case 2: // Flèche vers le bas
            x0 = 0;
            y0 = 20;
            x1 = 20;
            y1 = 20;
            x2 = 20;
            y2 = 0;
            x3 = -30;
            y3 = -40;
            x4 = -40;
            y4 = -30;
            break;
        case 3: // Flèche horizontale
            x0 = 0;
            y0 = 20;
            x1 = 20;
            y1 = 0;
            x2 = 0;
            y2 = -20;
            x3 = -50;
            y3 = -10;
            x4 = -50;
            y4 = +10;
            break;
        case 4: // Flèche vers le haut
            x0 = 20;
            y0 = 0;
            x1 = 20;
            y1 = -20;
            x2 = 0;
            y2 = -20;
            x3 = -30;
            y3 = +40;
            x4 = -40;
            y4 = +30;
            break;
        case 6: // DoubleUp
            doubleFleche = true;
            // fall through
        case 5: // Flèche vers le haut fort
            x0 = 20;
            y0 = 0;
            x1 = 0;
            y1 = -20;
            x2 = -20;
            y2 = 0;
            x3 = -10;
            y3 = 50;
            x4 = +10;
            y4 = 50;
            break;
        default:
            flecheDefinie = false; // 0 = tendance inconnue
        }
        if (flecheDefinie)
        {
            x0 = x0 * 3 / 4, y0 = y0 * 3 / 4, x1 = x1 * 3 / 4, y1 = y1 * 3 / 4, x2 = x2 * 3 / 4;
            y2 = y2 * 3 / 4, x3 = x3 * 3 / 4, y3 = y3 * 3 / 4, x4 = x4 * 3 / 4, y4 = y4 * 3 / 4;
            if (doubleFleche)
            {
                CanvaAccueil->fillTriangle(X0 + x0 - offset, Y0 + y0, X0 + x1 - offset, Y0 + y1, X0 + x2 - offset, Y0 + y2, glucoseInfoColor);
                CanvaAccueil->fillTriangle(X0 + x3 - offset, Y0 + y3, X0 + x1 - offset, Y0 + y1, X0 + x4 - offset, Y0 + y4, glucoseInfoColor);
            }
            CanvaAccueil->fillTriangle(X0 + x0, Y0 + y0, X0 + x1, Y0 + y1, X0 + x2, Y0 + y2, glucoseInfoColor);
            CanvaAccueil->fillTriangle(X0 + x3, Y0 + y3, X0 + x1, Y0 + y1, X0 + x4, Y0 + y4, glucoseInfoColor);
        }
    }
    // Age de la dernière glycémie : disque qui se remplit en 5 minutes (sens horaire depuis midi)
    {
        const int16_t Rc = 24;
        const int16_t Xc = EcranW - Rc - 10;
        const int16_t Yc = EcranH / 2 - 6; // à droite de la valeur, sous le bandeau
        uint16_t couleurAge = RGB565_WHITE;
        float fraction = 0.0;
        if (HeureValide && lastGlyUnixTime > 0)
        {
            time_t now;
            time(&now);
            AgeGlycemie = (long)now - lastGlyUnixTime;
            int minutes = AgeGlycemie / 60;
            if (minutes >= 10)
                couleurAge = RGB565_ORANGE;
            if (minutes >= 15)
                couleurAge = RGB565_RED;
            fraction = float(AgeGlycemie) / 300.0; // 5 minutes = disque plein
            if (fraction < 0.0)
                fraction = 0.0;
            if (fraction > 1.0)
                fraction = 1.0;
        }
        else
        {
            couleurAge = RGB565_GREY; // Pas encore de mesure
        }
        CanvaAccueil->drawCircle(Xc, Yc, Rc, couleurAge);
        if (fraction > 0.005)
            CanvaAccueil->fillArc(Xc, Yc, Rc - 3, 1, -90, -90 + 360.0 * fraction, couleurAge);
    }
    CanvaAccueil->setTextColor(RGB565_WHITE);
    // Trace Avancement demande glycémie
    float dT = 0.0;

    if (lastReceptionGlycMillis <= lastDemandeGlycMillis)
    { // ON a appelé pas encore de réponse
        dtReponse = float((millis() - lastDemandeGlycMillis)) * float(EcranW) / float(recurGlycMillis);
    }
    else
    { // L réponse est arrivée, on affiche le temps écoulé depuis la dernière glycémie et le temps restant pour la prochaine
        dtReponse = float((lastReceptionGlycMillis - lastDemandeGlycMillis)) * float(EcranW) / float(recurGlycMillis);
        dT = float((millis() - lastReceptionGlycMillis)) * (float(EcranW) - dtReponse) / float(recurGlycMillis);
        if (dT > (float(EcranW + 10) - dtReponse)) // Pas normal on dépasse la récurrence, on affiche un rectangle rouge
        {
            CanvaAccueil->fillRect(0, EcranH - 10, EcranW, 10, RGB565_RED);
        }
        else
        {
            CanvaAccueil->fillRect(dtReponse, EcranH - 10, dT, 10, C_grisMoyen);
        }
    }
    if (dtReponse > float(EcranW + EcranW2)) // Pas normal, on dépasse largement la récurrence, on affiche un rectangle rouge
    {
        dtReponse = float(EcranW + 20);
    }
    CanvaAccueil->fillRect(0, EcranH - 10, int(dtReponse), 10, RGB565_MAGENTA);

    // Trace courbe glycemie
    if (pointCountGly > 1)
    {
        int16_t X0 = 30;
        int16_t Y0 = EcranH / 1.9;
        int16_t W = EcranW - X0;
        int16_t H = EcranH * 0.37;
        int16_t EcranH10 = EcranH - 10;
        int16_t x, y, last_x;
        int lastHeure = -1;
        unsigned long Tmin = 0, Tmax = 0;
        Tmin = glucoseHeure[0];
        Tmax = glucoseHeure[pointCountGly - 1];
        last_x = X0;
        float DT = float(W) / (float(Tmax - Tmin));
        CanvaAccueil->setFont(u8g2_font_6x10_tf);

        for (int c = 0; c < 4; c++) // Trace fond graphique
        {
            int16_t y2 = EcranH10 - H * seuilCoul[c] / 400;
            y = EcranH10 - H * seuilCoul[c + 1] / 400;
            String Seuil = String(seuilCoul[c + 1]);
            if (glucoseUnit == 1)
            { // mmol/L
                Seuil = String(float(seuilCoul[c + 1]) / 18.0f, 1);
            }
            PrintDroite(CanvaAccueil, Seuil, X0, y, 1);
            CanvaAccueil->fillRect(X0, y, W, y2 - y, CouleursFond[c]);
        }
        for (int i = 0; i < pointCountGly; i++)
        {
            x = X0 + int(DT * float(glucoseHeure[i] - Tmin));
            y = H * glucoseValues[i] / 400;
            for (int c = 0; c < 4; c++)
            {
                if (glucoseValues[i] > seuilCoul[c])
                    idxCoul = c;
            }
            CanvaAccueil->fillRect(last_x, EcranH10 - y, x - last_x, y, Couleurs[idxCoul]);
            last_x = x;
            int heure = unixToHeure(glucoseHeure[i]);
            if (timeFormat == TIME_FORMAT_12H)
            {
                if (heure > 12)
                {
                    heure -= 12;
                }
                else if (heure == 0)
                {
                    heure = 12; // Midnight case
                }
            }
            if (heure != lastHeure)
            {
                if (heure >= 0 && lastHeure >= 0)
                {
                    CanvaAccueil->drawFastVLine(x, EcranH10, 10, RGB565_WHITE);
                    // Allow label to be drawn even at the edge (changed from x < W to x <= W + X0)
                    if (x <= W + X0)
                        PrintGauche(CanvaAccueil, String(heure), x, EcranH - 5, 1);
                }
                lastHeure = heure;
            }
        }
        CanvaAccueil->drawFastVLine(X0, EcranH10 - H, H, RGB565_WHITE); // Axe vertical
    }
}

void Trace_Gauge(Arduino_Canvas *canva)
{
    int W2 = EcranW / 2;
    int C = EcranH / 2;
    int R0 = EcranH / 3.5;
    int R1 = EcranH / 2 - 20;
    int Teta0 = -180;
    int Teta1 = Teta0 + 180 * targetLow / 400;
    canva->fillArc(W2, C, R0, R1, Teta0, Teta1, RGB565_BLUE);
    Teta0 = Teta1;
    Teta1 = -180 + 180 * targetHigh / 400;
    canva->fillArc(W2, C, R0, R1, Teta0, Teta1, RGB565_GREEN);
    Teta0 = Teta1;
    Teta1 = -180 + 180 * 300 / 400;
    if (glucoseUnit == 1)
    { // mmol/L
        Teta1 = -180 + 180 * 16 * 18 / 400;
    }
    canva->fillArc(W2, C, R0, R1, Teta0, Teta1, RGB565_ORANGE);
    Teta0 = Teta1;
    Teta1 = 0;
    canva->fillArc(W2, C, R0, R1, Teta0, Teta1, RGB565_RED);
}