#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <FS.h>

char ssid[32], pass[64];
MDNSResponder mdns;
ESP8266WebServer server(80);
String lastResponse;
String serialData;

void setup() {
  Serial.begin(115200);
  delay(20);
  EEPROM.begin(512);
  delay(20);
  
  EEPROM.get(0, ssid);
  EEPROM.get(32, pass);
  
  uint8_t failcount = 0;
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    failcount++;
    if (failcount > 20) { // 1 min
      Serial.println("M117 WIFI ERROR");
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(100);

      uint8_t num_ssids = WiFi.scanNetworks();
      // TODO: NONE? OTHER?
      String wifiConfigHtml = F("<html><body><h1>Select your WiFi network:</h1><br /><form method=\"POST\">");
      for (uint8_t i = 0; i < num_ssids; i++) {
         wifiConfigHtml += "<input type=\"radio\" id=\"" + WiFi.SSID(i) + "\"name=\"ssid\" value=\"" + WiFi.SSID(i) + "\" /><label for=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + "</label><br />";
      }
      wifiConfigHtml += F("<input type=\"password\" name=\"password\" /><br /><input type=\"submit\" value=\"Save and reboot\" /></form></body></html>");
      
      Serial.println("M117 FOUND " + String(num_ssids) + " WIFI");
      
      delay(5000);
      DNSServer dns;
      IPAddress apIP(192, 168, 1, 1);
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP("ESPrinter");
      Serial.println("M117 WiFi -> ESPrinter");
      dns.setErrorReplyCode(DNSReplyCode::NoError);
      dns.start(53, "*", apIP);
      
      server.on("/", HTTP_GET, [&wifiConfigHtml]() {
        server.send(200, "text/html", wifiConfigHtml);
      });

      server.on("/", HTTP_POST, []() {
        if (server.args() <= 0) {
          server.send(500, "text/plain", F("Got no data, go back and retry"));
          return;
        }
        for (uint8_t e = 0; e < server.args(); e++) {
          if (server.argName(e) == "password") server.arg(e).toCharArray(pass, 64);//pass = server.arg(e);
          else if (server.argName(e) == "ssid") server.arg(e).toCharArray(ssid, 32);//ssid = server.arg(e);
        }
        
        EEPROM.put(0, ssid);
        EEPROM.put(32, pass);
        server.send(200, "text/html", F("<h1>All set!</h1><br /><p>Will now reboot.</p>"));
        Serial.println("M117 SSID: " + String(ssid) + ", PASS: " + String(pass));
        delay(50);
        ESP.restart();
      });
      server.begin();
      Serial.println("M117 HTTP://" + WiFi.softAPIP());
      for (;;) { // THIS ONE IS FOR WIFI AP SETTINGS PAGE
        server.handleClient();
        dns.processNextRequest();
        delay(1);
      }
    }
  }

  if (mdns.begin("esprinter", WiFi.localIP())) {
    MDNS.addService("http", "tcp", 80);
  }
  
  SPIFFS.begin();

  server.onNotFound(fsHandler);
  server.on("/rr_connect", handleConnect);
  server.on("/rr_disconnect", handleDisconnect);
  server.on("/rr_status", handleStatus);
  server.on("/rr_reply", handleReply);
  server.on("/rr_files", handleFiles);
  server.on("/rr_gcode", handleGcode);
  server.on("/rr_config", handleConfig);
  server.begin();
}

void loop() {
  server.handleClient();
  delay(1);
  
  while (Serial.available() > 0) {
    char character = Serial.read();
    if (character == '\n') {
      Serial.println("M117 Serial data: " + serialData);
      lastResponse = String(serialData);
      serialData = "";
    } else {
      Serial.println("M117 serial char: " + character);
      serialData.concat(character);
    }
  }
}




