#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <avr/wdt.h>

#define SENSOR_REF      2
#define SENSOR_A        3
#define SENSOR_B        4
#define SENSOR_C        5
#define hidraulic_pin   6
#define SD_CS_PIN       8
#define DHTPIN          7
#define DHTTYPE         DHT11
#define BUZZER_PIN      9
#define ESP_SERIAL      Serial1
#define led_blue_pin    10
#define led_red_pin     11
#define led_green_pin   12
#define led_yellow_pin  13

#define LCD_ADDRESS     0x27
#define LCD_WIDTH       16
#define LCD_HEIGHT      2
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_WIDTH, LCD_HEIGHT);

DHT dht(DHTPIN, DHTTYPE);

// Configura como deve ser salvo os dados no cartão SD
const char* SD_FILENAME = "dados.csv";
const unsigned long FILTER_DELAY = 100;
const unsigned long DHT_INTERVAL = 1500;
unsigned long lastReadTimeRef = 0, lastReadTimeA = 0, lastReadTimeB = 0, lastReadTimeC = 0;
unsigned long startTime = 0;
unsigned long lastDHTRead = 0;
float lastTemp = NAN, lastHum = NAN;

bool sensorRefActive = false;
bool sensorAActive = false;
bool sensorBActive = false;
bool sensorCActive = false;

// Registra em qual teste está
int execucoes = 0;
bool sdDisponivel = true;

// Sistema de envio via ESP8266-01
unsigned long lastUpload = 0;
const unsigned long uploadInterval = 10000;

String lastLine1 = "", lastLine2 = "";

void setup() {
  // Seta todos os pinos utilizados para o dispositivo e os configura
  pinMode(SENSOR_REF, INPUT);
  pinMode(SENSOR_A, INPUT);
  pinMode(SENSOR_B, INPUT);
  pinMode(SENSOR_C, INPUT);
  pinMode(hidraulic_pin, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(hidraulic_pin, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(led_blue_pin, LOW);
  digitalWrite(led_red_pin, LOW);
  digitalWrite(led_green_pin, LOW);
  digitalWrite(led_yellow_pin, LOW);

  Serial.begin(9600);
  ESP_SERIAL.begin(9600);

  lcd.init();
  lcd.backlight();
  showLCD("Sistema", "Iniciando...");

  dht.begin();
  delay(500);

// Sistema para evitar que aja leitura se o SD não estiver conectado
  Serial.print("Iniciando SD...");
  delay(100);
  while (true) {
    if (SD.begin(SD_CS_PIN)) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      showLCD("SD", "conectado!");
      digitalWrite(led_green_pin, HIGH);
      digitalWrite(led_yellow_pin, LOW);
      delay(1000);
      break;
    } else {
      showLCD("Aguardando", "SD...");
      digitalWrite(led_green_pin, LOW);
      digitalWrite(led_blue_pin, LOW);
      digitalWrite(led_yellow_pin, LOW);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1300);
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(led_yellow_pin, HIGH);
      delay(1000);
    }
  }

  showLCD("SD", "Pronto!");
  showLCD("Aguardando", "...");

  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);

// Cria um arquivo .CSV para armazenar os dados dos teste e envialos de maneira segura para uma nuvem
  if (!SD.exists(SD_FILENAME)) {
    File file = SD.open(SD_FILENAME, FILE_WRITE);
    if (file) {
      file.println("Execucao, Sensor, Tempo(ms), Temp(C), Umidade(%), DataHora");
      file.close();
      Serial.println("Arquivo CSV criado com cabeçalho.");
    } else {
      Serial.println("Erro ao criar arquivo CSV!");
    }
  } else {
    Serial.println("Arquivo CSV já existe. Dados serão acrescentados.");
  }
}

void loop() {
  //mini checagem do cartão SD no inicio do código e renomeação de cada sensor infravermelho
  verificarSD();
  handleReferenceSensor();
  handleSensor(SENSOR_A, sensorAActive, "D3", lastReadTimeA);
  handleSensor(SENSOR_B, sensorBActive, "D4", lastReadTimeB);
  handleSensor(SENSOR_C, sensorCActive, "D5", lastReadTimeC);
  updateOutput();

// Para evitar muitos delays e para fazer o codigo fluir melhor adicionei
// Um sistema em millis para ainda sim ter delays, mas não influencia no codigo
  if (millis() - lastUpload >= uploadInterval) {
    enviarArquivoCSV();
    lastUpload = millis();
  }
}

