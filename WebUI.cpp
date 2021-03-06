#include <WebUI.h>
#include <skeleton.h>
#include "soc/rtc.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_pm.h"

const byte DNS_PORT = 53;

const char *serverIndexFWUpload =
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";

const char *serverIndexFWUploadScript =
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
  "<input type='file' name='update'><br>"
  "<input type='submit' value='Update'><a class='button' href='/'>Back</a>"
  "</form>"
  "<div id='prg'>progress: 0%</div>"
  "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
  "},"
  "error: function (a, b, c) {"
  "}"
  "});"
  "});"
  "</script>";

//const char jquery[] = "<link rel=\"stylesheet\" href=\"http://code.jquery.com/mobile/1.4.5/jquery.mobile-1.4.5.min.css\">\n<script src=\"http://code.jquery.com/jquery-2.2.4.min.js\"></script>\n<script src=\"http://code.jquery.com/mobile/1.4.5/jquery.mobile-1.4.5.min.js\"></script>";
const char jquery[] = "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>";

const char *myHostname = "esp32";

WebServer *WebUI::server = NULL;
DNSServer WebUI::dnsServer;
MqttClient *WebUI::mqtt;
WiFiClient WebUI::network;

uint64_t WebUI::chipid;
String WebUI::title = "WebUI Framework";
char WebUI::ssid[33];
char WebUI::password[65];
int WebUI::mqttRetry = 0;
bool WebUI::wifiTryReconnect = true;
bool WebUI::wifiAutoReconnect = true;
int WebUI::wifiConnectTimeout = 20;
bool WebUI::wifiAPmode;
int WebUI::inSetup;
int WebUI::webserverRunning = false;
int WebUI::startWebserver = false;

int WebUI::otaRunning;
int WebUI::otaProgress;
int WebUI::otaTotal;
int WebUI::publishTimerPeriod;
int WebUI::publishingTimerActive;
int WebUI::backgroundPublishTask;
int WebUI::sleepmode;
esp_sleep_wakeup_cause_t WebUI::wakeup_reason;

WebUI *WebUI::instance;

String WebUI::otaMessage;
String *WebUI::menuItems[10];
String *WebUI::menuUrls[10];

IPAddress WebUI::apIP(192, 168, 4, 1);
IPAddress WebUI::mqttBrokerIP(192, 168, 1, 139);
int WebUI::mqttBrokerPort = 1883;
#ifdef WEBUI_USE_BUILDIN_STATUS
char WebUI::MQTT_TOPIC_STATUS[64]; 
#endif
char WebUI::MQTT_TOPIC_CONFIG_CMD[64]; 
char WebUI::MQTT_TOPIC_CONFIG_REQ[] = "actors/config/request";
char WebUI::MQTT_TOPIC_CONFIG_RESP[] = "actors/config/response";

char WebUI::mqttid[32] = "";
int WebUI::pubflag = 0;

byte WebUI::interruptPin;
volatile int WebUI::interruptCounter;
int WebUI::numberOfInterrupts;
volatile SemaphoreHandle_t WebUI::timerSemaphore;
portMUX_TYPE WebUI::timerMux;
portMUX_TYPE WebUI::mux;

volatile uint32_t WebUI::isrCounter;
volatile uint32_t WebUI::lastIsrAt;
TimerCbk WebUI::timerCB;
TaskHandle_t WebUI::publishTaskHandle;
TaskHandle_t WebUI::webserverTaskHandle;
loadConfigCbk WebUI::loadConfigCB;
saveConfigCbk WebUI::saveConfigCB;

int WebUI::firmwareMajor;
int WebUI::firmwareMinor;
float WebUI::batteryLevel;
float WebUI::batteryV;

void WebUI::WiFiEvent(WiFiEvent_t event)
{
  Serial.printf("[WiFi-event] event: %d\n", event);

  switch (event) {
    case SYSTEM_EVENT_WIFI_READY:
      Serial.println("[WiFi-event] WiFi interface ready");
      break;
    case SYSTEM_EVENT_SCAN_DONE:
      Serial.println("[WiFi-event] Completed scan for access points");
      break;
    case SYSTEM_EVENT_STA_START:
      Serial.println("[WiFi-event] WiFi client started");
      break;
    case SYSTEM_EVENT_STA_STOP:
      Serial.println("[WiFi-event] WiFi clients stopped");
      break;
    case SYSTEM_EVENT_STA_CONNECTED:
      Serial.println("[WiFi-event] Connected to access point");
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("[WiFi-event] Disconnected from WiFi access point");
      //connectWIFI(6, wifiConnectTimeout, false);
      break;
    case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
      Serial.println("[WiFi-event] Authentication mode of access point has changed");
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.print("[WiFi-event] Obtained IP address: ");
      Serial.println(WiFi.localIP());
	  Serial.flush();
      break;
    case SYSTEM_EVENT_STA_LOST_IP:
      Serial.println("[WiFi-event] Lost IP address and IP address is reset to 0");
      //connectWIFI(6, wifiConnectTimeout,false);
      break;
    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
      Serial.println("[WiFi-event] WiFi Protected Setup (WPS): succeeded in enrollee mode");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
      Serial.println("[WiFi-event] WiFi Protected Setup (WPS): failed in enrollee mode");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
      Serial.println("[WiFi-event] WiFi Protected Setup (WPS): timeout in enrollee mode");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
      Serial.println("[WiFi-event] WiFi Protected Setup (WPS): pin code in enrollee mode");
      break;
    case SYSTEM_EVENT_AP_START:
      Serial.println("[WiFi-event] WiFi access point started");
      break;
    case SYSTEM_EVENT_AP_STOP:
      Serial.println("[WiFi-event] WiFi access point  stopped");
      break;
    case SYSTEM_EVENT_AP_STACONNECTED:
      Serial.println("[WiFi-event] Client connected");
      break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      Serial.println("[WiFi-event] Client disconnected");
//      connectWIFI(6, 20,false);
      break;
    case SYSTEM_EVENT_AP_STAIPASSIGNED:
      Serial.println("[WiFi-event] Assigned IP address to client");
      break;
    case SYSTEM_EVENT_AP_PROBEREQRECVED:
      Serial.println("Received probe request");
      break;
    case SYSTEM_EVENT_GOT_IP6:
      Serial.println("[WiFi-event] IPv6 is preferred");
      break;
    case SYSTEM_EVENT_ETH_START:
      Serial.println("[WiFi-event] Ethernet started");
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("[WiFi-event] Ethernet stopped");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("[WiFi-event] Ethernet connected");
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("[WiFi-event] Ethernet disconnected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.println("[WiFi-event] Obtained IP address");
      break;
  }
}

void WebUI::runWebserver( void * pvParameters ) {
  String taskMessage = "Starting webserver on core ";
  webserverRunning = true;
  taskMessage = taskMessage + xPortGetCoreID();
  Serial.println(taskMessage);

//  server->on("/", handleRoot );
  on("/", handleRoot );
  on("/wifi", handleWiFiSetupForm);
  on("/setParams", handleSetWiFiParams);
  on("/getData", handleGetData);
  on("/configMqttForm", handleMqttForm);
  on("/configMqtt", handleConfigMqtt);
  on("/otaActivateForm", otaActivateForm);
  on("/otaStart", handleOtaStart);
  on("/getOTAStatus", handleGetOTAStatus);
  on("/fwupload", HTTP_GET, handleFwUpload);
  on("/espreset", ESPrestart);
  on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  onNotFound(handleNotFound);
  /*handling uploading firmware file */
  WebServer::on("/update", HTTP_POST, [&]() {
	  //  server->sendHeader("Connection", "close");
    sendHeader("Connection", "close");
    send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    Serial.printf("Update return status: %s\n" , (Update.hasError()) ? "FAIL" : "OK");
    inSetup = false;
    ESP.restart();
  }, [&]() {
    inSetup = true;
    HTTPUpload& upload = this->upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Upload: %s\nSize: %d\n", upload.filename.c_str(), upload.currentSize);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Serial.printf("Update begin error...\n");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // flashing firmware to ESP
      Serial.printf("Upload file write: %d/%d\n", upload.currentSize, upload.totalSize);
      int currentSize = Update.write(upload.buf, upload.currentSize);
      if (currentSize != upload.currentSize) {
        Serial.printf("Upload size error (%d/%d)\n", currentSize, upload.totalSize );
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      Serial.printf("Upload file write: %d/%d\n", upload.currentSize, upload.totalSize);
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Upload Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Serial.printf("Update other error...");
        Update.printError(Serial);
      }
    } else {
      Serial.printf("Update unexpected status %d.", upload.status);
    }
  });
  

  //here the list of headers to be recorded
  //  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  //  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  //  collectHeaders(headerkeys, headerkeyssize );
  begin();
  Serial.println("HTTP server started");

#if WEBSERVER_TASK_CORE == 0
  while (!sleepmode){
    TIMG_WDT_WKEY_VALUE;
    TIMERG1.wdt_feed = 1;
    TIMERG1.wdt_wprotect = 0;
//    if( !otaRunning )
    	server->handleClient();
    //delay(100);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  } 
#endif
}

void WebUI::setup(int newBackgroundPublishTask) {
  backgroundPublishTask = newBackgroundPublishTask;
//  Serial.begin(HW_UART_SPEED);
  Serial.println("WebUI setup.");
  inSetup = false;
  print_wakeup_reason();
  if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
	  Serial.println("Button pressed to discontinue sleep mode");
	  sleepmode = 0;
	  inSetup = true;
	  startWebserver = true;
  }
  chipid = ESP.getEfuseMac();
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, FALLING);
  sprintf(mqttid, "%X", chipid );

  loadConfigFromEEPROM();
