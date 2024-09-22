#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// Wi-Fi ayarları
const char* ssid = "ssid";      // Wi-Fi SSID'nizi buraya yazın
const char* password = "password"; // Wi-Fi şifrenizi buraya yazın
// M
#define SS_PIN 4   // GPIO 4 (D2)
#define RST_PIN 0  // GPIO 0 (D3)

ESP8266WebServer server(80);
MFRC522 mfrc522(SS_PIN, RST_PIN);

const int MAX_CARDS = 10;
String cardList[MAX_CARDS];
int cardCount = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/write", HTTP_POST, handleWrite);

  server.begin();
}

void loop() {
  server.handleClient();
  readRFID();
}

void readRFID() {
  // Look for new cards
  if (mfrc522.PICC_IsNewCardPresent()) {
    // Select one of the cards
    if (mfrc522.PICC_ReadCardSerial()) {
      String currentUID = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        currentUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        currentUID += String(mfrc522.uid.uidByte[i], HEX);
      }
      currentUID.toUpperCase();

      // Add UID to list if not already present
      if (cardCount < MAX_CARDS && !cardInList(currentUID)) {
        cardList[cardCount] = currentUID;
        cardCount++;
      }
      
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  }
}

bool cardInList(String uid) {
  for (int i = 0; i < cardCount; i++) {
    if (cardList[i] == uid) {
      return true;
    }
  }
  return false;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>RFID Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; }
    .container { padding: 20px; }
    .button { background-color: #4CAF50; color: white; padding: 10px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; }
    .button:hover { background-color: #45a049; }
    .list { margin: 10px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>RFID Web Server</h1>
    <p>Current saved UIDs:</p>
    <div class="list">%CARD_LIST%</div>
    <form action="/scan" method="GET">
      <input type="submit" class="button" value="Scan Card">
    </form>
    <form action="/write" method="POST">
      <input type="text" name="order" placeholder="Enter UID order (e.g., 1,2,3)" required>
      <input type="submit" class="button" value="Write UID to Card">
    </form>
  </div>
</body>
</html>
)rawliteral";

  String currentCardList = "";
  for (int i = 0; i < cardCount; i++) {
    currentCardList += "<p>" + String(i + 1) + ". " + cardList[i] + "</p>";
  }
  if (currentCardList.isEmpty()) {
    currentCardList = "No cards scanned yet.";
  }
  String htmlContent = html;
  htmlContent.replace("%CARD_LIST%", currentCardList);
  server.send(200, "text/html", htmlContent);
}

void handleScan() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Redirecting...");
}
void handleWrite() {
  String order = server.arg("order");
  int uidOrder[MAX_CARDS];
  int uidCount = 0;

  // Parse the order input
  int lastIndex = 0;
  for (int i = 0; i < order.length(); i++) {
    if (order[i] == ',' || i == order.length() - 1) {
      int currentIndex = order.substring(lastIndex, i == order.length() - 1 ? i + 1 : i).toInt();
      if (currentIndex > 0 && currentIndex <= cardCount) {
        uidOrder[uidCount++] = currentIndex - 1;
      }
      lastIndex = i + 1;
    }
  }

  if (uidCount > 0) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      byte sector = 1;
      byte blockAddr = 4;
      byte trailerBlock = 7;
      MFRC522::MIFARE_Key key;
      for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

      MFRC522::StatusCode status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
      if (status == MFRC522::STATUS_OK) {
        for (int i = 0; i < uidCount; i++) {
          byte dataBlock[16];
          String uidToWrite = cardList[uidOrder[i]];
          memset(dataBlock, 0xFF, 16);  // Set all bytes to 0xFF to initialize
          for (int j = 0; j < 4; j++) {
            dataBlock[j] = strtol(uidToWrite.substring(j * 2, j * 2 + 2).c_str(), NULL, 16);
          }

          status = (MFRC522::StatusCode) mfrc522.MIFARE_Write(blockAddr, dataBlock, 16);
          if (status != MFRC522::STATUS_OK) {
            server.send(200, "text/html", "Write failed! <a href=\"/\">Back</a>");
            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
            return;
          }
          blockAddr++;
        }
        server.send(200, "text/html", "Write successful! <a href=\"/\">Back</a>");
      } else {
        server.send(200, "text/html", "Authentication failed! <a href=\"/\">Back</a>");
      }
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    } else {
      server.send(200, "text/html", "No card detected! <a href=\"/\">Back</a>");
    }
  } else {
    server.send(200, "text/html", "Invalid UID order! <a href=\"/\">Back</a>");
  }
}
