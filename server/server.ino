#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32_Servo.h>
#include <U8g2lib.h>

// Wi-Fi 設定
const char *ssid = "YOUR_WIFI_SSID";  // WiFi 名稱
const char *password = "YOUR_WIFI_PASSWORD";  // WiFi 密碼

WiFiUDP udp;
const char *clientIP = "172.20.10.6";  
unsigned int clientPort = 8888; // 與 Client 使用相同埠口

// OLED 設定 
#define SCREEN_WIDTH 128  // OLED 寬度
#define SCREEN_HEIGHT 64  // OLED 高度
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);  // 設定 I2C 顯示屏

// 硬體設定
const int pirPin = 34;  // PIR 感應器引腳
int pirState = LOW;     // PIR 感應器狀態
Servo servo1;           // 作服馬達 1
Servo servo2;           // 作服馬達 2
Servo servo3;           // 新增的作服馬達
const int fanPin = 15;  // 風扇續電器引腳
const int buzzerPin = 13; // 蜂鳴器引腳

// RGB LED 設定
const int rgb1Pins[] = {25, 26, 33}; // RGB LED1 的 R, G, B
const int rgb2Pins[] = {18, 19, 23}; // RGB LED2 的 R, G, B
const int buttonPin = 27;            // 按鈕引腳
bool randomFlash = false;            // 是否啟用隨機閃爍狀態
bool buttonLastState = HIGH;         // 按鈕前一次的狀態
bool buttonCurrentState = HIGH;      // 按鈕當前狀態
int buttonPressCount = 0;            // 按鈕按下次數

// 聖誕歌曲旋律與節奏
int note[] = {
  260, 347, 347, 390, 347, 328, 292, 292, 292,
  390, 390, 437, 390, 347, 328, 260, 260,
  437, 437, 463, 437, 390, 347, 292, 260, 260,
  292, 390, 328, 347
};
int duration[] = {
  125, 125, 62.5, 62.5, 62.5, 62.5, 125, 125, 125,
  125, 62.5, 62.5, 62.5, 62.5, 125, 125, 125,
  125, 62.5, 62.5, 62.5, 62.5, 125, 125, 62.5, 62.5,
  125, 125, 125, 250
};

// 音樂播放狀態
bool isPlayingSong = false;
int currentNote = 0;

// 時間管理
unsigned long pirActivatedTime = 0;
unsigned long servo3ResetTime = 0;

// 顯示的字串
const char *messages[] = {
  "剩單快樂",
  "樹你最棒",
  "叮叮噹 叮叮噹"
};
const int numMessages = sizeof(messages) / sizeof(messages[0]);
int messageState = 0;  // 記錄目前顯示的字串狀態

// 顯示時間管理
unsigned long lastUpdateTime = 0;  
unsigned long updateInterval = 2000; // 2 秒更新字串

// 模式變數
int currentMode = 0;  // 0 表示 PIR 感涉模式, 1 表示按鈕功能模式

bool servoMoving = false;
static bool servoAt90 = false;
static unsigned long lastToggleTime = 0;
unsigned long toggleInterval = 1000; // 可調整切換間隔(毫秒)

void setup() {
  Serial.begin(115200);

  // 硬體初始化
  pinMode(pirPin, INPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(rgb1Pins[i], OUTPUT);
    pinMode(rgb2Pins[i], OUTPUT);
  }
  pinMode(buttonPin, INPUT_PULLUP);

  servo1.attach(14);
  servo2.attach(17);
  servo3.attach(5);
  servo1.write(0);
  servo2.write(0);
  servo3.write(0);

  // Wi-Fi 連接
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  
  // Server 側使用 12345 埠接收
  udp.begin(12345);

  // 初始化 OLED
  u8g2.begin();
  u8g2.enableUTF8Print();  // 啟用 UTF-8 打印
  
}