#ifdef WEBUI_USE_BUILDIN_STATUS
  sprintf(MQTT_TOPIC_STATUS, "actors/%s-0/status", mqttid);
#endif
  sprintf(MQTT_TOPIC_CONFIG_CMD, "node/%s-0/config", mqttid);
  if ( strlen(ssid) == 0 ) {
    Serial.println("No WLAN settings found. Start AP mode");
	startWebserver = true;
    startAP();
  } else {
    Serial.println("Found WLAN settings. Try connecting to " + (String) ssid + " with pw " + password + "." );
	WiFi.mode(WIFI_STA );
	esp_wifi_set_ps(WIFI_PS_NONE);
	WiFi.onEvent(WiFiEvent);
	WiFi.setAutoReconnect(true);
	wl_status_t wifistatus = WiFi.begin ( ssid, password );
    // connectWIFI( 6, wifiConnectTimeout, false );
  }

	ArduinoOTA
	.onStart([&]() {
		String type;
		otaProgress = 0;
		otaTotal = 0;
		Serial.println("onStart OTA.");
		otaMessage = "Starting upload";
		inSetup = true;
		timerAlarmDisable(timer);
		if (ArduinoOTA.getCommand() == U_FLASH)
		  type = "sketch";
		else // U_SPIFFS
		  type = "filesystem";

		// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
		otaMessage = "Upload of " + type + " started.";
//	    server->handleClient();
		Serial.println("Start updating " + type);
	  })
  .onEnd([&]() {
    inSetup = false;
    ArduinoOTA.end();
    otaRunning = false;
    timerAlarmEnable(timer);
    otaMessage = "Upload completed.";
//    server->handleClient();
    Serial.println("\nonEnd");
  })
  .onProgress([&](unsigned int progress, unsigned int total) {
    otaProgress = progress;
    otaTotal = total;
    char buf[30];
    sprintf(buf,"Progress: %d %%", progress / (total / 100));
    otaMessage = (String) buf;
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
//    server->handleClient();
  })
  .onError([&](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    otaRunning = false;
    ArduinoOTA.end();
    timerAlarmEnable(timer);
    inSetup = false;
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      otaMessage = "Auth Failed";
    }
    else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      otaMessage = "Begin Failed";
    }
    else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      otaMessage = "Connect Failed";
    }
    else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      otaMessage = "Receive Failed.";
    }
    else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      otaMessage = "End Failed.";
    } else {
      Serial.print("Unknown error" );
      otaMessage = "Unknown error: " + (String) error;
    }
//    server->handleClient();
  });

    // Create semaphore to inform us when the timer has fired
    Serial.println("Setup timer...");
    timerSemaphore = xSemaphoreCreateBinary();

    // Use 1st timer of 4 (counted from zero).
    // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
    // info).
    timer = timerBegin(0, 80, true);

    // Attach onTimer function to our timer.
    timerAttachInterrupt(timer, &onTimer, true);

    // Set alarm to call onTimer function every second (value in microseconds).
    // Repeat the alarm (third parameter)
    timerAlarmWrite(timer, 1000000, true);

    // Start an alarm
    Serial.println("Enable timer...");
    timerAlarmEnable(timer);
    server = this;
  if (!wifiAPmode) {
   // Setup MqttClient
	initMQTT();
  } else {
	  if(startWebserver && !webserverRunning)
		runWebserver( NULL );
  }
#ifdef WEBUI_USE_BUILDIN_STATUS
if( backgroundPublishTask&&!wifiAPmode) {
	startBackgroundPublishTask();
} else {
	publishTaskHandle = NULL;
	Serial.println("Data publishing task not activated (set WEBUI_USE_BUILDIN_STATUS to activate)");
}
#endif
}

void WebUI::initMQTT() {
    Serial.println("Setup MqttClient");
    MqttClient::System *mqttSystem = new System;
    MqttClient::Logger *mqttLogger = new MqttClient::LoggerImpl<HardwareSerial>(Serial);
    MqttClient::Network * mqttNetwork = new MqttClient::NetworkClientImpl<WiFiClient>(network, *mqttSystem);
    //// Make 128 bytes send buffer
    MqttClient::Buffer *mqttSendBuffer = new MqttClient::ArrayBuffer<512>();
    //// Make 128 bytes receive buffer
    MqttClient::Buffer *mqttRecvBuffer = new MqttClient::ArrayBuffer<512>();
    //// Allow up to 5 subscriptions simultaneously
    MqttClient::MessageHandlers *mqttMessageHandlers = new MqttClient::MessageHandlersImpl<5>();
    //// Configure client options
    MqttClient::Options mqttOptions;
    ////// Set command timeout to 10 seconds
    mqttOptions.commandTimeoutMs = 10000;
    //// Make client object
    mqtt = new MqttClient(
      mqttOptions, *mqttLogger, *mqttSystem, *mqttNetwork, *mqttSendBuffer,
      *mqttRecvBuffer, *mqttMessageHandlers
    );
    Serial.println("MqttClient Setup completed.");
}

