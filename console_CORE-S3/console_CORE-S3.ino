#include <M5CoreS3.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define PLAYER1_ID 1
#define PLAYER2_ID 2

uint8_t player1Mac[] = {0xF0, 0x24, 0xF9, 0x9B, 0x5E, 0x90};
uint8_t player2Mac[] = {0xF0, 0x24, 0xF9, 0x9D, 0x9B, 0xF4};

uint8_t board[9] = {0};
uint8_t currentPlayer = PLAYER1_ID;
bool gameOver = false;
uint8_t winner = 0;
unsigned long gameEndTime = 0;
bool waitingToReset = false;

typedef struct {
  uint8_t player;
  uint8_t moveIndex;
} MoveMessage;

typedef struct {
  uint8_t boardState[9];
  uint8_t gameOver;
  uint8_t winner;
} GameState;

void drawBoard();
void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len);
bool checkWin(uint8_t player);
bool checkDraw();
void broadcastState();
void resetGame();

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  String myMac = WiFi.macAddress();
  Serial.print("Console MAC Address: ");
  Serial.println(myMac);

  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(3);

  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("ESP-NOW Init Failed");
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr, player1Mac, 6);
  esp_now_add_peer(&peerInfo);
  Serial.println("✅ Added Player 1 peer");

  memcpy(peerInfo.peer_addr, player2Mac, 6);
  esp_now_add_peer(&peerInfo);
  Serial.println("✅ Added Player 2 peer");

  Serial.println("✅ ESP-NOW initialized and peers added.");
  resetGame();
}

void loop() {
  M5.update();

  if (waitingToReset && millis() - gameEndTime >= 10000) {
    resetGame();
    waitingToReset = false;
  }
}

void resetGame() {
  memset(board, 0, sizeof(board));
  currentPlayer = PLAYER1_ID;
  gameOver = false;
  winner = 0;
  drawBoard();
  broadcastState();
  Serial.println("🔄 Game reset and state broadcasted");
}

void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.printf("📩 Data received. Length: %d\n", len);

  if (len != sizeof(MoveMessage)) {
    Serial.println("❌ Invalid move message size");
    return;
  }

  MoveMessage msg;
  memcpy(&msg, data, sizeof(MoveMessage));
  Serial.printf("🎮 Move from Player %d → Index %d\n", msg.player, msg.moveIndex);

  if (gameOver) {
    Serial.println("🚫 Game is already over.");
    return;
  }

  if (msg.player != currentPlayer) {
    Serial.printf("🚫 Not Player %d's turn\n", msg.player);
    return;
  }

  if (board[msg.moveIndex] != 0) {
    Serial.println("🚫 Cell already taken.");
    return;
  }

  board[msg.moveIndex] = msg.player;
  gameOver = checkWin(msg.player);

  if (gameOver) {
    winner = msg.player;
    Serial.printf("🏆 Player %d wins!\n", winner);
  } else if (checkDraw()) {
    gameOver = true;
    winner = 0;
    Serial.println("🤝 Draw");
  } else {
    currentPlayer = (currentPlayer == PLAYER1_ID) ? PLAYER2_ID : PLAYER1_ID;
    Serial.printf("➡️ Next turn: Player %d\n", currentPlayer);
  }

  drawBoard();
  broadcastState();

  if (gameOver) {
    gameEndTime = millis();
    waitingToReset = true;
  }
}

void drawBoard() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  if (gameOver) {
    if (winner == 0) {
      M5.Lcd.print("Draw!");
    } else {
      M5.Lcd.printf("Player %d Wins!", winner);
    }
  } else {
    M5.Lcd.printf("Player %d's Turn", currentPlayer);
  }

  for (int i = 0; i < 9; i++) {
    int x = 40 + (i % 3) * 60;
    int y = 60 + (i / 3) * 60;
    M5.Lcd.drawRect(x, y, 50, 50, WHITE);
    if (board[i] == PLAYER1_ID) {
      M5.Lcd.setCursor(x + 10, y + 10);
      M5.Lcd.print("X");
    } else if (board[i] == PLAYER2_ID) {
      M5.Lcd.setCursor(x + 10, y + 10);
      M5.Lcd.print("O");
    }
  }
}

bool checkWin(uint8_t player) {
  const int wins[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6}
  };
  for (auto &w : wins) {
    if (board[w[0]] == player && board[w[1]] == player && board[w[2]] == player)
      return true;
  }
  return false;
}

bool checkDraw() {
  for (int i = 0; i < 9; i++) {
    if (board[i] == 0) return false;
  }
  return true;
}

void broadcastState() {
  GameState state;
  memcpy(state.boardState, board, 9);
  state.gameOver = gameOver;
  state.winner = winner;
  esp_now_send(NULL, (uint8_t *)&state, sizeof(state));
  Serial.println("📡 Game state broadcasted to remotes");
}
