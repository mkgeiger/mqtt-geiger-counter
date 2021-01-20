/*=============================================================================
 =======                            INCLUDES                            =======
 =============================================================================*/
#include <user_config.h>
extern "C"
{
#include "hw_timer.h"
}
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <EEPROM.h>

/*=============================================================================
 =======               DEFINES & MACROS FOR GENERAL PURPOSE             =======
 =============================================================================*/
// eeprom
#define MQTT_IP_OFFSET         0
#define MQTT_IP_LENGTH        16
#define MQTT_USER_OFFSET      16
#define MQTT_USER_LENGTH      32
#define MQTT_PASSWORD_OFFSET  48
#define MQTT_PASSWORD_LENGTH  32

// access point
#define AP_NAME "GEIGER_MUELLER"
#define AP_TIMEOUT 300
#define MQTT_PORT 1883

// Geiger Mueller tube parameterization
// (see https://sites.google.com/site/diygeigercounter/technical/gm-tubes-supported for various differing conversion rates, using Method 2)
#define SBM20_FACTOR                ((double)0.0057)  // 1cpm ≙ 0.0057μSv/h
#define NBR_GMTUBES                 2	
#define TUBE_FACTOR                 SBM20_FACTOR
#define MEASURE_INTERVAL_MINUTES    10

/*=============================================================================
 =======                       CONSTANTS  &  TYPES                      =======
 =============================================================================*/

/*=============================================================================
 =======                VARIABLES & MESSAGES & RESSOURCEN               =======
 =============================================================================*/
// measurement
volatile uint32 measure_irq = 0ul;
volatile double cpm;
volatile double uSvph;
volatile uint32 hitCount = 0ul;
volatile boolean new_measurement = false;

// mqtt
IPAddress mqtt_server;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

char mqtt_ip_pre[MQTT_IP_LENGTH] = "";
char mqtt_user_pre[MQTT_USER_LENGTH] = "";
char mqtt_password_pre[MQTT_PASSWORD_LENGTH] = "";

char mqtt_ip[MQTT_IP_LENGTH] = "";
char mqtt_user[MQTT_USER_LENGTH] = "";
char mqtt_password[MQTT_PASSWORD_LENGTH] = "";

char topic_cpm[30] = "/";
char topic_uSvph[30] = "/";

boolean mqtt_connected = false;

// wifi
WiFiManager wifiManager;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;
char mac_str[13];

/*=============================================================================
 =======                              METHODS                           =======
 =============================================================================*/

ICACHE_RAM_ATTR void GMpulse(void)
{
	hitCount ++;
}

ICACHE_RAM_ATTR void measure_updater(void)
{
  measure_irq = (measure_irq + 1ul) % (MEASURE_INTERVAL_MINUTES * 60);  
  if (measure_irq == 0ul)
  {
    // calculate averaged cpm value for one tube
    cpm = (((double)hitCount) / ((double)MEASURE_INTERVAL_MINUTES)) / ((double)NBR_GMTUBES);
    hitCount = 0ul;
    // calculate uSvph value from cpm value
    uSvph = cpm * TUBE_FACTOR;

    new_measurement = true;
  }
}

String readEEPROM(int offset, int len)
{
  String res = "";
  for (int i = 0; i < len; ++i)
  {
    res += char(EEPROM.read(i + offset));
  }
  return res;
}

void writeEEPROM(int offset, int len, String value)
{
  for (int i = 0; i < len; ++i)
  {
    if (i < value.length())
    {
      EEPROM.write(i + offset, value[i]);
    }
    else
    {
      EEPROM.write(i + offset, 0x00);
    }
  }
}

void connectToWifi()
{
  Serial.println("Re-Connecting to Wi-Fi...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event)
{
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event)
{
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println("Connected to MQTT.");
  mqtt_connected = true;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  mqtt_connected = false;
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void setup(void)
{
  uint8_t mac[6];

	// init serial logging
	Serial.begin(115200);

  // init EEPROM
  EEPROM.begin(128);

  // switch (must be pullup input, D2 = GPIO4), check if pressed
  pinMode(D2, INPUT_PULLUP);
  if (LOW == digitalRead(D2))
  {
    Serial.println("reset wifi settings and restart.");
    wifiManager.resetSettings();
    delay(1000);
    ESP.restart();
  }

	// init WIFI
  readEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH).toCharArray(mqtt_ip_pre, MQTT_IP_LENGTH);
  readEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH).toCharArray(mqtt_user_pre, MQTT_USER_LENGTH);
  readEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH).toCharArray(mqtt_password_pre, MQTT_PASSWORD_LENGTH);

  WiFiManagerParameter custom_mqtt_ip("ip", "MQTT ip", mqtt_ip_pre, MQTT_IP_LENGTH);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user_pre, MQTT_USER_LENGTH);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT password", mqtt_password_pre, MQTT_PASSWORD_LENGTH, "type=\"password\"");

  wifiManager.addParameter(&custom_mqtt_ip);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalTimeout(AP_TIMEOUT);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,1,1), IPAddress(192,168,1,1), IPAddress(255,255,255,0));
  if (!wifiManager.autoConnect(AP_NAME))
  {
    Serial.println("failed to connect and restart.");
    delay(1000);
    // restart and try again
    ESP.restart();
  }

  strcpy(mqtt_ip, custom_mqtt_ip.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  if ((0 != strcmp(mqtt_ip, mqtt_ip_pre)) ||
      (0 != strcmp(mqtt_user, mqtt_user_pre)) ||
      (0 != strcmp(mqtt_password, mqtt_password_pre)))
  {
    Serial.println("Parameters changed, need to update EEPROM.");
    writeEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH, mqtt_ip);
    writeEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH, mqtt_user);
    writeEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH, mqtt_password);

    EEPROM.commit();
  }

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // construct MQTT topics with MAC
  WiFi.macAddress(mac);
  sprintf(mac_str, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("MAC: %s\n", mac_str);

  strcat(topic_cpm, mac_str);
  strcat(topic_cpm, "/cpm");
  strcat(topic_uSvph, mac_str);
  strcat(topic_uSvph, "/usvph");

  if (mqtt_server.fromString(mqtt_ip))
  {
    char mqtt_id[30] = AP_NAME;

    strcat(mqtt_id, "-");
    strcat(mqtt_id, mac_str);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.setServer(mqtt_server, MQTT_PORT);
    mqttClient.setCredentials(mqtt_user, mqtt_password);
    mqttClient.setClientId(mqtt_id);

    connectToMqtt();
  }
  else
  {
    Serial.println("invalid MQTT Broker IP.");
  }

  // GMpulse (interrupt, D7 = GPIO13)
  pinMode(D7, INPUT);
  attachInterrupt(digitalPinToInterrupt(D7), GMpulse, FALLING);

  // cyclic measurement funtion (every 1 second)
  hw_timer_init(FRC1_SOURCE, 1);
  hw_timer_set_func(measure_updater);
  hw_timer_arm(1000000);
}

void loop(void)
{  
  if ((mqtt_connected == true) && (new_measurement == true))
  {
	  char cpm_str[10];
    char uSvph_str[10];

	  new_measurement = false;

    Serial.printf("Publish: %.2f cpm    %.4f uSvph\n", cpm, uSvph);
    snprintf(cpm_str, 10, "%.2f", cpm);
    snprintf(uSvph_str, 10, "%.4f", uSvph);
    mqttClient.publish(topic_cpm, 0, true, cpm_str);
    mqttClient.publish(topic_uSvph, 0, true, uSvph_str);
  }
}