void WebUI::connectMQTT() {
	if( !mqtt )
		initMQTT();
		
    if (mqtt != NULL )
      if (!mqtt->isConnected() && (mqttRetry == 0 || mqttRetry < lastIsrAt - 10000)) {
        //Serial.println("mqtt not connected.");
        // Close connection if exists
        network.stop();
        // Re-establish TCP connection with MQTT broker
        if (!network.connect(mqttBrokerIP, mqttBrokerPort)) {
         Serial.printf("Can't establish the TCP connection to %d.%d.%d.%d on port %d\r\n", mqttBrokerIP[0], mqttBrokerIP[1], mqttBrokerIP[2], mqttBrokerIP[3], mqttBrokerPort );
		 printWiFiStatus(WiFi.status());
         Serial.println("Can't establish the TCP connection. Trying to reconnect.");
          mqttRetry = lastIsrAt;
		  return;
        } else {
          Serial.println("Connection established.");
          // Start new MQTT connection
          Serial.println("Connection established. Connect to MQTT broker...");
          MqttClient::ConnectResult connectResult;
          // Connect
          {
            MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
//            Serial.println("Set MQTT connection options...");
            options.MQTTVersion = 4;
            options.clientID.cstring = mqttid;
            options.cleansession = true;
            options.keepAliveInterval = 15; // 15 seconds
            char lastwill[64];
            sprintf(lastwill, "/homie/%s/$state", mqttid);
            options.willFlag = '1';
            options.will.topicName.cstring = lastwill;
            options.will.message.cstring = "lost";
            Serial.println("Establish MQTT connection...");
            MqttClient::Error::type rc = mqtt->connect(options, connectResult);
            if (rc != MqttClient::Error::SUCCESS) {
			  mqttRetry = lastIsrAt;
              logfln("MQTT Connection error: %i", rc);
              return;
            } else {
              Serial.println("MQTT Connection established.");
              mqttRetry = 0;
              sendConfigMessage();
            }
          }
          {
            // Add subscribe here if required
            // Subscribe
        	mqttSubscribe();
         	mqttConfigSubscribe();
			homieSubscribe();
			
			if(subscribeCB) {
				Serial.println("Now processing subscribe callback.");
				subscribeCB();
			} else {
				Serial.println("No subscribe callback registered.");
			}

          } // end of subscribe block
		}
      } else {
        {
#ifndef WEBUI_USE_BUILDIN_STATUS
			if(!backgroundPublishTask && publishingTimerActive) {
				publishData((void *)NULL);
			}
          // Add publish here if required
          // Publish
#endif
        }
      }
	
}

void WebUI::startBackgroundPublishTask() {
	xTaskCreatePinnedToCore(
    publishData,   /* Function to implement the task */
    "publishData", /* Name of the task */
    10000,      /* Stack size in words */
    NULL,       /* Task input parameter */
    0,          /* Priority of the task */
    &publishTaskHandle,       /* Task handle. */
    0);  /* Core where the task should run */
	Serial.println("Started data publishing task on core 0.");
}

void WebUI::stopBackgroundPublishTask() {
	if( publishTaskHandle != NULL ) {
		vTaskDelete(publishTaskHandle);
	Serial.println("Stoped data publishing task on core 0.");
	}
	
}
void WebUI::loop() {

  if ( !inSetup && !wifiAPmode && WiFi.status() == WL_CONNECTED) {
    // logfln("Check mqtt connection.");
	connectMQTT();
	if(mqtt) {
#ifndef WEBUI_USE_BUILDIN_STATUS
		if(!backgroundPublishTask && publishingTimerActive) {
			publishData((void *)NULL);
		}
#endif
		mqtt->yield(1L);
	}

  }

  if (wifiAPmode)
    dnsServer.processNextRequest();	
  checkInterrupt();
  if( startWebserver && !webserverRunning && (WiFi.status() == WL_CONNECTED || wifiAPmode)){
#if WEBSERVER_TASK_CORE == 0
    xTaskCreatePinnedToCore(
      runWebserver,   /* Function to implement the task */
      "Webserver", /* Name of the task */
      10000,      /* Stack size in words */
      NULL,       /* Task input parameter */
      0,          /* Priority of the task */
      &webserverTaskHandle,       /* Task handle. */
      0);  /* Core where the task should run */
#else
	Serial.println("WebUI::loop runWebserver()...");
    runWebserver( NULL );
#endif
 	  
  }
#if WEBSERVER_TASK_CORE == 1
  if(webserverRunning) {
//	Serial.println("server->handleClient()...");
  	server->handleClient();
  }
#endif
  if( otaRunning )
  	ArduinoOTA.handle();
}

bool WebUI::isAuthentified() {
  Serial.println("Enter is_authentified");
  if (server->hasHeader("Cookie")) {
    Serial.print("Found cookie: ");
    String cookie = server->header("Cookie");
    Serial.println(cookie);
    if (cookie.indexOf("ESPSESSIONID=1") != -1) {
      Serial.println("Authentification Successful");
      return true;
    }
  }
  Serial.println("Authentification Failed");
  return false;
}

//login page, also called for disconnect
void WebUI::handleLogin() {
  String msg;
  if (server->hasHeader("Cookie")) {
    Serial.print("Found cookie: ");
    String cookie = server->header("Cookie");
    Serial.println(cookie);
  }
  if (server->hasArg("DISCONNECT")) {
    Serial.println("Disconnection");
    server->sendHeader("Location", "/login");
    server->sendHeader("Cache-Control", "no-cache");
    server->sendHeader("Set-Cookie", "ESPSESSIONID=0");
    server->send(301);
    return;
  }
  if (server->hasArg("USERNAME") && server->hasArg("PASSWORD")) {
    if (server->arg("USERNAME") == "admin" &&  server->arg("PASSWORD") == "admin" ) {
      server->sendHeader("Location", "/");
      server->sendHeader("Cache-Control", "no-cache");
      server->sendHeader("Set-Cookie", "ESPSESSIONID=1");
      server->send(301);
      Serial.println("Log in Successful");
      return;
    }
    msg = "Wrong username/password! try again.";
    Serial.println("Log in Failed");
  }
  String content = "<html><body><form action='/login' method='POST'>To log in, please use : admin/admin<br>";
  content += "User:<input type='text' name='USERNAME' placeholder='user name'><br>";
  content += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form>" + msg + "<br>";
  content += "You also can go <a href='/inline'>here</a></body></html>";
  server->send(200, "text/html", content);
}

