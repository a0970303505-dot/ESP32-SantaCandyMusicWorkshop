#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32_Servo.h>

// WiFi 設定
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

WiFiUDP udp;
const char *serverIP = "172.20.10.3"; // 請根據實際 Server IP 調整
unsigned int serverPort = 12345;
unsigned int localClientPort = 8888;

// 硬體設定
const int buzzerPin = 26; 
const int touchPins[8] = {13, 12, 14, 27, 33, 32, 15, 4}; 
const int ledPins[8] = {16, 17, 5, 18, 19, 21, 22, 23};  

Servo motor;
const int motorPin = 25;

int touchThreshold = 30;
bool ledStates[8] = {false, false, false, false, false, false, false, false};

int noteFrequencies[8] = {262,294,330,349,392,440,494,523}; // C D E F G A B C

// 按鈕功能模式相關
bool buttonModeOn = false;
unsigned long lastToggleTime = 0;
unsigned long toggleInterval = 1000;
bool motorAt90 = false;

// 特殊閃爍模式相關
bool motorModeOn = false; 
int motorModeStep = 0;
unsigned long lastBlinkTime = 0;
int blinkCount = 0;
bool ledOn = false;

// 建議加入的函式，用於啟動特殊閃爍模式(取代原本motor_start)
void startMotorMode() {
  motorModeOn = true;
  motorModeStep = 1; 
  blinkCount = 0;
  ledOn = false;
  lastBlinkTime = millis();
}

// 結束特殊閃爍模式(取代原本motor_stop)
void stopMotorMode() {
  motorModeOn = false;
  motorModeStep = 0;
  // 關閉所有LED
  for (int i = 0; i < 8; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

// 跑馬燈效果
void runMarquee() {
  for (int i = 0; i < 8; i++) {
    digitalWrite(ledPins[i], HIGH);
    delay(100);
    digitalWrite(ledPins[i], LOW);
  }
}

void setup() {
  Serial.begin(115200);

  // 硬體初始化
  for (int i = 0; i < 8; i++) {
    pinMode(ledPins[i], OUTPUT);
  }
  pinMode(buzzerPin, OUTPUT);
  
  motor.attach(motorPin);
  motor.write(0);

  // WiFi連接
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  udp.begin(localClientPort);
}

void loop() {
  // 偵測觸摸
  for (int i = 0; i < 8; i++) {
    int touchValue = touchRead(touchPins[i]);
    bool isTouched = (touchValue < touchThreshold);
    if (isTouched && !ledStates[i]) {
      handleTouch(i, touchValue);
    } else if (!isTouched && ledStates[i]) {
      digitalWrite(ledPins[i], LOW);
      ledStates[i] = false;
    }
  }

  // 接收 Server 封包
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char incomingPacket[255];
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = 0;
    }
    String receivedMessage = String(incomingPacket);
    Serial.print("Received message: ");
    Serial.println(receivedMessage);

    if (receivedMessage == "button_pressed") {
      // 現在此封包同時代表馬達擺動與LED特殊閃爍開始
      buttonModeOn = true;
      motor.write(0);
      motorAt90 = false;
      lastToggleTime = millis();
      // 開啟特殊閃爍模式(原本motor_start)
      startMotorMode();

    } else if (receivedMessage == "button_released") {
      // 現在此封包同時代表馬達擺動與LED特殊閃爍停止
      buttonModeOn = false;
      motor.write(0);
      // 關閉特殊閃爍模式(原本motor_stop)
      stopMotorMode();

    } else if (receivedMessage == "motion_detected") {
      // 執行一次跑馬燈效果，不變
      runMarquee();
    }
  }

  // 按鈕模式馬達擺動
  if (buttonModeOn) {
    unsigned long currentTime = millis();
    if (currentTime - lastToggleTime >= toggleInterval) {
      toggleMotorPosition();
      lastToggleTime = currentTime;
    }
  }

  // 特殊閃爍模式執行
  if (motorModeOn) {
    handleMotorMode();
  }
}

void handleTouch(int index, int touchValue) {
  Serial.print("Touched at index ");
  Serial.print(index);
  Serial.print(" with value ");
  Serial.println(touchValue);

  ledStates[index] = true;
  lightUpLED(index);
  playTone(index);
  sendTouchData(index);
}

void lightUpLED(int index) {
  digitalWrite(ledPins[index], HIGH);
}

void playTone(int index) {
  tone(buzzerPin, noteFrequencies[index], 200);
}

void sendTouchData(int index) {
  String message = "touch:";
  message += index;
  udp.beginPacket(serverIP, serverPort);
  udp.print(message);
  udp.endPacket();
}

// 在按鈕模式下馬達0度與90度來回切換
void toggleMotorPosition() {
  if (motorAt90) {
    motor.write(0);
    motorAt90 = false;
  } else {
    motor.write(180);
    motorAt90 = true;
  }
}

// 特殊閃爍模式處理(將原本motorModeStep的LED閃爍行為整合於此)
void handleMotorMode() {
  unsigned long currentMillis = millis();

  // 根據motorModeStep來控制LED閃爍節奏
  // Step與原程式邏輯相同(慢閃、快閃、跑馬燈)，只是觸發時機現在由button_pressed和button_released控制
  int blinkInterval;
  int blinkCountLimit;

  switch (motorModeStep) {
    case 1: // 單數LED 慢閃數次
      blinkInterval = 500;
      blinkCountLimit = 6;
      if (currentMillis - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = currentMillis;
        ledOn = !ledOn;
        for (int i = 0; i < 8; i += 2) {
          digitalWrite(ledPins[i], ledOn ? HIGH : LOW);
        }
        blinkCount++;
        if (blinkCount >= blinkCountLimit) {
          // 切換到下一階段
          motorModeStep = 2;
          blinkCount = 0;
          ledOn = false;
        }
      }
      break;

    case 2: // 偶數LED 快閃數次
      blinkInterval = 200;
      blinkCountLimit = 10;
      if (currentMillis - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = currentMillis;
        ledOn = !ledOn;
        for (int i = 1; i < 8; i += 2) {
          digitalWrite(ledPins[i], ledOn ? HIGH : LOW);
        }
        blinkCount++;
        if (blinkCount >= blinkCountLimit) {
          motorModeStep = 3;
          blinkCount = 0;
          ledOn = false;
        }
      }
      break;

    case 3: // 單數LED 快閃數次
      blinkInterval = 200;
      blinkCountLimit = 10;
      if (currentMillis - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = currentMillis;
        ledOn = !ledOn;
        for (int i = 0; i < 8; i += 2) {
          digitalWrite(ledPins[i], ledOn ? HIGH : LOW);
        }
        blinkCount++;
        if (blinkCount >= blinkCountLimit) {
          motorModeStep = 4;
          blinkCount = 0;
          ledOn = false;
        }
      }
      break;

    case 4: // 偶數LED 再次快閃數次
      blinkInterval = 200;
      blinkCountLimit = 10;
      if (currentMillis - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = currentMillis;
        ledOn = !ledOn;
        for (int i = 1; i < 8; i += 2) {
          digitalWrite(ledPins[i], ledOn ? HIGH : LOW);
        }
        blinkCount++;
        if (blinkCount >= blinkCountLimit) {
          motorModeStep = 5;
          blinkCount = 0;
          ledOn = false;
        }
      }
      break;

    case 5: // 執行跑馬燈
      runMarquee();
      // 跑馬燈結束後回到Step 1
      motorModeStep = 1;
      blinkCount = 0;
      ledOn = false;
      break;

    default:
      break;
  }
}
