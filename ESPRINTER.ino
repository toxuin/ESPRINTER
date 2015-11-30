#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <FS.h>

#define ESPRINTER_VERSION "1"

char ssid[32], pass[64];
MDNSResponder mdns;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiServer tcp(23);
WiFiClient tcpclient;
String lastResponse;
String serialData;
String fileUploading = "";
String lastUploadedFile = "";

void setup() {
  Serial.begin(115200);
  delay(20);
  EEPROM.begin(512);
  delay(20);

  EEPROM.get(0, ssid);
  EEPROM.get(32, pass);

  uint8_t failcount = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    failcount++;
    if (failcount > 50) { // 1 min
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
          String argument = server.arg(e);
          urldecode(argument);
          if (server.argName(e) == "password") argument.toCharArray(pass, 64);//pass = server.arg(e);
          else if (server.argName(e) == "ssid") argument.toCharArray(ssid, 32);//ssid = server.arg(e);
        }
        
        EEPROM.put(0, ssid);
        EEPROM.put(32, pass);
        EEPROM.commit();
        server.send(200, "text/html", F("<h1>All set!</h1><br /><p>(Please reboot me.)</p>"));
        Serial.println("M117 SSID: " + String(ssid) + ", PASS: " + String(pass));
        delay(50);
        ESP.restart();
      });
      server.begin();
      Serial.println("M117 HTTP://" + WiFi.softAPIP().toString());
      for (;;) { // THIS ONE IS FOR WIFI AP SETTINGS PAGE
        server.handleClient();
        dns.processNextRequest();
        delay(1);
      }
    }
  }

  MDNS.begin("esprinter");
  MDNS.addService("http", "tcp", 80);

  SPIFFS.begin();

  server.onNotFound(fsHandler);
  server.on("/rr_connect", handleConnect);
  server.on("/rr_disconnect", handleDisconnect);
  server.on("/rr_status", handleStatus);
  server.on("/rr_reply", handleReply);
  server.on("/rr_files", handleFiles);
  server.on("/rr_gcode", handleGcode);
  server.on("/rr_config", handleConfig);
  server.on("/rr_upload_begin", handleUploadStart);
  server.on("/rr_upload", handleUploadData);
  server.on("/rr_upload_end", handleUploadEnd);
  server.on("/rr_upload_cancel", handleUploadCancel);
  server.on("/rr_delete", handleDelete);
  server.on("/rr_fileinfo", handleFileinfo);
  server.on("/rr_mkdir", handleMkdir);
  
  // UNSUPPORTED STUFF
  server.on("/rr_move", handleUnsupported);

  // SYSTEM STUFF
  server.on("/rr_update", handleRemoteUpdate);
  httpUpdater.setup(&server);
  server.begin();
  tcp.begin();
  tcp.setNoDelay(true);
}

void loop() {
  server.handleClient();
  delay(1);

  while (Serial.available() > 0) {
    char character = Serial.read();
    if (character == '\n' || character == '\r') {
      if (serialData.startsWith("ok")) {
          serialData = "";
          continue;
      }
      tcpclient.write(serialData.c_str(), strlen(serialData.c_str()));
      tcpclient.flush();
      delay(1);
      lastResponse = String(serialData);
      serialData = "";
    } else {
      serialData.concat(character);
    }
  }

  // DISCONNECT ALL IF SOMEONE IS ALLREADY CONNECTED
  if (tcp.hasClient()) {
      if (tcpclient && tcpclient.connected()) {
          WiFiClient otherGuy = tcp.available();
          otherGuy.stop();
      } else {
          tcpclient = tcp.available();
      }
  }

  // PUSH FRESH DATA FROM TELNET TO SERIAL
  if (tcpclient && tcpclient.connected()) {
    while (tcpclient.available()) {
      uint8_t data = tcpclient.read();
      tcpclient.write(data); // ECHO BACK TO SEE WHATCHA TYPIN
      Serial.write(data);
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
  //if (path.endsWith(".gz")) server.sendHeader(F("Content-Encoding"), "gzip");
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
  Serial.println("M117 " + WiFi.localIP().toString());
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
  if (serialData.startsWith("ok")) serialData = Serial.readStringUntil('\n');
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
  urldecode(dir);
  Serial.println("M20 S2 P" + dir);
  Serial.setTimeout(5000);
  serialData = Serial.readStringUntil('\n');
  if (serialData.startsWith("ok")) serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, "application/json", lastResponse);
}

void handleGcode() {
  String gcode = "";
  if (server.args() > 0) {
    gcode = server.arg(0);
  } else {
    server.send(500, "application/json", "{\"err\":\"ERROR 500: EMPTY COMMAND\"}");
    return;
  }
  urldecode(gcode);
  Serial.println(gcode);
  server.send(200, "application/json", F("{\"buff\": 16}"));
}

void handleConfig() {
  Serial.println(F("M408 S5"));
  Serial.setTimeout(5000);
  serialData = Serial.readStringUntil('\n');
  if (serialData.startsWith("ok")) serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, "application/json", lastResponse);
}

