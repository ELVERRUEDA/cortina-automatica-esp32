// Librerías necesarias
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <time.h>
#include <vector>

// CONFIGURA TUS CREDENCIALES
const char* ssid = "Red_IoT";
const char* password = "Rapsoda25";
const char* firebase_project_id = "api-rest-fastapi-firebase";
const char* authToken = "";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  // Colombia (UTC -5)
const int daylightOffset_sec = 0;

Servo myServo;
const int servoPin = 14;

// Bandera para evitar ejecuciones repetidas en el mismo minuto
String ultimaHoraEjecutada = "";
std::vector<String> tareasEjecutadas;
bool tareaEnEjecucion = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Espera a que se sincronice la hora (máx 10 intentos)
  struct tm timeinfo;
  int intentos = 0;
  while (!getLocalTime(&timeinfo) && intentos < 10) {
    Serial.println("Esperando sincronización de hora...");
    delay(1000);
    intentos++;
  }

  if (intentos == 10) {
    Serial.println(" No se pudo obtener la hora. Continuando sin sincronización.");
  } else {
    Serial.println(" Hora sincronizada correctamente.");
  }

}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://firestore.googleapis.com/v1/projects/" + String(firebase_project_id) + "/databases/(default)/documents/tareas";
    http.begin(url);

    if (String(authToken).length() > 0) {
      http.addHeader("Authorization", "Bearer " + String(authToken));
    }

    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString(); 
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, payload);

      // Obtener hora actual
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("No se pudo obtener la hora");
        return;
      }

      char currentTime[6];  // HH:mm
      strftime(currentTime, sizeof(currentTime), "%H:%M", &timeinfo);
      Serial.print("Hora actual: ");
      Serial.println(currentTime);

      bool tareaEjecutadaEnEsteCiclo = false;

      // Revisamos todas las tareas
      for (JsonObject tarea : doc["documents"].as<JsonArray>()) {
        String tareaID = tarea["name"];
        tareaID = tareaID.substring(tareaID.lastIndexOf("/") + 1);

        String hora = tarea["fields"]["hora"]["stringValue"] | "";
        bool completada = tarea["fields"]["completada"]["booleanValue"] | false;

        // Verifica si la tarea debe ejecutarse
        if (!completada && hora == String(currentTime)) {
          if (!tareaEnEjecucion) {
            Serial.println("Ejecutando tarea: " + tareaID);
            tareaEnEjecucion = true;
            tareaEjecutadaEnEsteCiclo = true;

            // Mueve el servo
            myServo.attach(servoPin);  // Asegúrate de que está conectado
            myServo.write(90);
            delay(5000);               // Tiempo en esa posición
            myServo.write(0);
            delay(9000);                // Espera antes de apagarlo
            myServo.detach();          // Apaga el servo

            // PATCH a Firebase para marcar la tarea como completada
            HTTPClient patchHttp;
            String patchUrl = "https://firestore.googleapis.com/v1/projects/" + String(firebase_project_id) + "/databases/(default)/documents/tareas/" + tareaID + "?updateMask.fieldPaths=completada";
            patchHttp.begin(patchUrl);

            if (String(authToken).length() > 0) {
              patchHttp.addHeader("Authorization", "Bearer " + String(authToken));
            }
            patchHttp.addHeader("Content-Type", "application/json");

            String patchPayload = "{\"fields\": {\"completada\": {\"booleanValue\": true}}}";
            int patchCode = patchHttp.PATCH(patchPayload);

            if (patchCode == 200) {
              Serial.println("Tarea marcada como completada");
            } else {
              Serial.println("Error al actualizar tarea");
              Serial.println(patchHttp.getString());
            }

            patchHttp.end();
          }
        }
      }

      // Si no se ejecutó ninguna tarea, aseguramos que el servo no se active
      if (!tareaEjecutadaEnEsteCiclo && tareaEnEjecucion) {
        tareaEnEjecucion = false;
        Serial.println("Sin tareas para ejecutar. Servo en reposo.");
      } else if (!tareaEjecutadaEnEsteCiclo) {
        Serial.println("Sin tareas para ejecutar.");
      }

    } else {
      Serial.print("Error en la petición GET: ");
      Serial.println(httpCode);
    }

    http.end();
  }

  delay(10000);  // Espera 1 minuto antes de revisar nuevamente
}