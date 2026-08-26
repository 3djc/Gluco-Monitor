#include "Ecran/pageAffichage.h"
#include "Config.h"
#include <Arduino.h>
#include <WiFi.h>
#include "Ecran/Gestion.h"
#include "Stock.h"
#include "Langues/Langue.h"

static RadioBouton Rboutons[10] = {
    {10, 65, 14, "10%"},
    {10, 65, 14, "25%"},
    {10, 65, 14, "50%"},
    {10, 65, 14, "100%"},
    {10, 135, 14, "0°"},
    {10, 135, 14, "180°"},
    {10, 205, 14, "Blanc"},
    {10, 205, 14, "Couleur"},
    {10, 275, 14, "12H (AM/PM)"},
    {10, 275, 14, "24H"}};

void DrawBoutons();

void pageAffichageSetup()
{
    PageActu = pageAffichage;
    CanvaBase->setFont(u8g2_font_helvB18_tf);
    CanvaBase->setTextColor(RGB565_WHITE);
    CanvaBase->fillScreen(C_grisFonce);
    PrintCentre(CanvaBase, T("Display"), EcranW / 2, 30, 1);
    //============= Luminosité ==========================
    CanvaBase->fillRoundRect(7, 40, EcranW - 14, 60, 8, RGB565_NAVY);
    CanvaBase->drawRoundRect(7, 40, EcranW - 14, 60, 8, RGB565_WHITE);
    CanvaBase->setFont(u8g2_font_helvB14_tf);
    CanvaBase->setTextColor(RGB565_WHITE);
    PrintCentre(CanvaBase, T("Luminosite"), EcranW / 2, 60, 1);

    //============= Rotation ==========================
    CanvaBase->fillRoundRect(7, 110, EcranW - 14, 60, 8, RGB565_NAVY);
    CanvaBase->drawRoundRect(7, 110, EcranW - 14, 60, 8, RGB565_WHITE);
    CanvaBase->setFont(u8g2_font_helvB14_tf);
    CanvaBase->setTextColor(RGB565_WHITE);
    PrintCentre(CanvaBase, T("Rotation"), EcranW / 2, 130, 1);

    //============= Couleur Glycémie ======================
    CanvaBase->fillRoundRect(7, 180, EcranW - 14, 60, 8, RGB565_NAVY);
    CanvaBase->drawRoundRect(7, 180, EcranW - 14, 60, 8, RGB565_WHITE);
    CanvaBase->setFont(u8g2_font_helvB14_tf);
    CanvaBase->setTextColor(RGB565_WHITE);
    PrintCentre(CanvaBase, T("CouleurGlycemie"), EcranW / 2, 200, 1);

    //============= Format Heure ======================
    CanvaBase->fillRoundRect(7, 250, EcranW - 14, 60, 8, RGB565_NAVY);
    CanvaBase->drawRoundRect(7, 250, EcranW - 14, 60, 8, RGB565_WHITE);
    CanvaBase->setFont(u8g2_font_helvB14_tf);
    CanvaBase->setTextColor(RGB565_WHITE);
    PrintCentre(CanvaBase, T("FormatHeure"), EcranW / 2, 270, 1);

    DrawBoutons();

    CanvaBase->flush();
}

void handleTouch_Affichage(uint16_t touchX, uint16_t touchY)
{
    int8_t oldRotation = rotation;
    for (int i = 0; i < 10; i++)
    {
        if (RadioBouton_Appui(Rboutons[i], touchX, touchY))
        {
            switch (i)
            {
            case 0:
                LuminositeNuit = 15;
                break;
            case 1:
                LuminositeNuit = 40;
                break;
            case 2:
                LuminositeNuit = 100;
                break;
            case 3:
                LuminositeNuit = 255;
                break;
            case 4:
                rotation = 1;
                break;
            case 5:
                rotation = 3;
                break;
            case 6:
                glucoseColor = GLUCOSE_BLANC;
                break;
            case 7:
                glucoseColor = GLUCOSE_COULEUR;
                break;
             case 8:
                timeFormat = TIME_FORMAT_12H;
                break;
            case 9:
                timeFormat = TIME_FORMAT_24H;
                break;
            }

            DrawBoutons();
            if (i < 4)
            {
                ledcWrite(GFX_BL, LuminositeNuit);
                delay(1000);
            }
            if (oldRotation != rotation)
            {
                CanvaBase->setRotation(rotation);
                pageAffichageSetup();
            }
            RecordFichierParametres();
        }
    }
}
void DrawBoutons()
{
    int16_t Seuil[4] = {15, 40, 100, 255};
    for (int i = 0; i < 4; i++)
    {
        Rboutons[i].X0 = EcranW * (i * 3 + 2) / 13 - 20;
        if (LuminositeNuit == Seuil[i])
        {
            RadioBouton_Trace(Rboutons[i], RGB565_BLUE);
        }
        else
        {
            RadioBouton_Trace(Rboutons[i]);
        }
    }
    int16_t rot[2] = {1, 3};
    for (int i = 4; i < 6; i++)
    {
        Rboutons[i].X0 = EcranW * (i * 2 - 7) / 4 - 20;
        if (rotation == rot[i - 4])
        {
            RadioBouton_Trace(Rboutons[i], RGB565_BLUE);
        }
        else
        {
            RadioBouton_Trace(Rboutons[i]);
        }
    }
    int16_t coul[2] = {0, 1};
    for (int i = 6; i < 8; i++)
    {
        Rboutons[i].X0 = EcranW * (i * 2 - 11) / 4 - 20;
        if (glucoseColor == coul[i - 6])
        {
            RadioBouton_Trace(Rboutons[i], RGB565_BLUE);
        }
        else
        {
            RadioBouton_Trace(Rboutons[i]);
        }
    }
    int16_t form[2] = {TIME_FORMAT_12H, TIME_FORMAT_24H};
    for (int i = 8; i < 10; i++)
    {
        Rboutons[i].X0 = EcranW * (i * 2 - 15) / 4 - 20;
        if (timeFormat == form[i - 8])
        {
            RadioBouton_Trace(Rboutons[i], RGB565_BLUE);
        }
        else
        {
            RadioBouton_Trace(Rboutons[i]);
        }
    }
}