/** Handle root or redirect to captive portal */
void WebUI::handleRoot() {
  Serial.println("handleRoot" + String(myHostname));
  String out = "";
  if (WebUI::captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->sendHeader("Connection", "close");
  out += "<!doctype html><html><head><title>" + title + "</title>\n";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
//  out += jquery;
  out += skeleton;
  out += "<script>";
  out += "$(function() {setInterval(\"updateData()\", 1000) });";
  out += "function autoswitchChanged() {";
  out +=   "$.ajax({";
  out +=       "type:'Post',";
  out +=       "dataType: 'text',";
  out +=       "url:'/setAutoMode',";
  out +=       "data: { \"mode\": $('#autoswitch').val()},";
  out +=       "success:function(data) {\n";
  out +=       "   console.log(\"Auto Mode changed successfully: \" + data);\n";
  out +=       "},";
  out +=       "error:function(xhr, status, error) {\n";
  out +=       "   console.log(\"Error changing auto mode: \" + error);\n";
  out +=       "   console.log(\"status: \" + status);\n";
  out +=       "   console.log(\"xhr: \" + xhr);\n";
  out +=       "}\n";
  out +=   "})\n";
  out += "}\n\n";
  out += "function pumpswitchChanged() {";
  out +=   "$.ajax({";
  out +=       "type:'Post',";
  out +=       "dataType: 'text',";
  out +=       "url:'/switchPump',";
  out +=       "data: { \"mode\": $('#pumpswitch').val()},";
  out +=       "success:function(data) {\n";
  out +=       "   console.log(\"Auto Mode changed successfully: \" + data);\n";
  out +=       "},";
  out +=       "error:function(xhr, status, error) {\n";
  out +=       "   console.log(\"Error changing auto mode: \" + error);\n";
  out +=       "   console.log(\"status: \" + status);\n";
  out +=       "   console.log(\"xhr: \" + xhr);\n";
  out +=       "}\n";
  out +=   "})\n";
  out += "}\n\n";
  out += "function updateData() {";
  out +=   "$.ajax({";
  out +=       "type:'Get',";
  out +=       "dataType: 'json',";
  out +=       "url:'/getData',";
  out +=       "success:function(data) {\n";
  out +=       "   days = Math.floor(data.isrcounter / 86400);";
  out +=       "   data.isrcounter %= 86400;";
  out +=       "   hours = Math.floor(data.isrcounter / 3600);";
  out +=       "   data.isrcounter %= 3600;";
  out +=       "   minutes = Math.floor(data.isrcounter / 60);";
  out +=       "   seconds = data.isrcounter % 60;";
  out +=       "   $('#uptime').html(days+'d '+hours+':'+minutes+':'+seconds);\n";
  out +=       "   $('#isrcounter').html(data.isrcounter );\n";
  out +=       "   $('#lastisrat').html(data.lastisrat);\n";
  out +=       "   $('#network').html(data.network ?\"Yes\":\"No\");\n";
  out +=       "   $('#signal').html(data.signal);\n";
  out +=       "   $('#wifipower').html(data.wifipower);\n";
  out +=       "   $('#battery').html(data.battery);\n";
  out +=       "   $('#voltage').html(data.voltage);\n";
  out +=       "   $('#insetup').html(data.insetup ? \"Yes\":\"No\");\n";
  out +=       "   $('#wifiapmode').html(data.wifiapmode ? \"On\":\"Off\");\n";
  out +=       "},";
  out +=       "error:function(xhr, status, error) {\n";
  out +=       "   console.log(\"getData error: \" + error);\n";
  out +=       "}";
  out +=   "})";
  out += "}";
  out += "</script>";
  out += "</head><body>";
  out += "<h1>"+title+"</h1>";
  out += "<table><tr><td>chipid:</td><td>" + String(mqttid) + "</td></tr>";
  out += "<tr><td>Firmware:</td><td>" + String(firmwareMajor) + "." + String(firmwareMinor) + "</td></tr>";
  if (server->client().localIP() == apIP) {
    out += "<tr><td>Soft AP:</td><td>" + String(ssid) + "</td></tr>";
  } else {
    out += "<tr><td>wifi connection:</td><td>" + String(ssid) + "</td></tr>";
  }
  uint64_t time = isrCounter;
  uint32_t d = time/86400;
  time = time%86400;
  uint32_t h = time/3600;
  time = time%3600;
  uint32_t min = time/60;
  time = time%60;
  uint32_t sec = time;

  out += "<tr><td>Uptime:</td><td id='uptime'>" + (String) d;
  out += "d " +(String) h;
  out += ":" + (String) min;
  out += ":" + (String) sec;
  out += "</td></tr>";
  out += "<tr><td>isrCounter:</td><td id='isrcounter'>" + (String)isrCounter + "</td></tr>";
  out += "<tr><td>lastIsrAt:</td><td id='lastisrat'>" + (String) lastIsrAt + "</td></tr>";
  out += "<tr><td>Setup mode:</td><td id='insetup'>" + (String) (inSetup ? "Yes" : "No") + "</td></tr>";
  out += "<tr><td>AP mode:</td><td id='wifiapmode'>" + (String) (wifiAPmode ? "On" : "Off") + "</td></tr>";
  out += "<tr><td>Network:</td><td id='network'>" + (String) (network.connected() ? "Yes" : "No") + "</td></tr>";
  out += "<tr><td>Auto reconnect:</td><td id='wifiautoconnect'>" + (String) (wifiAutoReconnect ? "Yes" : "No") + "</td></tr>";
  out += "<tr><td>Signal:</td><td id='signal'>" + (String) WiFi.RSSI() + "</td></tr>";
  out += "<tr><td>WiFi power:</td><td id='wifipower'>" + (String) WiFi.getTxPower() + "</td></tr>";
  out += "<tr><td>Battery:</td><td id='battery'>" + (String) batteryLevel + "</td></tr>";
  out += "<tr><td>Voltage:</td><td id='voltage'>" + (String) batteryV + "</td></tr>";
  out += "</table > ";
  for( int i = 0; i < 10 && menuItems[i]!=0L; i++) {
	  out += "<a class='button' href='";
	  out += *menuUrls[i];
	  out += "'>";
	  out += *menuItems[i];
	  out += "</a>";
  }
  out += "<a class='button' href='/wifi'>WiFi setup</a><a class='button' href='/configMqttForm'>MQTT setup</a><a class='button' href='/otaActivateForm'>update firmware</a><a class='button' href='/fwupload'>upload firmware</a><a class='button' href='/espreset'>Reset the thing</a></body></html>";
  server->send(200, "text/html", out);
}

boolean WebUI::captivePortal() {
  if (!isIp(server->hostHeader()) && server->hostHeader() != (String(myHostname) + ".local")) {
    String out = "";
    Serial.print("Request redirected to captive portal");
    server->sendHeader("Location", String("http://") + toStringIp(server->client().localIP()), true);
    server->send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    //    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
//  Serial.print("No captive portal");
  return false;
}

void WebUI::handleGetData() {
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  batteryLevel = map(analogRead(33), 0.0f, 4095.0f, 0, 100);
  batteryV = 3.3/4096*(float) analogRead(33); // map((float) analogRead(33), 0.0f, 4095.0f,0, 3.7f);
  server->send(200, "text/json",
              "{\"insetup\":" + (String) inSetup +
              ",\"wifiapmode\":" + (String) wifiAPmode +
              ",\"network\":" + network.connected() +
              ",\"signal\":" + WiFi.RSSI() +
              ",\"wifipower\":" + WiFi.getTxPower() +
              ",\"battery\":"+ batteryLevel +
              ",\"voltage\":"+ batteryV +
              ",\"isrcounter\":" + (String) isrCounter +
              ",\"lastisrat\":" + (String) lastIsrAt + "}");
}

void WebUI::handleNotFound() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the error page.
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache - Control", "no - cache, no - store, must - revalidate");
  server->sendHeader("Pragma", "no - cache");
  server->sendHeader("Expires", " - 1");
  server->send ( 404, "text / plain", message );
}

void WebUI::handleWiFiSetupForm() {
	//  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	//  server->sendHeader("Pragma", "no-cache");
	//  server->sendHeader("Connection", "close");
  String out = "<!doctype html><html><head><title>WIFI Setup</title>";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  out += skeleton;
  out += "</head><body><form action = '/setParams' method = 'POST'>Please select SSID and provide WIFI password<br>";
  int n = WiFi.scanNetworks();
  Serial.println("scan done");

  if (n == 0) {
    Serial.println("no networks found");
    out += "no networks found<br>";
  } else {
    out += "<label class = \"select\" for=\"USERID\">SSID:</label><select name='USERID' id='USERID' placeholder='SSID' data-inline='true' data-mini='true' >";
    out += n + " networks found:";
    Serial.println(" networks found");

    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      out += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " " + WiFi.RSSI(i) + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*") + "</option>";
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
      //            delay(10);
    }
  }

  out += "</select><br>";
  out += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
  out += "Automatic reconnect: <input type='checkbox' name='autoreconnect'";
  out += (wifiAutoReconnect?" checked":"");
  out += "><br>";
  out += "<input type='submit' name='SUBMIT' value='Submit'><a class='button' href='/'>Go back</a></form></body></html>";

  server->send(200, "text/html", out);
}

