#include <iostream>
#include <queue>
#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Bounce2.h>

using namespace std;

#define BUTTON 27
#define LDR 34
#define GREEN 33
#define YELLOW 25
#define RED 26
#define TOUCH_1 2
#define TOUCH_2 13

const char *ssid = "VjumpKunG";
const char *password = "Kungjump-1";

Bounce debouncer = Bounce();

queue<int> q;

int threshold = 25;
const long touchDelay = 1800; // ms
volatile unsigned long touch[2] = {0, 0};

bool active[3] = {0, 0, 0}; // 0 = off, 1 = on
int level[3] = {0, 0, 0};   // 0-255
bool mode[3] = {0, 0, 0};   // 0 = manual, 1 = auto

void Connect_Wifi();
void Solve(void *param);
void POST_Status(void *param);
void GET_Status(void *param);
void GET_Mode(void *param);
void GET_Level(void *param);

bool touchDelayComp(unsigned long lastTouch)
{
  return millis() - lastTouch >= touchDelay;
}

void touch1Detect()
{
  if (touchDelayComp(touch[0]))
  {
    touch[0] = millis();
    if (!mode[1])
    {
      q.push(1);
    }
  }
}
void touch2Detect()
{
  if (touchDelayComp(touch[1]))
  {
    touch[1] = millis();
    if (!mode[2])
    {
      q.push(2);
    }
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

  touchAttachInterrupt(TOUCH_1, touch1Detect, threshold);
  touchAttachInterrupt(TOUCH_2, touch2Detect, threshold);

  Connect_Wifi();

  xTaskCreatePinnedToCore(GET_Status, "GET_Status", 65536, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(POST_Status, "POST_Status", 20000, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(Solve, "Solve", 1000, NULL, 1, NULL, 0);
}

void loop()
{
  // int l = map(analogRead(LDR), 2100, 3700, 0, 255);
  // for (int i = 0; i < 3; i++)
  // {
  //   if (mode[i])
  //   {
  //     if (l < 127)
  //     {
  //       ledcWrite(i, level[i]);
  //     }
  //     else
  //     {
  //       ledcWrite(i, 0);
  //     }
  //   }
  //   else
  //   {
  //     if (i == 0)
  //     {
  //       debouncer.update();
  //       if (debouncer.fell() && !mode[i])
  //       {
  //         q.push(0);
  //       }
  //     }

  //     ledcWrite(i, active[i] ? level[i] : 0);
  //   }
  // }
}

void Solve(void *param)
{
  while (1)
  {
    Serial.println("Solve");
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
            q.push(0);
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
    Serial.println("POST_Status");
    while (!q.empty())
    {
      // POST to server with room id & status (!active[i])
      int i = q.front();
      char buffer[100];
      sprintf(buffer, "http://192.168.136.167:8000/room/manual/turn_off/%d", i + 1);
      if (!active[i])
      {
        sprintf(buffer, "http://192.168.136.167:8000/room/manual/turn_on/%d", i + 1);
      }
      String json;
      HTTPClient http;
      http.begin(buffer);
      Serial.printf("POST: room: %d, status: %s\n", i + 1, active[i] ? "off" : "on");
      int httpResponseCode = http.POST(json);
      if (httpResponseCode == 200)
      {
        http.end();
        Serial.printf("POST Result: room: %d, status: %s\n", i + 1, active[i] ? "off" : "on");
        q.pop();
      }
      else
      {
        http.end();
        Serial.printf("POST Error code: %d\n", httpResponseCode);
      }
    }
  }
}

// Update room status every seconds
void GET_Status(void *param)
{
  while (1)
  {
    Serial.println("GET_Status");
    //  Don't update active map if mode is auto?
    DynamicJsonDocument doc(65536);
    HTTPClient http;
    http.begin("http://192.168.136.167:8000/room/get_all_bulbs_info/");

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
      Serial.printf("GET Result: room: %d, status: %d, brightness: %d, mode: %s\n", 1, active[0], level[0], mode[0] ? "auto" : "manual");
      Serial.printf("GET Result: room: %d, status: %d, brightness: %d, mode: %s\n", 2, active[1], level[1], mode[1] ? "auto" : "manual");
      Serial.printf("GET Result: room: %d, status: %d, brightness: %d, mode: %s\n", 3, active[2], level[2], mode[2] ? "auto" : "manual");
      http.end();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    else
    {
      http.end();
      Serial.printf("GET Error code: %d\n", httpResponseCode);
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