void fsHandler() {
  String path = server.uri();
  if (path.endsWith("/")) path += F("index.html");
  File dataFile = SPIFFS.open(path, "r");
  if (!dataFile) {
    send404();
    return;
  }
  server.sendHeader("Content-Length", String(dataFile.size()));
  String dataType = "text/plain";
  if (path.endsWith(".gz")) server.sendHeader(F("Content-Encoding"), "gzip");
  if (path.endsWith(".html")) dataType = F("text/html");
  else if (path.endsWith(".css")) dataType = F("text/css");
  else if (path.endsWith(".js")) dataType = F("application/javascript");
  else if (path.endsWith(".js.gz")) dataType = F("application/javascript");
  else if (path.endsWith(".css.gz")) dataType = F("text/css");
  else if (path.endsWith(".gz")) dataType = F("application/x-gzip");
  server.streamFile(dataFile, dataType);
  dataFile.close();
}






void handleConnect() {
  // ALL PASSWORDS ARE VALID! YAY!
  // TODO: NO, SERIOUSLY, CONSIDER ADDING AUTH HERE. LATER MB?
  Serial.println("M117 " + WiFi.localIP());
  server.send(200, "application/json", "{\"err\":0}");
}

void handleDisconnect() {
  // TODO: DEAUTH?..
  server.send(200, F("application/json"), F("{\"err\":0}"));
}

void handleStatus() {
  uint8_t type = 1;
  if (server.args() > 0) {
    type = atoi(server.arg(0).c_str());
  }
  Serial.println("M408 S" + String(type));
  Serial.setTimeout(5000); // 2s
  serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, F("application/json"), lastResponse);
}

void handleReply() {
  server.send(200, F("text/plain"), lastResponse);
}

void handleFiles() {
  String dir = "";
  if (server.args() > 0) {
    dir = server.arg(0);
  }
  Serial.println("M20 S2 P" + dir);
  Serial.setTimeout(5000);
  serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, "application/json", lastResponse);
}

void handleGcode() {
  String gcode = "";
  if (server.args() > 0) {
    gcode = server.arg(0);
    urldecode(gcode);
  }
  Serial.println(gcode);
  server.send(200, "application/json", F("{\"buff\": 16}"));
}

void handleConfig() {
  Serial.println(F("M408 S5"));
  Serial.setTimeout(5000);
  serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, "application/json", lastResponse);
}






void send404() {
  /*Serial.print("ERROR 404:");
  Serial.print((server.method() == HTTP_GET) ? " GET " : " POST ");
  Serial.print(server.uri() + ", ");
  if (server.args() > 0) {
    Serial.print("arguments:");
    for (uint8_t i=0; i<server.args(); i++) {
      Serial.print(" " + server.argName(i) + ": " + server.arg(i) + ", ");
    }
  }
  Serial.println("\n");
  */
  server.send(404, "application/json", "{\"err\": \"404: " + server.uri() + " NOT FOUND\"}");
}

void send500(String errorMessage) {
  //Serial.println("ERROR 500: " + server.uri());
  server.send(500, "application/json", "{\"err\": \"500: " + errorMessage + "\"}");
}

void urldecode(String &input) { // LAL ^_^
  input.replace("%20", " ");
  input.replace("+", " ");
  input.replace("%21", "!");
  input.replace("%22", "\"");
  input.replace("%23", "#");
  input.replace("%24", "$");
  input.replace("%25", "%");
  input.replace("%26", "&");
  input.replace("%27", "\'");
  input.replace("%28", "(");
  input.replace("%29", ")");
  input.replace("%30", "*");
  input.replace("%31", "+");
  input.replace("%2C", ",");
  input.replace("%2E", ".");
  input.replace("%2F", "/");
  input.replace("%2C", ",");
  input.replace("%3A", ":");
  input.replace("%3A", ";");
  input.replace("%3C", "<");
  input.replace("%3D", "=");
  input.replace("%3E", ">");
  input.replace("%3F", "?");
  input.replace("%40", "@");
  input.replace("%5B", "[");
  input.replace("%5C", "\\");
  input.replace("%5D", "]");
  input.replace("%5E", "^");
  input.replace("%5F", "-");
  input.replace("%60", "`");
}
