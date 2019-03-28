$include <WebUI.h>

void WebUI::WebUI() {
	
}

bool WebUI::runWebserver( void * pvParameters ) {
  String taskMessage = "Start webserver on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  Serial.println(taskMessage);

  server.on("/", handleRoot );
  server.on("/wifi", handleWiFiSetupForm);
  server.on("/setParams", handleSetWiFiParams);
  server.on("/getData", handleGetData);
  server.on("/configMqttForm", handleMqttForm);
  server.on("/configMqtt", handleConfigMqtt);
  server.on("/thresholdConfig", handleThresholdForm);
  server.on("/setThreshold", handleSetThreshold);
  server.on("/otaActivateForm", otaActivateForm);
  server.on("/otaStart", otaStart);
  server.on("/getOTAStatus", handleGetOTAStatus);
  server.on("/fwupload", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    Serial.printf("Update return status: %s\n" , (Update.hasError()) ? "FAIL" : "OK");
    inSetup = false;
    ESP.restart();
  }, []() {
    inSetup = true;
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Upload: %s\nSize: %d\n", upload.filename.c_str(), upload.currentSize);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Serial.printf("Update begin error...\n");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
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
  server.on("/espreset", ESPrestart);
  server.on("/setAutoMode", setAutoMode);
  server.on("/switchPump", handlerSwitchPump);
  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server.onNotFound(handleNotFound);
  //here the list of headers to be recorded
  //  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  //  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  //  server.collectHeaders(headerkeys, headerkeyssize );
  server.begin();
  Serial.println("HTTP server started");

#if WEBSERVER_TASK_CORE == 0
  while (true) {
    server.handleClient();
    ArduinoOTA.handle();
    delay(100);
  }
#endif
}

void WebUI::setup() {
	  ArduinoOTA
  .onStart([]() {
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
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    inSetup = false;
    ArduinoOTA.end();
    timerAlarmEnable(timer);
    otaMessage = "Upload completed.";
    Serial.println("\nonEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    otaProgress = progress;
    otaTotal = total;
    otaMessage = "Progress: " + (String) (progress / (total / 100) + "%");
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
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
  });

#if WEBSERVER_TASK_CORE == 0
    xTaskCreatePinnedToCore(
      runWebserver,   /* Function to implement the task */
      "Webserver", /* Name of the task */
      10000,      /* Stack size in words */
      NULL,       /* Task input parameter */
      0,          /* Priority of the task */
      NULL,       /* Task handle. */
      0);  /* Core where the task should run */
#else
    runWebserver( NULL );
#endif
}

void WebUI::loop() {
	
}