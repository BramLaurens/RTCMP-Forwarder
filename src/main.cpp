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
#define GNSS_RX 16   // GNSS TX → ESP32 RX
#define GNSS_TX 17   // GNSS RX → ESP32 TX
#define GNSS_BAUD 115200

WiFiClient ntripClient;

String lastGGA = "";
unsigned long lastGGASend = 0;

// -------------------------------------------------------------------
// WiFi Connection
// -------------------------------------------------------------------
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// -------------------------------------------------------------------
// Base64 Encoder (for Authorization)
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Connect to NTRIP Caster
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Read GGA from GNSS and store latest one
// -------------------------------------------------------------------
void checkForGGA() {
  static String buffer;
  while (GNSS.available()) {
    char c = GNSS.read();
    buffer += c;

    if (c == '\n') {
      if (buffer.startsWith("$GPGGA") || buffer.startsWith("$GNGGA") ||
          buffer.startsWith("$GLGGA") || buffer.startsWith("$GBGGA")) {
        lastGGA = buffer;
      }
      buffer = "";
    }
  }
}

// -------------------------------------------------------------------
// Periodically send the latest GGA to NTRIP server
// -------------------------------------------------------------------
void sendGGAIfDue() {
  if (millis() - lastGGASend >= 1000 && lastGGA.length() > 0) {
    ntripClient.print(lastGGA);
    lastGGASend = millis();
  }
}

// -------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  GNSS.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX, GNSS_TX);
  connectWiFi();
  while (!connectNTRIP()) {
    delay(3000);
  }
  Serial.println("[READY] NTRIP bridge active.");
}

// -------------------------------------------------------------------
// Main Loop
// -------------------------------------------------------------------
void loop() {
  // 1. Read from NTRIP and forward to GNSS
  int count = 0;
  while (ntripClient.available() && count < 256) {
    uint8_t b = ntripClient.read();
    GNSS.write(b);
    count++;
  }

  // 2. Read GNSS data (for GGA detection and optional debug)
  // checkForGGA();

  // 3. Optional: forward GNSS → USB for monitoring (trimmed for performance)
  count = 0;
  while (GNSS.available() && count < 256) {
    uint8_t b = GNSS.read();
    Serial.write(b);
    count++;
  }

  // 4. Send latest GGA to NTRIP server every 1s
  // sendGGAIfDue();
}
