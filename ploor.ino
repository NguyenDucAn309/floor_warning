#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <LiquidCrystal_I2C.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ==== WIFI ====
#define WIFI_SSID "Chann"
#define WIFI_PASSWORD "tamsochin"

// ==== FIREBASE ====
#define API_KEY "AIzaSyDUgxuK6XL6DZY8jNeYKsmvsd8XYuQ_GsY"
#define DATABASE_URL "https://floor-df6a0-default-rtdb.asia-southeast1.firebasedatabase.app/"

// ==== LCD I2C ====
// Địa chỉ I2C thường là 0x27 hoặc 0x3F
// SDA -> GPIO 21
// SCL -> GPIO 22
LiquidCrystal_I2C lcd(0x27, 16, 2); // Thử 0x27, nếu không được đổi thành 0x3F

// ==== CẢM BIẾN ====
const int trigPin = 23;
const int echoPin = 19;
const int ledPin = 2;
const int PinSensor = 18; // Flow sensor

// ==== BIẾN TOÀN CỤC ====
volatile int NumPulses = 0;
float factor_conversion = 7.11;  // Hz → L/min
float volume = 0;
float flow_L_m = 0;
unsigned long lastFlowTime = 0;
unsigned long lastFirebaseTime = 0;
unsigned long lastLcdUpdate = 0;
int lcdMode = 0; // 0: Distance, 1: Flow+Volume

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// ==== HÀM NGẮT ====
void IRAM_ATTR CountPulse() {
  NumPulses++;
}

// ==== HÀM ĐO KHOẢNG CÁCH ====
long readDistanceCM() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long start = micros();
  while (digitalRead(echoPin) == LOW) {
    if (micros() - start > 25000) return -1;
  }

  start = micros();
  while (digitalRead(echoPin) == HIGH) {
    if (micros() - start > 25000) return -1;
  }

  unsigned long duration = micros() - start;
  long distance = duration / 58;
  return distance;
}

// ==== KẾT NỐI WIFI ====
void connectWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ket noi WiFi...");
  
  Serial.print("🔌 Đang kết nối Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    lcd.setCursor(dots % 16, 1);
    lcd.print(".");
    dots++;
    delay(500);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Ket Noi!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  
  Serial.println("\n✅ Wi-Fi đã kết nối!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ==== CẬP NHẬT LCD ====
void updateLCD(long distance, float flow, float vol) {
  // Chế độ 1: Hiển thị khoảng cách
  if (lcdMode == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Khoang cach:    ");
    lcd.setCursor(0, 1);
    
    if (distance > 0) {
      lcd.print(distance);
      lcd.print(" cm      ");
    } else {
      lcd.print("Loi cam bien    ");
    }
  }
  // Chế độ 2: Hiển thị lưu lượng và thể tích
  else {
    lcd.setCursor(0, 0);
    lcd.print("Flow:");
    lcd.print(flow, 2);
    lcd.print("L/m  ");
    
    lcd.setCursor(0, 1);
    lcd.print("Vol:");
    lcd.print(vol, 2);
    lcd.print("L    ");
  }
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  
  // Khởi tạo LCD I2C
  lcd.begin();
  lcd.backlight(); // Bật đèn nền
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" He Thong IoT  ");
  lcd.setCursor(0, 1);
  lcd.print("  Khoi dong...  ");
  delay(2000);
  
  pinMode(ledPin, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(PinSensor, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PinSensor), CountPulse, RISING);
  connectWiFi();

  // Firebase setup
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ket noi Firebase");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("✅ Đăng ký Anonymous thành công!");
    signupOK = true;
    lcd.setCursor(0, 1);
    lcd.print("Firebase OK!    ");
  } else {
    Serial.printf("❌ Lỗi đăng ký: %s\n", config.signer.signupError.message.c_str());
    lcd.setCursor(0, 1);
    lcd.print("Firebase Loi!   ");
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  delay(2000);
  lcd.clear();
  Serial.println("✅ Hệ thống sẵn sàng!");
}

// ==== LOOP ====
void loop() {
  unsigned long now = millis();

  // 1️⃣ Đo khoảng cách (HY-SRF05)
  long cm = readDistanceCM();
  if (cm > 0) {
    digitalWrite(ledPin, (cm < 10) ? HIGH : LOW);
  }

  // 2️⃣ Đo lưu lượng mỗi 1 giây (S201)
  if (now - lastFlowTime >= 1000) {
    noInterrupts();
    int pulses = NumPulses;
    NumPulses = 0;
    interrupts();

    flow_L_m = pulses / factor_conversion;
    if (flow_L_m > 0) {
      volume += (flow_L_m / 60.0);
    } else {
      volume = 0;
    }

    lastFlowTime = now;

    Serial.print("📏 Distance: ");
    Serial.print(cm);
    Serial.println(" cm");

    Serial.print("💧 Flow: ");
    Serial.print(flow_L_m, 3);
    Serial.print(" L/min\tVolume: ");
    Serial.print(volume, 3);
    Serial.println(" L");
  }

  // 3️⃣ Cập nhật LCD mỗi 2 giây và đổi chế độ hiển thị
  if (now - lastLcdUpdate >= 2000) {
    updateLCD(cm, flow_L_m, volume);
    lcdMode = 1 - lcdMode; // Đổi giữa 0 và 1
    lastLcdUpdate = now;
  }

  // 4️⃣ Gửi dữ liệu Firebase mỗi 1 giây
  if (Firebase.ready() && signupOK && (now - lastFirebaseTime >= 1000)) {
    bool ok = true;
    ok &= Firebase.RTDB.setInt(&fbdo, "/sensor/ultrasonic/distance", cm);
    ok &= Firebase.RTDB.setFloat(&fbdo, "/sensor/flow/flow_L_min", flow_L_m);
    ok &= Firebase.RTDB.setFloat(&fbdo, "/sensor/flow/volume_L", volume);

    if (ok) {
      Serial.println("✅ Gửi dữ liệu Firebase thành công!");
    } else {
      Serial.print("❌ Firebase lỗi: ");
      Serial.println(fbdo.errorReason());
    }
    lastFirebaseTime = now;
  }
}