void loop() {
  pirState = digitalRead(pirPin);
  buttonCurrentState = (digitalRead(buttonPin) == LOW);

  if (buttonCurrentState && !buttonLastState) {
    // 按鈕按下時切換模式
    buttonPressCount++; 

    if (buttonPressCount % 2 == 1) {
      // 切換至按鈕功能模式
      currentMode = 1;
      randomFlash = true;
      servo1.write(0);  // 初始位置先在0度
      servo2.write(0);
      servoAt90 = false; 
      lastToggleTime = millis();
      servoMoving = true; // 開始持續移動(0<->90)

      // 傳送按鈕按下的訊息給 Client (同時代表LED特效與馬達開始)
      sendButtonPressed();

    } else {
      // 切換回 PIR 感測模式
      currentMode = 0;
      randomFlash = false;
      servo1.write(0);
      servo2.write(0);
      servoMoving = false; // 停止持續移動

      // 傳送回到 PIR 模式的訊息給 Client (同時代表LED特效與馬達停止)
      sendButtonReleased();
    }
  }

  buttonLastState = buttonCurrentState;

  if (currentMode == 0) {  // PIR 感測模式
    if (pirState == HIGH && !randomFlash) {
      Serial.printf("Motion detected! State: %d\n", messageState);

      // 傳送運動偵測訊息給 Client
      sendMotionDetected();

      // 控制風扇與伺服馬達
      digitalWrite(fanPin, HIGH);
      servo2.write(270);
      servo1.write(270);

      // 觸發 servo3 動作
      servo3.write(180 - 90);
      pirActivatedTime = millis();
      servo3ResetTime = pirActivatedTime + 200; 

      // 播放聖誕歌曲
      if (!isPlayingSong) {
        isPlayingSong = true;
        currentNote = 0;
      }

      // 更新 OLED 顯示
      displayMessage();
    } else {
      Serial.println("No motion detected.");
      digitalWrite(fanPin, LOW);
      servo2.write(0);
      servo1.write(0);
    }

    // 處理 servo3 歸位
    if (servo3ResetTime > 0 && millis() >= servo3ResetTime) {
      servo3.write(180 - 0);
      servo3ResetTime = 0;
    }

    // 處理歌曲播放
    handleSongPlayback();
  }

  // 處理 RGB LED 隨機閃爍
  if (randomFlash) {
    randomFlashRGBLEDs();
  } else {
    resetRGBLEDs();
  }

  // 在按鈕功能模式下，持續讓servo1與servo2在0度與90度間來回切換
  if (currentMode == 1 && servoMoving) {
    if (millis() - lastToggleTime >= toggleInterval) {
      if (servoAt90) {
        servo1.write(0);
        servo2.write(0);
        servoAt90 = false;
      } else {
        servo1.write(270);
        servo2.write(270);
        servoAt90 = true;
      }
      lastToggleTime = millis();
    }
  }

  delay(100);
}

// 傳送按鈕按下的訊息給 Client (現在同時代表馬達與LED特效啟動)
void sendButtonPressed() {
  String message = "button_pressed";
  udp.beginPacket(clientIP, clientPort);
  udp.print(message);
  udp.endPacket();
  Serial.println("Sent button pressed message (with LED and motor start) to client.");
}

// 傳送回到 PIR 模式的訊息給 Client (現在同時代表馬達與LED特效停止)
void sendButtonReleased() {
  String message = "button_released";
  udp.beginPacket(clientIP, clientPort);
  udp.print(message);
  udp.endPacket();
  Serial.println("Sent button released message (with LED and motor stop) to client.");
}

// 傳送運動偵測訊息給 Client
void sendMotionDetected() {
  String message = "motion_detected";
  udp.beginPacket(clientIP, clientPort);
  udp.print(message);
  udp.endPacket();
  Serial.println("Sent motion detected message to client.");
}

// 非阻塞播放聖誕歌曲
void handleSongPlayback() {
  static unsigned long noteStartTime = 0;
  static bool isTonePlaying = false;

  if (isPlayingSong && currentNote < 30) {
    unsigned long currentTime = millis();
    int noteDuration = duration[currentNote];

    if (!isTonePlaying) {
      tone(buzzerPin, note[currentNote], noteDuration);
      noteStartTime = currentTime;
      isTonePlaying = true;
    } else if (currentTime - noteStartTime >= (unsigned long)noteDuration) {
      noTone(buzzerPin);
      noteStartTime = currentTime;
      isTonePlaying = false;
      currentNote++;
    }

    if (currentNote >= 30) {
      isPlayingSong = false;
    }
  }
}
// 更新 OLED 顯示文字
void displayMessage() {
  static unsigned long lastMotionTime = 0; // 上一次顯示消息的時間

  // 如果距離上次顯示時間過短，不更新顯示
  if (millis() - lastMotionTime < 1000) {
    return;  // 1秒內不更新顯示
  }

  // 計算顯示的訊息編號
  int messageIndex = messageState % numMessages;

  // 清空顯示
  u8g2.clearBuffer();

  // 顯示訊息
  
  u8g2.setFont(u8g2_font_unifont_t_chinese1);  // 設定字體
   u8g2.firstPage();
  do {
    u8g2.setCursor(2, 32);
    u8g2.print(messages[messageIndex]);  // 顯示訊息
  } while ( u8g2.nextPage() );

  // 記錄這次顯示的時間
  lastMotionTime = millis();
  messageState++;
}

// 隨機 RGB LED 閃爍
void randomFlashRGBLEDs() {
  int randomRed = random(0, 2);
  int randomGreen = random(0, 2);
  int randomBlue = random(0, 2);

  analogWrite(rgb1Pins[0], randomRed * 255);
  analogWrite(rgb1Pins[1], randomGreen * 255);
  analogWrite(rgb1Pins[2], randomBlue * 255);

  analogWrite(rgb2Pins[0], randomRed * 255);
  analogWrite(rgb2Pins[1], randomGreen * 255);
  analogWrite(rgb2Pins[2], randomBlue * 255);
}


// 恢復 RGB LED 狀態
void resetRGBLEDs() {
  for (int i = 0; i < 3; i++) {
    analogWrite(rgb1Pins[i], 0);
    analogWrite(rgb2Pins[i], 0);
  }
}