void handleUploadStart() {
  if (server.args() > 0) {
    fileUploading = server.arg(0);
  } else {
    server.send(500, "application/json", "{\"err\":\"ERROR 500: PLEASE SPECIFY FILENAME\"}");
    return;
  }
  bool compat = false;
  if (server.args() > 1) {
    compat = (server.arg(1) == "true");
  }
  urldecode(fileUploading);
  lastUploadedFile = fileUploading;
  // TODO: CHECK FOR VALID SERVER RESPONSE!!!! IMPORTANT!
  if (!compat) {
    Serial.println("M575 P1 B460800 S0"); // CHANGE BAUDRATE ON 3DPRINTER
    Serial.flush();
    delay(200);
    Serial.end();
    delay(200);
    Serial.begin(460800);
    delay(200);
    Serial.flush();
  }
  Serial.println("M28 " + fileUploading);
  Serial.flush();
  server.send(200, "application/json", "{\"err\":0}");
}

void handleUploadData() {
  if (fileUploading == "") {
    server.send(500, "application/json", "{\"err\":\"ERROR 500: NOT UPLOADING FILES!\"}");
    return;
  }
  String data = "";
  if (server.args() > 0) {
    data = server.arg(0);
  } else {
    server.send(500, "application/json", "{\"err\":\"ERROR 500: NO DATA RECEIVED\"}");
    return;
  }
  urldecode(data);
  Serial.println(data);
  Serial.flush();
  server.send(200, F("application/json"), "{\"err\":0}");
}

void handleUploadEnd() {
  if (fileUploading == "") {
    server.send(500, "application/json", "{\"err\": \"ERROR 500: NOT UPLOADING ANY FILES\"}");
    return;
  }
  bool compat = false;
  if (server.args() > 0) {
    compat = (server.arg(0) == "true");
  }
  Serial.println("M29 " + fileUploading);
  if (!compat) {
    Serial.println("M575 P1 B115200 S0"); // CHANGE BAUDRATE ON 3DPRINTER
    Serial.flush();
    delay(200);
    Serial.end();
    delay(200);
    Serial.begin(115200);
    delay(200);
    Serial.flush();
  }
  fileUploading = "";
  server.send(200, "application/json", "{\"err\":0}");
}

void handleUploadCancel() {
  // IS SENT AFTER UPLOAD END
  Serial.println("M30 " + lastUploadedFile);
  server.send(200, "application/json", "{\"err\":0}");
}

void handleDelete() {
  String fileName = "";
  if (server.args() > 0) {
    fileName = server.arg(0);
  } else {
    server.send(500, "application/json", "{\"err\": \"ERROR 500: NO FILENAME PROVIDED\"}");
    return;
  }
  urldecode(fileName);
  Serial.println("M30 " + fileName);
  server.send(200, "application/json", "{\"err\":0}");
}

void handleFileinfo() {
  String fileName = "";
  if (server.args() > 0) {
    fileName = server.arg(0);
    urldecode(fileName);
  }
  if (fileName == "") {
    Serial.println("M36");
  } else {
    Serial.println("M36 " + fileName);
  }
  Serial.setTimeout(5000);
  serialData = Serial.readStringUntil('\n');
  if (serialData.startsWith("ok")) serialData = Serial.readStringUntil('\n');
  lastResponse = String(serialData);
  server.send(200, "application/json", lastResponse);
}

void handleMkdir() {
  String dirName = "";
  if (server.args() < 2 || server.arg(1) != "true") { // 2 ARGS FOR COMPATMODE OR NOPE
    server.send(200, "application/json", F("{\"err\":\"Unsupported operation :(\"}"));
    return;
  }
  dirName = server.arg(0);
  urldecode(dirName);
  if (dirName == "") {
    server.send(500, "application/json", "{\"err\":\"ERROR 500: NO DIR NAME PROVIDED\"}");
    return;
  }
  Serial.println("M32 " + dirName);
  server.send(200, F("application/json"), F("{\"err\":0}"));
}



void handleRemoteUpdate() {
  if (server.args() < 2) {
    server.send(500, F("application/json"), F("{\"err\":\"ERROR 500: wrong update server provided\"}"));
    return;
  }
  String updateServer = server.arg(0);
  String updateUrl = server.arg(1);
  urldecode(updateUrl); // ??
  urldecode(updateServer); // ??
  if (updateServer == "" || updateUrl == "") {
    server.send(500, F("application/json"), F("{\"err\":\"ERROR 500: wrong update server provided\"}"));
    return;
  }
  t_httpUpdate_return updateResult = ESPhttpUpdate.update(updateServer, 80, updateUrl, ESPRINTER_VERSION);
  switch (updateResult) {
    case HTTP_UPDATE_FAILED:
      Serial.println(F("M117 ESPRINTER UPDATE ERROR"));
      server.send(500, F("application/json"), F("{\"err\": \"Update failed\"}"));
      break;

    case HTTP_UPDATE_NO_UPDATES:
      server.send(200, F("application/json"), F("{\"err\":\"No updates\"}"));
      break;

    case HTTP_UPDATE_OK:
      Serial.println(F("M117 ESPRINTER UPDATED!"));
      server.send(200, F("application/json"), F("{\"err\":0}"));
      break;
  }
}





void handleUnsupported() {
  server.send(200, "application/json", F("{\"err\":\"Unsupported operation :(\"}"));
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
  server.send(500, "application/json", "{\"err\": \"500: " + errorMessage + "\"}");
}

void urldecode(String &input) { // LAL ^_^
  input.replace("%0A", String('\n'));
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