void verificarSD() {
  File test = SD.open(SD_FILENAME, FILE_WRITE);
  if (!test) {
    if (sdDisponivel) {
      Serial.println("FALHA NO CARTÃO SD!");
      sdDisponivel = false;
      digitalWrite(led_green_pin, LOW);
      digitalWrite(led_blue_pin, LOW);
      digitalWrite(led_yellow_pin, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1300);
      digitalWrite(BUZZER_PIN, LOW);
      showLCD("Erro SD!", "Reiniciando...");
      delay(1500);
      wdt_enable(WDTO_15MS);
      while (1);
    }
  } else {
    test.close();
    if (!sdDisponivel) {
      Serial.println("Cartão SD reconectado.");
      showLCD("SD", "Reconectado");
    }
    sdDisponivel = true;
  }
}

// Inicio do teste lógico para ativar a contagem entre os testes
// No codigo abaixo ele especifica que um dos sensores vai servir como tres triggers
void handleReferenceSensor() {
  if (digitalRead(SENSOR_REF) == LOW && !sensorRefActive) {
    if (millis() - lastReadTimeRef >= FILTER_DELAY) {
      lastReadTimeRef = millis();
      startTime = millis(); // Um ativa o timer para registro dos tempos
      sensorRefActive = true;
      digitalWrite(hidraulic_pin, LOW); // Outro para desativar o pistão
      digitalWrite(led_blue_pin, HIGH);  // E por final um para desativar uma led que serve como referência de estado
      execucoes++; // Já aqui ele adiciona +1 para as execuções feitas
      Serial.println("Sensor de referência ativado!");
      showLCD("Ref ativado", "Exec: " + String(execucoes));
    }
  } else if (digitalRead(SENSOR_REF) == HIGH) {
    sensorRefActive = false;
  }
}

// Aqui faz a parte final do codigo que registra tempos, humidade que aconteceu o teste, temperatura
// E envia os dados para o SD
void handleSensor(int pin, bool &flag, const String &label, unsigned long &lastReadTime) {
  if (digitalRead(pin) == LOW && !flag && execucoes >= 1) {
    if (millis() - lastReadTime >= FILTER_DELAY) {
      lastReadTime = millis();
      unsigned long elapsed = millis() - startTime;
      flag = true;

      if (millis() - lastDHTRead >= DHT_INTERVAL) {
        lastTemp = dht.readTemperature();
        lastHum  = dht.readHumidity();
        lastDHTRead = millis();
      }

      String tempStr = isnan(lastTemp) ? "erro" : String(lastTemp, 1);
      String humStr  = isnan(lastHum)  ? "erro" : String(lastHum, 1);

      Serial.print("Sensor "); Serial.print(label);
      Serial.print(" - Tempo: "); Serial.print(elapsed);
      Serial.print(" ms, Temp: "); Serial.print(tempStr);
      Serial.print(" C, Umid: "); Serial.println(humStr);

      showLCD("Sensor " + label, String(elapsed) + "ms T:" + tempStr + "C");
      delay(200);
      showLCD("Umid:" + humStr + "%", "Sensor " + label);

      logToSD(execucoes, label, elapsed, lastTemp, lastHum);
    }
  } else if (digitalRead(pin) == HIGH && flag) {
    flag = false;
    Serial.print("Sensor "); Serial.print(label); Serial.println(" voltou a HIGH.");
  }
}

// Aqui reinicia o sistema desligando o led e dastivando o pistão para entrar
// Entrar em contato com o sensor de referência ou com o sensor de registro de tempos criando um loop de forma fisica também
void updateOutput() {
  if (digitalRead(SENSOR_A) == LOW && digitalRead(SENSOR_B) == LOW && digitalRead(SENSOR_C) == LOW) {
    digitalWrite(hidraulic_pin, HIGH);
    digitalWrite(led_blue_pin, LOW);
  } else if (sensorRefActive) {
    digitalWrite(hidraulic_pin, LOW);
    digitalWrite(led_blue_pin, HIGH);
  }
}

