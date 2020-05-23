#ifndef _WebUI_h
#define _WebUI_h

#include <stdint.h>
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"

#include <Esp.h>
#include <Arduino.h>
#include "WiFi.h"
#include <cstdio>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <MqttClient.h>
#include "ArduinoJson.h"

#define LOG_SIZE_MAX 128
#define BUF_MAX 512
#define MICRODELAY 10

// #define LOG_PRINTFLN(fmt, ...)  logfln(fmt, ##__VA_ARGS__)

#define GPIO_MAX 4

#define HW_UART_SPEED                 115200L

#define WEBSERVER_TASK_CORE 1

#define WEBUI_USE_BUILDIN_STATUS 1
typedef int (*loadConfigCbk)(int eeAddress, int major, int minor);
typedef int (*saveConfigCbk)(int eeAddress);
typedef void (*PublishHandlerCbk)();
typedef void (*SubscribeHandlerCbk)();
typedef void (*TimerCbk)();

class WebUI : public WebServer {
	public:
	WebUI(int port = 80){
		wifiTryReconnect = false;
		wifiAPmode = false;
		inSetup = 1;
		otaRunning = 0;
		otaProgress = 0;
		otaTotal = 0;
		otaMessage = "Inactive.";
		firmwareMajor = 1;
		firmwareMinor = 0;
		batteryLevel = 0;
		batteryV = 0;
		mqtt = NULL;
		interruptPin = 0;
		interruptCounter = 0;
		numberOfInterrupts = 0;
		mqttBrokerPort = 1883;
		int pubflag = 0;
		timerCB = NULL;
		uint32_t isrCounter = 0;
		uint32_t lastIsrAt = 0;
		mqttRetry = 0;
		timerMux = portMUX_INITIALIZER_UNLOCKED;
		mux = portMUX_INITIALIZER_UNLOCKED;
		for( int i=0; i<10; i++ ) {
			menuItems[i] == NULL;
			menuUrls[i] = NULL;
		}
	};
	void setup();
	void loop();
	static String title;
	static void setTitle(String newTitle){
		title = newTitle;
	}
	/*
	static WebServer* getServer(){
		return &server;
	};
	static void on(const String &uri, WebServer::THandlerFunction handler){
		server.on(uri, handler);
	};
	static void on(const String &uri, HTTPMethod method, WebServer::THandlerFunction fn){
		server.on(uri, method, fn);
	};
	*/
	static void setTimerCB(TimerCbk callback){
		timerCB = callback;
	};
	static void setSaveConfigCB(saveConfigCbk cb){
		saveConfigCB = cb;
	}
	static void setLoadConfigCB(loadConfigCbk cb){
		loadConfigCB = cb;
	}
	static void saveConfig();
	static void setSubscribeCB(SubscribeHandlerCbk);
	static void setPublishCB(PublishHandlerCbk);
	static MqttClient::Error::type publish(const char* topic, MqttClient::Message& message) {
		return mqtt->publish(topic, message);
	};
	static MqttClient::Error::type subscribe(const char* topic, enum MqttClient::QoS qos, MqttClient::MessageHandlerCbk cbk) {
		return mqtt->subscribe(topic, qos, cbk );
	};

	const char *getMqttid() {
		Serial.printf("Get mqttid: %s\n",mqttid);
			return( mqttid);
	}

	int addMenuItem(char *title, char *url){
		for( int i=0; i<10; i++ ) {
			if( menuItems[i] == NULL) {
				menuItems[i] = new String(title);
				menuUrls[i] = new String(url);
				return i;
			}
		}
		return -1;
	}
	private:
	static WebServer *server;
	static DNSServer dnsServer;
	static MqttClient *mqtt;
	static WiFiClient network;
	static int mqttRetry;
	static uint64_t chipid;
	static char ssid[33];
	static char password[65];
	static bool wifiTryReconnect;
	static bool wifiAutoReconnect;
	static int wifiConnectTimeout;
	static bool wifiAPmode;
	static int inSetup;
	static int otaRunning;
	static int otaProgress;
	static int otaTotal;
	static String otaMessage;

	static IPAddress apIP;
	static IPAddress mqttBrokerIP;
	static int mqttBrokerPort;
	static char mqttid[32];
	static String *menuItems[10];
	static String *menuUrls[10];
#ifdef WEBUI_USE_BUILDIN_STATUS
	static char MQTT_TOPIC_STATUS[64]; 
#endif
	static char MQTT_TOPIC_CONFIG_REQ[];
	static char MQTT_TOPIC_CONFIG_RESP[];
	static int pubflag;
	static TimerCbk timerCB;
	static byte interruptPin;
	static volatile int interruptCounter;
	static int numberOfInterrupts;
	hw_timer_t * timer = NULL;
	static volatile SemaphoreHandle_t timerSemaphore;
	static portMUX_TYPE timerMux;
	static portMUX_TYPE mux;

	static volatile uint32_t isrCounter;
	static volatile uint32_t lastIsrAt;

	static int firmwareMajor;
	static int firmwareMinor;
	static float batteryLevel;
	static float batteryV;
	static loadConfigCbk loadConfigCB;
	static saveConfigCbk saveConfigCB;
	static PublishHandlerCbk publishCB;
	static SubscribeHandlerCbk subscribeCB;
	
	static void WiFiEvent(WiFiEvent_t event);
	void runWebserver( void * pvParameters );
	
	static bool isAuthentified();
	static void handleLogin();
	static void handleRoot();
	static boolean captivePortal();
	static void handleGetData();
	static void handleNotFound();
	static void handleWiFiSetupForm();
	static void handleSetWiFiParams();
	static void handleMqttForm ();
	static void handleConfigMqtt();
	static void otaActivateForm();
	static void handleOtaStart();
	static void handleGetOTAStatus();
	static void ESPrestart();
	static void handleFwUpload();
	static void startAP();
	static void connectWIFI( int maxRetries, int connectionTimeout, bool credentialsChanged );
	static void loadOldConfigFromEEPROM();
	static void loadConfigFromEEPROM();
	static void loadConfigFromEEPROMV10();
	static boolean isIp(String str);
	static String toStringIp(IPAddress ip);
	static void checkInterrupt();
	static void IRAM_ATTR handleInterrupt();
	static void IRAM_ATTR onTimer();
	static void publishData( void * pvParameters );
	void mqttSubscribe();
	void homieSubscribe();
	static void processConfigRequest(MqttClient::MessageData & md);
	static void sendConfigMessage();
	static void homieDiscoveryHandler(MqttClient::MessageData & md);
	static void homieSendDiscoveryResponse();

	static void logfln(const char *fmt, ...) {
	  char buf[LOG_SIZE_MAX];
	  va_list ap;
	  va_start(ap, fmt);
	  vsnprintf(buf, LOG_SIZE_MAX, fmt, ap);
	  va_end(ap);
	  Serial.println(buf);
	}

	static void fmtstring(char * buf, const char *fmt, ...) {
	  va_list ap;
	  va_start(ap, fmt);
	  vsnprintf(buf, BUF_MAX, fmt, ap);
	  va_end(ap);
	}

};

// ============== Object to supply system functions ============================
class System: public MqttClient::System {
  public:

    unsigned long millis() const {
      return ::millis();
    }

    void yield(void) {
      ::yield();
    }
};


#endif
