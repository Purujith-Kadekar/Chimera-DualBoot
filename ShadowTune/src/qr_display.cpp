// ═══════════════════════════════════════════════════════════════════════════════
//  qr_display.cpp — WiFi QR code rendering on the ILI9341
// ═══════════════════════════════════════════════════════════════════════════════
//  Renders a WiFi-network QR code (WIFI:S:<SSID>;T:WPA;P:<password>;H:false;;)
//  as black/white modules on the TFT, with a proper 4-module quiet zone and
//  automatic version selection so the QR is as large as possible at the given
//  max pixel size while remaining scannable by phone cameras.
// ═══════════════════════════════════════════════════════════════════════════════
#include "qr_display.h"
#include <qrcode.h>

int drawWiFiQR(Adafruit_ILI9341& tft, int cx, int cy, int maxPx,
               const char* ssid, const char* password) {
  if (!ssid) return 0;

  // Build the WiFi QR string.
  // Format: WIFI:S:<SSID>;T:<WPA|nopass>;P:<password>;H:false;;
  String qrText = "WIFI:S:";
  qrText += ssid;
  if (password && password[0] != '\0') {
    qrText += ";T:WPA;P:";
    qrText += password;
  } else {
    qrText += ";T:nopass;P:";
  }
  qrText += ";H:false;;";

  // Pick the smallest QR version that fits the payload at ECC_MEDIUM.
  // Version 4 = 33×33 (64 byte payload at M ECC). For longer SSIDs/passwords
  // we step up to version 5 (37×37, 84 bytes) or 6 (41×41, 106 bytes).
  // We try versions 4..7 in order and use the first that succeeds.
  const uint8_t VERSIONS_TO_TRY[] = {4, 5, 6, 7};
  const int NUM_VERSIONS = sizeof(VERSIONS_TO_TRY) / sizeof(VERSIONS_TO_TRY[0]);

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(7)];  // buffer for the largest version we try
  bool ok = false;
  uint8_t usedVersion = 4;

  for (int i = 0; i < NUM_VERSIONS; i++) {
    usedVersion = VERSIONS_TO_TRY[i];
    if (qrcode_initText(&qrcode, qrcodeData, usedVersion, ECC_MEDIUM,
                        qrText.c_str()) == 0) {
      ok = true;
      break;
    }
  }
  if (!ok) {
    Serial.println("[QR] Failed to encode WiFi QR (payload too long)");
    return 0;
  }

  // Calculate module size so the QR fits within maxPx.
  // At least 3 px per module is required for reliable scanning by phone
  // cameras at typical viewing distances.
  int moduleSize = maxPx / qrcode.size;
  if (moduleSize < 3) moduleSize = 3;
  int qrPx = qrcode.size * moduleSize;

  // Quiet zone = 4 modules (per QR spec) — phones need this white border
  // to detect the QR reliably. We draw it as a solid white rectangle.
  int quietZone = 4 * moduleSize;

  // Top-left corner of the QR (centered at cx, cy).
  int startX = cx - qrPx / 2;
  int startY = cy - qrPx / 2;

  // White background with quiet zone
  tft.fillRect(startX - quietZone, startY - quietZone,
               qrPx + 2 * quietZone, qrPx + 2 * quietZone, ILI9341_WHITE);

  // Draw only black modules — white is the background, faster this way.
  for (int row = 0; row < qrcode.size; row++) {
    for (int col = 0; col < qrcode.size; col++) {
      if (qrcode_getModule(&qrcode, col, row)) {
        tft.fillRect(startX + col * moduleSize,
                     startY + row * moduleSize,
                     moduleSize, moduleSize, ILI9341_BLACK);
      }
    }
  }

  Serial.printf("[QR] Drawn: v%u, %dx%d modules, %dpx/module, total %dpx (quiet zone %dpx)\n",
                usedVersion, qrcode.size, qrcode.size, moduleSize, qrPx, quietZone);

  // Return the total drawn size (QR + quiet zone) so the caller can
  // arrange other UI elements without overlapping.
  return qrPx + 2 * quietZone;
}
