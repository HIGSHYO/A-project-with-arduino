#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

// Conexão automatica na internet
const char* ssid = "Nome_da_sua_internet_vai_aqui";
const char* password = "Senha_da_sua_internet_vai_aqui";
const char* scriptUrl = "URL_do_seu_script_de_envio_via_nuvem_vai_aqui"; // *ATENÇÃO: No codigo foi utilizado uma "API" do google web app para envio dos dados para a nuvem*

WiFiClientSecure client;
HTTPClient http;

void setup() {
  // Configuração de qual porta e qual rede wifi utilizar ignorando o SSL
  Serial.begin(9600);
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  client.setInsecure();
}

void loop() {
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    // Aqui o ESP espera pelo comando (GET_TIME) do arduino para enviar data e hora de cada testes
    if (comando == "GET_TIME") {
      sendFormattedDateTime();
    } else if (comando == "UPLOAD_CSV") {
      uploadCSVFromSerial(); // Já aqui ele chama a função de enviar os testes
    }
  }
}

// Função de envio de data e hora
void sendFormattedDateTime() {
  configTime(-3 * 3600, 0, "pool.ntp.org");
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d %02d:%02d:%02d", // Formata como deve ser enviado a data e hora
           t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
           t->tm_hour, t->tm_min, t->tm_sec);
  Serial.println(buffer);
}

// Função de envio dos dados registrados pelo Arduino
void uploadCSVFromSerial() {
  Serial.println("READY");

  String conteudoCSV = "";
  unsigned long timeout = millis();

  while (millis() - timeout < 5000) {
    if (Serial.available()) {
      String linha = Serial.readStringUntil('\n');
      linha.trim();

      if (linha == "<<END>>") break;
      conteudoCSV += linha + "\n";
      timeout = millis();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    http.begin(client, scriptUrl);
    http.addHeader("Content-Type", "text/plain");

    int response = http.POST(conteudoCSV);
    if (response > 0) {
      Serial.print("Upload OK: ");
      Serial.println(response);
    } else {
      Serial.print("Erro upload: ");
      Serial.println(http.errorToString(response));
    }

    http.end();
  } else {
    Serial.println("WiFi desconectado.");
  }
}