void WebUI::handleSetWiFiParams() {
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  String out = "<html><head><title>WIFI Setup</title>";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  out += skeleton;
  out += "</head><body><form action='/' method='POST'>Setting parameters and switching to WIFI client.<br>";
  wifiAutoReconnect = false;
  bool credentialsChanged = false;
  for (uint8_t i = 0; i < server->args(); i++) {
//	  Serial.print(server->argName(i));
//	  Serial.print(": ");
//	  Serial.println(server->arg(i).c_str());
    if ( server->argName(i) == "USERID" ) {
    	if(strcmp(server->arg(i).c_str(),ssid)!=0)
    		credentialsChanged = true;
      strcpy( ssid, server->arg(i).c_str() );
    }
    if (server->argName(i) == "PASSWORD" ) {
    	if(strcmp(server->arg(i).c_str(),password)!=0)
    		credentialsChanged = true;
      strcpy( password, server->arg(i).c_str() );
    }
    if (server->argName(i) == "autoreconnect" ) {
//    	Serial.printf("autoreconnect: %s\n", server->arg(i).c_str());
    	wifiAutoReconnect = (strcmp(server->arg(i).c_str(),"on")==0);
    }
  }
  if(credentialsChanged && strlen(password)) {
	  out += "connect to ";
	  out += (String) ssid + " with password ";
	  saveConfig();
	  Serial.print( "Attempting to connect to ");
	  Serial.print( ssid );
	  Serial.print( " with pw " );
	  Serial.println( password );
	  Serial.println( wifiAutoReconnect );
  }

  out += (String) password + "<br><input type='submit' name='SUBMIT' value='Back'></form></body></html>";
  server->send(200, "text/html", out);

  if(credentialsChanged && strlen(password)) {
	  Serial.print("WiFi.disconnect(): ");
	  Serial.println(WiFi.disconnect(true));
	  wifiAPmode = false;
	  vTaskDelay(200 / portTICK_RATE_MS);
	  delay(2000L);
	  Serial.println(WiFi.status());
	  WiFi.begin ( ssid, password );
	  //connectWIFI(6, wifiConnectTimeout, credentialsChanged);
	  if(!wifiAPmode)
		ESP.restart();
  }
}

void WebUI::handleMqttForm () {
	  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	  server->sendHeader("Pragma", "no-cache");
	  server->sendHeader("Connection", "close");
	  String out = "<!doctype html><html><head><title>MQTT Setup</title>";
	  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
	  out += skeleton;
	  out += "</head><body><form action='/configMqtt' method='POST'>Please enter MQTT boker IP address and port<br>";
	  out += "MQTT ID / host name:<input type='text' name='mqttid' value='" + (String) mqttid + "'><br>";
	  out += "Broker address:<input type='text' name='brokerip' value='" + (String) mqttBrokerIP[0] + "." + (String) mqttBrokerIP[1] + "." + (String) mqttBrokerIP[2] + "." + (String) mqttBrokerIP[3] + "'><br>";
	  out += "Broker port:<input type='text' name='brokerport' value='" + (String) mqttBrokerPort + "'><br>";
	  out += "<input type='submit' name='SUBMIT' value='Submit'><a class='button' href='/'>Go back</a></form></body></html>";
	  server->send(200, "text/html", out);
}

void WebUI::handleConfigMqtt() {
	  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	  server->sendHeader("Pragma", "no-cache");
	  server->sendHeader("Connection", "close");
	  String out = "<!doctype html><html><head><title>MQTT Setup</title>";
	  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
	  out += skeleton;
	  out += "</head><body>";
	  for (uint8_t i = 0; i < server->args(); i++) {
		if ( server->argName(i) == "brokerip" ) {
		  mqttBrokerIP.fromString(server->arg(i).c_str());
		  // strcpy( ssid, server->arg(i).c_str() );
		}
		if (server->argName(i) == "brokerport" ) {
		  mqttBrokerPort = atoi( server->arg(i).c_str() );
		}
		if (server->argName(i) == "mqttid" ) {
		  strcpy( mqttid, server->arg(i).c_str() );
		}
	  }
	  out += "Setting MQTT broker to ";
	  out += (String) "MQTT ID: " + (String) mqttid + "<br>";
	  out += (String) mqttBrokerIP[0] + "." + (String) mqttBrokerIP[1] + "." + (String) mqttBrokerIP[2] + "." + (String) mqttBrokerIP[3] + " with port ";
	  out += (String) mqttBrokerPort + "<br>";
	  out += "<a class=\"ui-button ui-widget ui-corner-all\" href='/'>Back to main menu</a></body></html>";
	  server->send(200, "text/html", out);
	  saveConfig();
	  inSetup = 0;
#ifdef WEBUI_USE_BUILDIN_STATUS
  sprintf(MQTT_TOPIC_STATUS, "actors/%s-0/status", mqttid);
#endif
  sprintf(MQTT_TOPIC_CONFIG_CMD, "node/%s-0/config", mqttid);
	  if(mqtt != NULL)
		  mqtt->disconnect();
}

void WebUI::otaActivateForm() {
  otaProgress = 0;
  otaTotal = 0;
  otaMessage = "Waiting...";
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  String out = "<!doctype html><html><head><title>OTA</title>";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  out += skeleton;
  out += "</head><body><form action='/otaStart' method='POST'>Please press Button to activate OTA<br>";
  out += "<input type='text' name='brokerip' value='" + ArduinoOTA.getHostname() + "'><br>";
  out += "<input type='submit' name='SUBMIT' value='Start'><a class='button' href='/'>Go back</a></form></body></html>";
  server->send(200, "text/html", out);
}

void WebUI::handleOtaStart() {
  Serial.println("Start OTA");
  otaRunning = true;
  ArduinoOTA.begin();
//  otaMessage = "waiting.";
  Serial.println("Ready");
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  inSetup = true;
  String out = "<!doctype html><html><head><title>OTA</title>";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  out += skeleton;
  out += "<script>";
  out += " $(function() {setInterval(\"updateStatus()\", 3000) });\n";
  out += "function updateStatus() {\n";
  out +=   "$.ajax({";
  out +=       "type:'Get',";
  out +=       "dataType: 'json',";
  out +=       "url:'/getOTAStatus',";
  out +=       "success:function(data) {\n";
  out +=       "   $('#otastatus').html(data.status );\n";
  out +=       "   console.log(\"getData status: \" + data.status);\n";
  out +=       "},";
  out +=       "error:function(xhr, status, error) {\n";
  out +=       "   //$('#otastatus').html(\"error \" + error );\n";
  out +=       "   console.log(\"getData error: \" + error);\n";
  out +=       "}";
  out +=   "});\n";
  out += "}\n";
  out += "</script>\n";
  out += "</head><body><form action='/' method='POST'>Hostname: " + ArduinoOTA.getHostname() + "<br>Now start uploading the firmware in Arduino IDE<br>";
  out += "Status: <div id='otastatus'>" + otaMessage + "</div><br>";
  out += "<input type='submit' name='SUBMIT' value='Click when finished'></form><br></body></html>";
  server->send(200, "text/html", out);
}

