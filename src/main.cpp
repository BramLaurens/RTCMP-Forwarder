#include <WiFi.h>
#include <WiFiClient.h>

const char* WIFI_SSID     = "Luluenco";
const char* WIFI_PASSWORD = "Tuindorprulez2023!";

const char* NTRIP_HOST = "ntrip.kadaster.nl";
const int   NTRIP_PORT = 2101;
const char* NTRIP_MOUNTPOINT = "CBW100NLD0";
const char* NTRIP_USER = "";
const char* NTRIP_PASS = "";

// LC29H UART (ESP32 UART2)
HardwareSerial GNSS(2);
#define GNSS_RX 16   // GNSS TX -> ESP32 RX
#define GNSS_TX 17   // GNSS RX <- ESP32 TX
#define GNSS_BAUD 115200

WiFiClient ntripClient;

String lastGGA = "";
unsigned long lastGGASend = 0;

// Optional: enable to increase UART buffers (uncomment to use)
//#define ENABLE_UART_TUNING
#ifdef ENABLE_UART_TUNING
  #include "driver/uart.h"
  #define UART_RX_BUFFER_SIZE 2048
  #define UART_TX_BUFFER_SIZE 2048
#endif

// ---------------- WiFi ----------------
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ---------------- base64 ----------------
String base64Encode(const String& input) {
  const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output = "";
  int val = 0, valb = -6;
  for (size_t i = 0; i < input.length(); i++) {
    unsigned char c = input[i];
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      output += table[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) output += table[((val << 8) >> (valb + 8)) & 0x3F];
  while (output.length() % 4) output += '=';
  return output;
}

// ---------------- NTRIP connect ----------------
bool connectNTRIP() {
  Serial.printf("[NTRIP] Connecting to %s:%d ...\n", NTRIP_HOST, NTRIP_PORT);
  if (!ntripClient.connect(NTRIP_HOST, NTRIP_PORT)) {
    Serial.println("[NTRIP] Connection failed.");
    return false;
  }

  String authHeader = "";
  if (strlen(NTRIP_USER) > 0) {
    String credentials = String(NTRIP_USER) + ":" + String(NTRIP_PASS);
    authHeader = "Authorization: Basic " + base64Encode(credentials) + "\r\n";
  }

  String request =
      String("GET /") + NTRIP_MOUNTPOINT + " HTTP/1.0\r\n" +
      "User-Agent: NTRIP ESP32Client\r\n" +
      "Accept: */*\r\n" +
      authHeader + "\r\n";

  ntripClient.print(request);

  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (ntripClient.available()) {
      String line = ntripClient.readStringUntil('\n');
      if (line.startsWith("ICY 200 OK")) {
        Serial.println("[NTRIP] Connected successfully.");
        return true;
      }
    }
  }

  Serial.println("[NTRIP] No valid response from server.");
  return false;
}

// ---------------- Send GGA once per second ----------------
void sendGGAIfDue() {
  if (millis() - lastGGASend >= 1000 && lastGGA.length() > 0 && ntripClient.connected()) {
    ntripClient.print(lastGGA);
    lastGGASend = millis();
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  // start GNSS UART
  GNSS.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX, GNSS_TX);

  // optional: increase UART driver buffers (uncomment ENABLE_UART_TUNING)
  #ifdef ENABLE_UART_TUNING
    // re-install uart driver for GNSS port with larger buffers
    uart_driver_delete(GNSS.port());
    uart_driver_install(GNSS.port(), UART_RX_BUFFER_SIZE, UART_TX_BUFFER_SIZE, 0, NULL, 0);
    Serial.println("[UART] increased RX/TX buffer sizes.");
  #endif

  connectWiFi();
  while (!connectNTRIP()) {
    delay(3000);
  }
  Serial.println("[READY] NTRIP bridge active.");
}

// ---------------- Helper: process GNSS bytes (single-read) ----------------
// This function reads up to 'maxBytes' bytes from GNSS into 'bufLen' and
// writes the same bytes to Serial (USB). It also extracts complete NMEA
// lines and stores latest GGA sentence in lastGGA.
void processGNSSOnce(size_t maxBytes) {
  static String lineBuffer; // accumulates characters until newline
  uint8_t tmpBuf[512];
  size_t toRead = 0;
  if (GNSS.available()) {
    toRead = GNSS.readBytes(tmpBuf, min((size_t)maxBytes, sizeof(tmpBuf)));
  }

  if (toRead == 0) return;

  // forward same bytes to USB serial (so nothing is lost)
  Serial.write(tmpBuf, toRead);

  // parse bytes for complete NMEA lines and update lastGGA
  for (size_t i = 0; i < toRead; ++i) {
    char c = (char)tmpBuf[i];
    lineBuffer += c;
    if (c == '\n') {
      // got a full line
      if (lineBuffer.startsWith("$GPGGA") || lineBuffer.startsWith("$GNGGA") ||
          lineBuffer.startsWith("$GLGGA") || lineBuffer.startsWith("$GBGGA")) {
        lastGGA = lineBuffer; // keep entire NMEA line (with \r\n)
      }
      lineBuffer = "";
    }
    // guard against runaway buffer (in case of no newline for long time)
    if (lineBuffer.length() > 200) {
      lineBuffer = lineBuffer.substring(lineBuffer.length() - 100);
    }
  }
}

// ---------------- Main loop ----------------
void loop() {
  // 1) Forward NTRIP -> GNSS in 256-byte chunks
  int i = 0;
  while (ntripClient.available() && i < 256) {
    uint8_t b = ntripClient.read();
    GNSS.write(b);
    i++;
  }

  // 2) Read GNSS *once* and both: (a) forward to USB, (b) extract GGA
  processGNSSOnce(256);

  // 3) Send latest GGA to caster every 1s
  sendGGAIfDue();
}
