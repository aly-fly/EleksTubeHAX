#ifndef TFTS_H
#define TFTS_H

#define FS_NO_GLOBALS
#include <FS.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include "GLOBAL_DEFINES.h"
#include "ChipSelect.h"

class TFTs : public TFT_eSPI
{
public:
  TFTs() : TFT_eSPI(), chip_select(), TFTsEnabled(false)
  {
#ifndef HARDWARE_IPSTUBE_CLOCK
    for (uint8_t digit = 0; digit < NUM_DIGITS; digit++)
      digits[digit] = 0;
#endif
  }

  // no == Do not send to TFT. yes == Send to TFT if changed. force == Send to TFT.
  enum show_t
  {
    no,
    yes,
    force
  };
  // A digit of 0xFF means blank the screen.
  const static uint8_t blanked = 255;

  uint8_t dimming = 255; // amount of dimming graphics
  uint8_t current_graphic = 1;

  void begin();
  void reinit();
  void clear();
  void showNoWifiStatus();
  void showNoMqttStatus();

  void setDigit(uint8_t digit, uint8_t value, show_t show = yes);
  uint8_t getDigit(uint8_t digit) { return digits[digit]; }

  void showAllDigits()
  {
    for (uint8_t digit = 0; digit < NUM_DIGITS; digit++)
      showDigit(digit);
  }
  void showDigit(uint8_t digit);

  // Controls the power to all displays
  void enableAllDisplays();
  void disableAllDisplays();
  void toggleAllDisplays();
  bool isEnabled() { return TFTsEnabled; }

  // Making chip_select public so we don't have to proxy all methods, and the caller can just use it directly.
  ChipSelect chip_select;

  uint8_t NumberOfClockFaces = 0;
  void LoadNextImage();
  void InvalidateImageInBuffer(); // force reload from Flash with new dimming settings
  void ProcessUpdatedDimming();

  String clockFaceToName(uint8_t clockFace);
  uint8_t nameToClockFace(String name);

private:
  uint8_t digits[NUM_DIGITS];
  bool TFTsEnabled = false;

  bool FileExists(const char *path);
  int8_t CountNumberOfClockFaces();
  bool LoadImageIntoBuffer(uint8_t file_index);
  void DrawImage(uint8_t file_index);
  uint16_t read16(fs::File &f);
  uint32_t read32(fs::File &f);

  static uint16_t UnpackedImageBuffer[TFT_HEIGHT][TFT_WIDTH];
  uint8_t FileInBuffer = 255; // invalid, always load first image
  uint8_t NextFileRequired = 0;

  String patterns_str[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
  void loadClockFacesNames();
};

extern TFTs tfts;

#endif // TFTS_H