void WebUI::handleGetOTAStatus() {
  Serial.println("handleGetOTAStatus");
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  server->send(200, "text/json",
              "{\"status\":\"" + (String) otaMessage + "\"}");
}

void WebUI::ESPrestart() {
  ESP.restart();
}

void WebUI::handleFwUpload() {
    server->sendHeader("Connection", "close");
    String out = serverIndexFWUpload;
    out += skeleton;
    out += serverIndexFWUploadScript;
    server->send(200, "text/html", out);
  };
  
void WebUI::startAP() {
  wifiAPmode = true;
  //backgroundPublishTask = false;
  WiFi.disconnect();
  Serial.println();
  Serial.println("Starting Access Point ESP32");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32", "");
//  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  IPAddress IP = WiFi.softAPIP ();
  Serial.println("");
  Serial.println("WiFi AP ready.");
  Serial.println("IP address: ");
  Serial.println(IP);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  if(!webserverRunning) {
	  Serial.println("Starting web server...");
	   WebUI::startWebserver = true;
  }
	  
}
void WebUI::printWiFiStatus( wl_status_t wifistatus ) {
	switch( wifistatus ) {
		case WL_IDLE_STATUS:
			Serial.println("WL_IDLE_STATUS");
			break;
		case WL_NO_SSID_AVAIL:
			Serial.println("WL_NO_SSID_AVAIL");
			break;
		case WL_SCAN_COMPLETED:
			Serial.println("WL_SCAN_COMPLETED");
			break;
		case WL_CONNECTED:
			Serial.println("WL_CONNECTED");
			break;
		case WL_CONNECT_FAILED:
			Serial.println("WL_CONNECT_FAILED");
			break;
		case WL_CONNECTION_LOST:
			Serial.println("WL_CONNECTION_LOST");
			break;
		case WL_DISCONNECTED:
			Serial.println("WL_DISCONNECTED");
			break;
		case WL_NO_SHIELD:
			Serial.println("WL_NO_SHIELD");
			break;
		default:
			Serial.println("unknown WiFi return code.");
			break;
	}
}

/*
void WebUI::connectWIFI( int maxRetries, int connectionTimeout, bool credentialsChanged ) {
	  if ( wifiAPmode ) return;
	  Serial.println("connectWIFI");
//	  WiFi.setSleepMode(WIFI_PS_NONE);
	WiFi.mode(WIFI_STA );
	wl_status_t wifistatus = WiFi.status();
//	  if(WiFi.status() == WL_CONNECTED)
//		  WiFi.disconnect();
	  WiFi.onEvent(WiFiEvent);
	  int retries = 0;
	  while( (retries < maxRetries) && wifistatus != WL_CONNECTED && !wifiAPmode ) {
		Serial.print ( "Connecting to " );
		Serial.println ( ssid );
		Serial.print ( "PW: " );
		Serial.println ( password );
		Serial.println("WiFi.begin()");
		//    Serial.println("WiFi.disconnect()");
		//    WiFi.disconnect(true);
		esp_wifi_set_ps(WIFI_PS_NONE);
		wl_status_t wifistatus = WiFi.begin ( ssid, password );
			delay ( 1000 );

		  // Wait for connection
		for( int i = 0; wifistatus != WL_CONNECTED && wifistatus != WL_NO_SHIELD && i < connectionTimeout && !wifiAPmode; i++ ) {
			wifistatus = WiFi.status();
			if( wifistatus != WL_DISCONNECTED && wifistatus != WL_CONNECTED )
				printWiFiStatus( wifistatus );
			checkInterrupt();
			//vTaskDelay(1000 / portTICK_RATE_MS);
			delay ( 1000 );
			Serial.print ( "." );
		  }

		if(!wifiAutoReconnect)
			retries++;
		} 

	  if ( WiFi.status() == WL_CONNECTED ) {
		Serial.println( "" );
		Serial.printf( "Connected to %s with IP address ", ssid);
		Serial.println( WiFi.localIP() );
		if( credentialsChanged )
			saveConfig();
		if(wakeup_reason != ESP_SLEEP_WAKEUP_EXT0)
			inSetup = 0;
	  } else {
		Serial.printf( "Connection to %s timed out.\n", ssid );
		Serial.println( " timed out." );
		Serial.println("Falling back to Access Point Mode." );
		startAP();
	  }
	  Serial.println("connectWIFI done.");
}
*/
/** Load config like WLAN credentials from EEPROM */

void WebUI::loadOldConfigFromEEPROM() {
  uint32_t tmpip = 0;
  int eeAddress = 0;
  EEPROM.begin(512);
  EEPROM.get(eeAddress, ssid);
  eeAddress += sizeof(ssid);
  EEPROM.get(eeAddress, password);
  eeAddress += sizeof(password);
  EEPROM.get(eeAddress, tmpip);
  mqttBrokerIP = IPAddress( tmpip);
  eeAddress += sizeof(uint32_t);
  EEPROM.get(eeAddress, mqttBrokerPort);
  char ok[2 + 1];
  eeAddress += sizeof(mqttBrokerPort);
  EEPROM.get(eeAddress, ok);

  EEPROM.end();

  if (String(ok) != String("OK")) {
    Serial.println("Could not recover config, resetting to defaults.");
    ssid[0] = 0;
    password[0] = 0;
    mqttBrokerIP = IPAddress(192, 168, 1, 124);
    mqttBrokerPort = 1883;
  } else {
    Serial.println("Recovered config:");
    Serial.println(ssid);
    Serial.println(strlen(password) > 0 ? "********" : "<no password>");
    Serial.println(mqttBrokerIP);
    Serial.println(mqttBrokerPort);
  }
}

void WebUI::loadConfigFromEEPROM() {
  uint32_t tmpip = 0;
  int eeAddress = 0;
  EEPROM.begin(512);
  EEPROM.get(eeAddress, firmwareMajor);
  eeAddress += sizeof(firmwareMajor);
  EEPROM.get(eeAddress, firmwareMinor);
  eeAddress += sizeof(firmwareMinor);
  EEPROM.get(eeAddress, ssid);
  eeAddress += sizeof(ssid);
  EEPROM.get(eeAddress, password);
  eeAddress += sizeof(password);

  EEPROM.get(eeAddress, mqttid);
  eeAddress += sizeof(mqttid);

  EEPROM.get(eeAddress, tmpip);
  mqttBrokerIP = IPAddress( tmpip);
  eeAddress += sizeof(uint32_t);
  EEPROM.get(eeAddress, mqttBrokerPort);
  eeAddress += sizeof(mqttBrokerPort);
  if(loadConfigCB != NULL) {
	  eeAddress = loadConfigCB(eeAddress, firmwareMinor, firmwareMajor);
  }
  char ok[2 + 1];
  EEPROM.get(eeAddress, ok);

  EEPROM.end();

  if (String(ok) != String("OK")) {
    Serial.println("Could not recover config, trying old format...");
    loadConfigFromEEPROMV10();
  } else {
    Serial.println("Recovered config:");
    Serial.print("Firmware ");
    Serial.print(firmwareMajor);
    Serial.print(".");
    Serial.println(firmwareMinor);
    Serial.println(ssid);
    Serial.println(strlen(password) > 0 ? "********" : "<no password>");
    Serial.println(mqttid);
    Serial.println(mqttBrokerIP);
    Serial.println(mqttBrokerPort);
  }
  ArduinoOTA.setHostname(mqttid);
}

