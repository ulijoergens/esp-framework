#include <Arduino.h>
#include "WiFi.h"
#include <cstdio>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

const byte DNS_PORT = 53;

Class WebUI {
	static private Webserver server(80);
	static private DNSServer dnsServer;
	private 
	void WebUI();
	bool setup();
	
	
}