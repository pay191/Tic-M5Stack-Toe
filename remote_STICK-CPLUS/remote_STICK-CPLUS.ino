#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Unit_MiniEncoderC.h"

#define BUZZER_PIN 2

uint8_t board[9] = {0};  // 0 = empty, 1 = X, 2 = O
uint8_t cursorIndex = 0;
int PLAYER_ID = 0;
char PLAYER_MARK = '?';

const char* player1_mac_str = "F0:24:F9:9B:5E:90";
const char* player2_mac_str = "F0:24:F9:9D:9B:F4";
uint8_t consoleMac[] = {0x24, 0xEC, 0x4A, 0x36, 0xE6, 0x60}; // Core S3

typedef struct {
  uint8_t player;
  uint8_t moveIndex;
} MoveMessage;

typedef struct {
  uint8_t boardState[9];
  uint8_t gameOver;
  uint8_t winner;
} GameState;

UNIT_MINIENCODERC encoder;
int32_t lastEncoderValue = 0;
int32_t accumulatedDiff = 0;

void sendMove();
void drawScreen();
void onGameStateReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void playTone(bool win = false);
bool macMatches(const String& a, const String& b);

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextSize(2);
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);  // Set channel to match Core S3

  String myMac;
  while (true) {
    myMac = WiFi.macAddress();
    if (!myMac.startsWith("00:00:00")) break;
    Serial.println("Waiting for valid MAC...");
    delay(100);
  }

  Serial.print("My MAC Address: ");
  Serial.println(myMac);

  if (macMatches(myMac, player1_mac_str)) {
    PLAYER_ID = 1;
    PLAYER_MARK = 'X';
  } else if (macMatches(myMac, player2_mac_str)) {
    PLAYER_ID = 2;
    PLAYER_MARK = 'O';
  } else {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.setCursor(10, 20);
    M5.Lcd.println("Unknown MAC");
    Serial.println("Unknown MAC. Halting.");
    while (true);
  }

  pinMode(BUZZER_PIN, OUTPUT);

  encoder.begin(&Wire, MINIENCODERC_ADDR, 0, 26, 100000UL);
  lastEncoderValue = encoder.getEncoderValue();

  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onGameStateReceived);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, consoleMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  drawScreen();
}

void loop() {
  M5.update();

  int32_t currentValue = encoder.getEncoderValue();
  int32_t diff = currentValue - lastEncoderValue;

  if (diff != 0) {
    accumulatedDiff += diff;
    lastEncoderValue = currentValue;
    Serial.printf("Raw encoder diff: %d | Accumulated: %d\n", diff, accumulatedDiff);

    if (abs(accumulatedDiff) >= 2) {
      int steps = accumulatedDiff / 2;
      cursorIndex = (cursorIndex + steps + 9) % 9;
      accumulatedDiff = 0;
      Serial.printf("Moved cursor by %d → New index: %d\n", steps, cursorIndex);
      drawScreen();
    }
  }

  if (encoder.getButtonStatus() == false) {
    Serial.println("🔘 Button pressed");
    if (board[cursorIndex] == 0) {
      sendMove();
      playTone(false);
      delay(300);  // debounce
    } else {
      Serial.println("⚠️ Cell already taken, ignoring move.");
    }
  }

  delay(30);
}

void sendMove() {
  MoveMessage msg = { (uint8_t)PLAYER_ID, cursorIndex };
  Serial.printf("📤 Sending move: Player %d → Index %d\n", msg.player, msg.moveIndex);
  esp_err_t result = esp_now_send(consoleMac, (uint8_t *)&msg, sizeof(msg));
  if (result == ESP_OK) {
    Serial.println("✅ Move sent");
  } else {
    Serial.printf("❌ Failed to send move. Error code: %d\n", result);
  }
}

void drawScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("Player %d - %c", PLAYER_ID, PLAYER_MARK);

  for (int i = 0; i < 9; i++) {
    int x = 20 + (i % 3) * 35;
    int y = 50 + (i / 3) * 35;
    if (i == cursorIndex) {
      M5.Lcd.drawRect(x - 2, y - 2, 30, 30, YELLOW);
    }
    char mark = board[i] == 1 ? 'X' : (board[i] == 2 ? 'O' : ' ');
    M5.Lcd.setCursor(x, y);
    M5.Lcd.printf("%c", mark);
  }
}

void onGameStateReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GameState)) return;
  GameState state;
  memcpy(&state, data, sizeof(GameState));
  memcpy(board, state.boardState, 9);
  drawScreen();

  if (state.gameOver && state.winner == PLAYER_ID) {
    playTone(true);
  }
}

void playTone(bool win) {
  if (win) {
    tone(BUZZER_PIN, 880, 200);
    delay(200);
    tone(BUZZER_PIN, 1040, 300);
  } else {
    tone(BUZZER_PIN, 660, 100);
  }
}

bool macMatches(const String& a, const String& b) {
  return a.equalsIgnoreCase(String(b));
}
