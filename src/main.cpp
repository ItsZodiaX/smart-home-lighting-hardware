#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Bounce2.h>

#define BUTTON 27
#define LDR 34
#define GREEN 33
#define YELLOW 25
#define RED 26
#define touch1 2
#define touch2 13

const char *ssid = "VjumpKunG";
const char *password = "Kungjump-1";

Bounce debouncer = Bounce();

bool active[3] = {0, 0, 0}; // 0 = off, 1 = on
int level[3] = {0, 0, 0};   // 0-255
bool mode[3] = {0, 0, 0};   // 0 = manual, 1 = auto

bool touch[2] = {0, 0};
int touches[2] = {touch1, touch2};

int threshold = 25;
bool touch1detected = false;
bool touch2detected = false;
const long touchDelay = 1800; // ms
volatile unsigned long sinceLastTouch1 = 0;
volatile unsigned long sinceLastTouch2 = 0;

TaskHandle_t POST_StatusHandle;

void Connect_Wifi();
void Solve(void *param);
void POST_Status(void *param);
void GET_Status(void *param);
void GET_Mode(void *param);
void GET_Level(void *param);

bool touchDelayComp(unsigned long lastTouch)
{
  if (millis() - lastTouch < touchDelay)
    return false;
  return true;
}

void touch1detect()
{
  if (touchDelayComp(sinceLastTouch1))
  {
    sinceLastTouch1 = millis();
    touch[0] = true;
  }
}
void touch2detect()
{
  if (touchDelayComp(sinceLastTouch2))
  {
    sinceLastTouch2 = millis();
    touch[1] = true;
  }
}

void setup()
{
  Serial.begin(115200);

  debouncer.attach(BUTTON, INPUT_PULLUP);
  debouncer.interval(25);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(GREEN, 0);
  ledcSetup(1, 5000, 8);
  ledcAttachPin(YELLOW, 1);
  ledcSetup(2, 5000, 8);
  ledcAttachPin(RED, 2);

  touchAttachInterrupt(touch1, touch1detect, threshold);
  touchAttachInterrupt(touch2, touch2detect, threshold);

  Connect_Wifi();

  xTaskCreatePinnedToCore(GET_Status, "GET_Status", 65536, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(Solve, "Solve", 1000, NULL, 2, NULL, 1);
}

void loop()
{
}

void Solve(void *param)
{
  while (1)
  {
    int l = map(analogRead(LDR), 2100, 3700, 0, 255);
    for (int i = 0; i < 3; i++)
    {
      if (mode[i])
      {
        if (l < 127)
        {
          ledcWrite(i, level[i]);
        }
        else
        {
          ledcWrite(i, 0);
        }
      }
      else
      {
        if (i == 0)
        {
          debouncer.update();
          if (debouncer.fell() && !mode[i])
          {
            int j = 0;
            xTaskCreatePinnedToCore(POST_Status, "POST_Status", 10000, (void *)&j, 2, &POST_StatusHandle, 0);
          }
        }
        else
        {
          if (touch[i - 1] && !mode[i])
          {
            int j = i;
            touch[i - 1] = false;
            xTaskCreatePinnedToCore(POST_Status, "POST_Status", 10000, (void *)&j, 2, &POST_StatusHandle, 0);
          }
        }
        ledcWrite(i, active[i] ? level[i] : 0);
      }
    }
  }
}

// Update room status on/off
// Calling this whenever button/touch is pressed & mode is manual
void POST_Status(void *param)
{
  while (1)
  {
    // POST to server with room id & status (!active[i])
    int i = *(int *)param;
    char buffer[100];
    sprintf(buffer, "http://192.168.136.167:8000/room/manual/turn_off/%d", i + 1);
    if (!active[i])
    {
      sprintf(buffer, "http://192.168.136.167:8000/room/manual/turn_on/%d", i + 1);
    }
    Serial.println(buffer);
    String json;
    HTTPClient http;
    http.begin(buffer);
    int httpResponseCode = http.POST(json);
    if (httpResponseCode == 200)
    {
      Serial.println("Done");
      http.end();
    }
    else
    {
      http.end();
      Serial.print("POST Error code: ");
      Serial.println(httpResponseCode);
    }
    // Also delete this task when sucessfully POST to server
    if (POST_StatusHandle != NULL)
    {
      vTaskDelete(POST_StatusHandle);
    }
  }
}

// Update room status every seconds
void GET_Status(void *param)
{
  while (1)
  {
    // Don't update active map if mode is auto?
    DynamicJsonDocument doc(65536);
    HTTPClient http;
    http.begin("http://192.168.136.167:8000/room/get_all_bulbs_info/");
    http.setTimeout(10000);

    int httpResponseCode = http.GET();
    if (httpResponseCode == 200)
    {
      String payload = http.getString();
      deserializeJson(doc, payload);
      JsonObject rooms = doc.as<JsonObject>();
      active[0] = rooms["room_1"].as<JsonObject>()["status"].as<bool>();
      level[0] = rooms["room_1"].as<JsonObject>()["brightness"].as<int>();
      mode[0] = rooms["room_1"].as<JsonObject>()["mode"].as<String>() != "manual";
      active[1] = rooms["room_2"].as<JsonObject>()["status"].as<bool>();
      level[1] = rooms["room_2"].as<JsonObject>()["brightness"].as<int>();
      mode[1] = rooms["room_2"].as<JsonObject>()["mode"].as<String>() != "manual";
      active[2] = rooms["room_3"].as<JsonObject>()["status"].as<bool>();
      level[2] = rooms["room_3"].as<JsonObject>()["brightness"].as<int>();
      mode[2] = rooms["room_3"].as<JsonObject>()["mode"].as<String>() != "manual";
      http.end();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    else
    {
      http.end();
      Serial.print("GET Error code: ");
      Serial.println(httpResponseCode);
    }
  }
}

void Connect_Wifi()
{
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.print("OK! IP=");
  Serial.println(WiFi.localIP());
  Serial.println("----------------------------------");
}