void WebUI::loadConfigFromEEPROMV10() {
  uint32_t tmpip = 0;
  int eeAddress = 0;
  EEPROM.begin(512);
  EEPROM.get(eeAddress, firmwareMajor);
  eeAddress += sizeof(firmwareMajor);
  EEPROM.get(eeAddress, firmwareMinor);
  eeAddress += sizeof(firmwareMinor);
  EEPROM.get(eeAddress, ssid);
  eeAddress += sizeof(ssid);
  EEPROM.get(eeAddress, password);
  eeAddress += sizeof(password);
  EEPROM.get(eeAddress, tmpip);
  mqttBrokerIP = IPAddress( tmpip);
  eeAddress += sizeof(uint32_t);
  EEPROM.get(eeAddress, mqttBrokerPort);
  eeAddress += sizeof(mqttBrokerPort);
  if(loadConfigCB != NULL) {
	  eeAddress = loadConfigCB(eeAddress, firmwareMinor, firmwareMajor);
  }
  char ok[2 + 1];
  EEPROM.get(eeAddress, ok);

  EEPROM.end();

  if (String(ok) != String("OK")) {
    Serial.println("Could not recover config, trying old format...");
    loadOldConfigFromEEPROM();
  } else {
    Serial.println("Recovered config:");
    Serial.print("Firmware ");
    Serial.print(firmwareMajor);
    Serial.print(".");
    Serial.println(firmwareMinor);
    Serial.println(ssid);
    Serial.println(strlen(password) > 0 ? "********" : "<no password>");
    Serial.println(mqttBrokerIP);
    Serial.println(mqttBrokerPort);
  }
}


/** Store WLAN credentials to EEPROM */

void WebUI::WebUI::saveConfig() {
  int eeAddress = 0;
  Serial.println("Save config:");
  Serial.println(mqttBrokerIP);
  Serial.println(sizeof(mqttBrokerIP));
  Serial.println(sizeof(mqttBrokerIP[0]));
  uint32_t brokerIP = (uint32_t) mqttBrokerIP;
  EEPROM.begin(512);
  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  firmwareMajor = 1;
  firmwareMinor = 1;
  EEPROM.put(eeAddress, firmwareMajor);
  eeAddress += sizeof(firmwareMajor);
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, firmwareMinor);
  eeAddress += sizeof(firmwareMinor);
  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, ssid);
  eeAddress += sizeof(ssid);
  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, password);
  eeAddress += sizeof(password);

  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, mqttid);
  eeAddress += sizeof(mqttid);

  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, brokerIP);
  eeAddress += sizeof(uint32_t);
  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, mqttBrokerPort);
  eeAddress += sizeof(mqttBrokerPort);

  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  if(saveConfigCB != NULL) {
	  eeAddress = saveConfigCB(eeAddress);
  }

  char ok[2 + 1] = "OK";
  Serial.print("eeAdr: ");
  Serial.println(eeAddress);
  EEPROM.put(eeAddress, ok);
  EEPROM.commit();
  EEPROM.end();
}

/** Is this an IP? */
boolean WebUI::isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String WebUI::toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

void WebUI::checkInterrupt() {
	if(sleepmode)
		return;
  if (interruptCounter > 0) {
    portENTER_CRITICAL(&mux);
    interruptCounter--;
    portEXIT_CRITICAL(&mux);
    Serial.println("Button pressed, interrupt triggered!");
	inSetup = true;
	startAP();
  }
}


void IRAM_ATTR WebUI::handleInterrupt() {
  portENTER_CRITICAL_ISR(&mux);
  interruptCounter++;
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR WebUI::onTimer() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter++;
  lastIsrAt = millis();
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
  if(timerCB)
	  timerCB();
}

#ifdef WEBUI_USE_BUILDIN_STATUS
void WebUI::publishData( void * pvParameters ) {
/*	if(inSetup||wifiAPmode) {
		Serial.print("No publishing: Setup or AP activated.");
		vTaskDelay(publishTimerPeriod / portTICK_PERIOD_MS);
		return;
	}
	*/
	do {
    if ( publishingTimerActive && mqtt != NULL && !otaRunning)
      if ( !wifiAPmode && !inSetup && mqtt->isConnected()) {
        char pubbuf[128] = "";

        sprintf( pubbuf, "{'status':'%s','batterylevel':%.2f}", "OK", batteryLevel );

        MqttClient::Message message;
        message.qos = MqttClient::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*) pubbuf;
        message.payloadLen = strlen(pubbuf) + 1;
        mqtt->publish(MQTT_TOPIC_STATUS, message);
        //Serial.printf("Published %s to %s\n", pubbuf, MQTT_TOPIC_STATUS);

        //	  Serial.println("Now processing callbacks...");
        	  if(publishCB)
        		publishCB(pvParameters);
        pubflag = 0;
        // Idle for 30 seconds
        mqtt->yield(500L);
		esp_err_t err = 0;
		Serial.print("Data published now going to sleep mode ");
		Serial.print(sleepmode);
		Serial.print(" for ");
		Serial.print(publishTimerPeriod);
		Serial.println(" ms.");
		//vTaskDelay(1 / portTICK_PERIOD_MS);	
		Serial.print("RTC: ");
		uint64_t rtcval = rtc_time_get();
		Serial.println((long) rtcval);
		Serial.flush();
		switch( sleepmode ) {
			case 1:
			  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
			  err = esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
			  err = esp_sleep_enable_timer_wakeup(publishTimerPeriod *1000);
			  err = esp_light_sleep_start();
			  esp_wifi_set_ps(WIFI_PS_NONE);
			  print_wakeup_reason();
			  if(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
				  Serial.println("Button pressed to discontinue sleep mode");
				  sleepmode = 0;
				  inSetup = true;
				  WebUI::instance->runWebserver( NULL );
			  }
			  break;
			case 2:
			  WiFi.disconnect();
			  WiFi.mode(WIFI_OFF); 
 			  btStop();
			  mqtt->disconnect();
			  network.stop();
			  err = esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
			  err = esp_sleep_enable_timer_wakeup(publishTimerPeriod *1000);
			  Serial.print("esp_sleep_enable_timer_wakeup returns: ");
			  Serial.println(err);
			  esp_deep_sleep_start();
			  print_wakeup_reason();
			  break;
			
			default :
				vTaskDelay(publishTimerPeriod / portTICK_PERIOD_MS);			
		}
     } else {
        // Serial.println("Publish: wait for connection to mqtt broker.");
      }
	  
//	  Serial.println("Callbacks processed.");
	if(wifiAPmode || inSetup) {
			//Serial.println("Publishing task in setup / AP-mode idle state.");
			vTaskDelay(10);
	}
	
  } while (backgroundPublishTask);
  Serial.println("Terminating publishing task.");
  WebUI::instance->stopBackgroundPublishTask();
}
#endif