// Configura a forma que será utilizado o LCD em todo o código
void showLCD(const String &line1, const String &line2) {
  if (line1 != lastLine1 || line2 != lastLine2) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
    lastLine1 = line1;
    lastLine2 = line2;
  }
}

// Função para obter data e hora utilizando UDP 
String obterDataHora() {
  const int tentativasMax = 3;
  int tentativa = 0;

  while (tentativa < tentativasMax) {
    ESP_SERIAL.println("GET_TIME");
    unsigned long start = millis();

    while (!ESP_SERIAL.available()) {
      if (millis() - start > 5000) break;
      delay(1);
    }

    if (ESP_SERIAL.available()) {
      String dataHora = ESP_SERIAL.readStringUntil('\n');
      dataHora.trim();

      Serial.print("Resposta recebida do ESP: [");
      Serial.print(dataHora);
      Serial.println("]");

      if (dataHora == "WIFI_FAIL") {
        Serial.println("ESP8266 não conectado ao Wi-Fi!");
        if (tentativa == 0) {
          digitalWrite(BUZZER_PIN, HIGH);
          delay(2000);
          digitalWrite(BUZZER_PIN, LOW);
          digitalWrite(led_red_pin, HIGH);
        }
      } else {
        if (dataHora.length() >= 17 && dataHora.indexOf('/') != -1 && dataHora.indexOf(':') != -1) {
          return dataHora;
        }
      }
    }

    tentativa++;
    delay(500);
  }

  return "00/00/0000 00:00:00";
}

// Registro de todos os dados obtidos anteriomente no SD
void logToSD(int exec, const String &sensor, unsigned long tempo, float temperature, float humidity) {
  String dataHora = obterDataHora();

  if (dataHora == "00/00/0000 00:00:00") {
    Serial.println("Atenção: Data/Hora não recebida corretamente do ESP!");
  }

  File file = SD.open(SD_FILENAME, FILE_WRITE);
  if (file) {
    file.print(exec); file.print(", ");
    file.print(sensor); file.print(", ");
    file.print(tempo); file.print(", ");
    file.print(temperature); file.print(", ");
    file.print(humidity); file.print(", ");
    file.println(dataHora);
    file.close();
    Serial.print("Gravado no SD com data: "); Serial.println(dataHora);
  } else {
    Serial.println("Erro ao abrir arquivo SD.");
  }
}

// Envio dos dados via nuvem 
void enviarArquivoCSV() {
  Serial.println("Iniciando envio do arquivo CSV...");

  File file = SD.open(SD_FILENAME, FILE_READ);
  if (!file) {
    Serial.println("Erro ao abrir o arquivo CSV para envio.");
    showLCD("Erro:", "SD leitura falha");
    return;
  }

  while (ESP_SERIAL.available()) ESP_SERIAL.read();

  ESP_SERIAL.println("UPLOAD_CSV");

  unsigned long start = millis();
  while (!ESP_SERIAL.available()) {
    if (millis() - start > 3000) {
      Serial.println("ESP não respondeu ao comando UPLOAD_CSV.");
      file.close();
      showLCD("Erro:", "Sem resposta");
      return;
    }
  }

  String resposta = ESP_SERIAL.readStringUntil('\n');
  resposta.trim();
  if (resposta != "READY") {
    Serial.println("ESP não está pronto para receber.");
    file.close();
    return;
  }

  while (file.available()) {
    String linha = file.readStringUntil('\n');
    linha.trim();
    if (linha.length() > 0) {
      ESP_SERIAL.println(linha);
      delay(2);
    }
  }
  file.close();

  ESP_SERIAL.println("<<END>>");
  Serial.println("Arquivo CSV enviado.");

  start = millis();
  while (!ESP_SERIAL.available()) {
    if (millis() - start > 5000) {
      Serial.println("Sem confirmação do ESP.");
      showLCD("Erro:", "Sem confirmação");
      return;
    }
  }

  String status = ESP_SERIAL.readStringUntil('\n');
  status.trim();
  if (status.startsWith("Upload OK")) {
    Serial.println("Upload confirmado!");
    showLCD("Upload:", "Concluído");
    delay(1000);
    showLCD("Aguardando", "...");
  } else {
    Serial.print("Falha no upload: ");
    Serial.println(status);
    showLCD("Erro:", status);
    delay(1000);
    showLCD("Aguardando", "...");
  }
}