void WebUI::print_wakeup_reason(){

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void WebUI::processConfigCommand(MqttClient::MessageData & md) {
  Serial.println("processConfigCommand callback triggered.");
  const MqttClient::Message& msg = md.message;
  StaticJsonDocument<200> jsonBuffer;
  static char payload[BUF_MAX];
  memcpy(payload, msg.payload, msg.payloadLen);
  payload[msg.payloadLen] = '\0';
  /*
  logfln(
    "Message arrived: qos %d, retained %d, dup %d, packetid %d, payload:[%s]",
    msg.qos, msg.retained, msg.dup, msg.id, payload
  );
  */
  Serial.print("payload: ");
  Serial.println((const char *) msg.payload);
  deserializeJson(jsonBuffer, payload);
  JsonObject root = jsonBuffer.as<JsonObject>();
  if( root["sleep"] ) {
	  Serial.print("Parsed sleep mode: ");
	  Serial.println((int) root["sleep"]);
	  sleepmode = root["sleep"];
  }
  if( root["period"] ) {
	  Serial.print("Parsed period: ");
	  Serial.println((int) root["period"]);
	  publishTimerPeriod = root["period"];
	  if(publishTimerPeriod == 0)
		publishingTimerActive = false; 
	  else
		publishingTimerActive = true;
  }
  if( root["pactive"] ) {
	  Serial.print("Parsed pactive: ");
	  Serial.println((int) root["pactive"]);
	  publishingTimerActive = (int) root["pactive"];
  }
  if( root["web"] ) {
	  Serial.print("Parsed web: ");
	  Serial.println((const char *) root["web"]);
	  startWebserver = (strcmp(root["web"], "on")== 0);
	  if(startWebserver) {
		  sleepmode = 0;
	  }
  }
  
  if( root["setup"] ) {
	  Serial.print("Parsed setup: ");
	  Serial.println((const char *) root["setup"]);
	  inSetup = (strcmp(root["setup"], "on")== 0);
	  if(inSetup) {
		  sleepmode = 0;
		  startWebserver = true;
	  }
  }
  
  Serial.print("web: ");
  Serial.println(startWebserver);
  Serial.print("setup: ");
  Serial.println(inSetup);
  Serial.print("sleep mode: ");
  Serial.println(sleepmode);
  Serial.print("sleep period: ");
  Serial.println(publishTimerPeriod);
  Serial.print("timer based publishing active: ");
  Serial.println(publishingTimerActive);
  
  // LOG_PRINTFLN( "Message parsed: %d, %d, %d", channel, value, timer );
}


void WebUI::processConfigRequest(MqttClient::MessageData & md) {
  Serial.println("processConfigRequest callback triggered.");
  const MqttClient::Message& msg = md.message;
  StaticJsonDocument<200> jsonBuffer;
  static char payload[BUF_MAX];
  memcpy(payload, msg.payload, msg.payloadLen);
  payload[msg.payloadLen] = '\0';
  logfln(
    "Message arrived: qos %d, retained %d, dup %d, packetid %d, payload:[%s]",
    msg.qos, msg.retained, msg.dup, msg.id, payload
  );
  /* JsonObject& root = jsonBuffer.parseObject(payload);
    const char* senderID = root["confreq"];
    logfln( "Message parsed: %s", senderID );
  */
  sendConfigMessage();
}

void WebUI::sendConfigMessage() {
  char pubbuf[1024] = "";

  sprintf(pubbuf, "{'id':'%s','ngpio':%d,'naout':3,'ip':'%d.%d.%d.%d', 'mqttip':'%d.%d.%d.%d', 'mqttport':%d}", mqttid, GPIO_MAX, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], mqttBrokerIP[0], mqttBrokerIP[1], mqttBrokerIP[2], mqttBrokerIP[3], mqttBrokerPort);

  logfln("Publish %s to %s", pubbuf, MQTT_TOPIC_CONFIG_RESP);
  MqttClient::Message message;
  message.qos = MqttClient::QOS0;
  message.retained = false;
  message.dup = false;
  message.payload = (void*) pubbuf;
  message.payloadLen = strlen(pubbuf) + 1;
  mqtt->publish(MQTT_TOPIC_CONFIG_RESP, message);
  pubflag = 0;

}

PublishHandlerCbk WebUI::publishCB;
void WebUI::setPublishCB(PublishHandlerCbk cb){
	publishCB = cb;
}

SubscribeHandlerCbk WebUI::subscribeCB;
void WebUI::setSubscribeCB(SubscribeHandlerCbk cb){
	subscribeCB = cb;
}

void WebUI::mqttConfigSubscribe() {
    logfln("Subscribe to %s", MQTT_TOPIC_CONFIG_CMD);
    MqttClient::Error::type rc = mqtt->subscribe(
                                   MQTT_TOPIC_CONFIG_CMD, MqttClient::QOS0, processConfigCommand
                                 );
    if (rc != MqttClient::Error::SUCCESS) {
      logfln("Subscribe error: %i", rc);
      Serial.println("Drop connection");
      mqtt->disconnect();
      return;
    } else {
      logfln("Subscribtion to %s successful.", MQTT_TOPIC_CONFIG_CMD);
    }
}

void WebUI::mqttSubscribe() {
    logfln("Subscribe to %s", MQTT_TOPIC_CONFIG_REQ);
    MqttClient::Error::type rc = mqtt->subscribe(
                                   MQTT_TOPIC_CONFIG_REQ, MqttClient::QOS0, processConfigRequest
                                 );
    if (rc != MqttClient::Error::SUCCESS) {
      logfln("Subscribe error: %i", rc);
      Serial.println("Drop connection");
      mqtt->disconnect();
      return;
    } else {
      logfln("Subscribtion to %s successful.", MQTT_TOPIC_CONFIG_REQ);
    }
}

void WebUI::homieSubscribe() {
    logfln("Subscribe to %s", "+/+/$homie");
    MqttClient::Error::type rc = mqtt->subscribe(
    		"+/+/$homie", MqttClient::QOS0, homieDiscoveryHandler
                                 );
    if (rc != MqttClient::Error::SUCCESS) {
      logfln("Subscribe error: %i", rc);
      Serial.println("Drop connection");
      mqtt->disconnect();
      return;
    } else {
      logfln("Subscribtion to %s successful.", "+/+/$homie");
      // homieSendDiscoveryResponse();
    }
}
void WebUI::homieDiscoveryHandler(MqttClient::MessageData & md) {
	Serial.println("Shit, we got discovered!");
}

void WebUI::homieSendDiscoveryResponse() {
	  char pubbuf[1024] = "";

	  sprintf(pubbuf, "{'id':'%s','ngpio':%d,'naout':3,'ip':'%d.%d.%d.%d', 'mqttip':'%d.%d.%d.%d', 'mqttport':%d}", mqttid, GPIO_MAX, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], mqttBrokerIP[0], mqttBrokerIP[1], mqttBrokerIP[2], mqttBrokerIP[3], mqttBrokerPort);

	  logfln("Publish %s to %s", pubbuf, "/+/+/homie");
	  MqttClient::Message message;
	  message.qos = MqttClient::QOS0;
	  message.retained = false;
	  message.dup = false;
	  message.payload = (void*) pubbuf;
	  message.payloadLen = strlen(pubbuf) + 1;
	  mqtt->publish("/+/+/homie", message);
	  pubflag = 0;

}
