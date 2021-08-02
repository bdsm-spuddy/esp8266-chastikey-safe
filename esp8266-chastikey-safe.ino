// Using an ESP8266 as a replacement controller for a digital safe board
// providing a web-UI for control
//
// Specifically configured for chastikey API
//
// (c) 2021 bdsm@spuddy.org
//
// MIT License

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#include "html.h"
#include "rootCA.h"

// This is pin D6 on most boards; this is the pin that needs to be
// connected to the relay
#define default_pin D6

// We need to store some values in EEPROM.
//   safe UI username
//   safe UI password
//   WiFi SSID
//   WiFi password
//   API Key
//   API Secret
//   Discord ID/Username
//   Lock ID
//   API URL
//   Safe Name
//   Pin to open solenoid
//
// For simplicitly we'll limit them to 100 characters each and sprinkle
// them through the EEPROM at 128 byte offsets
// It's not very efficient, but we have up to 4096 bytes to play with
// so...
//
// We put a magic string in front to detect if the values are good or not

#define EEPROM_SIZE 2048
#define maxpwlen 100
#define eeprom_magic F("PSWD:")
#define eeprom_magic_len 5

#define ui_username_offset   0
#define ui_pswd_offset       128
#define ui_wifi_ssid_offset  256
#define ui_wifi_pswd_offset  384
#define api_key_offset       512
#define api_secret_offset    640
#define username_offset      768
#define lockid_offset        896
#define apiurl_offset        1024
#define safename_offset      1152
#define pin_offset           1280

enum safestate
{
  UNLOCKED,
  LOCKED
};

/////////////////////////////////////////

// Global variables
int state=UNLOCKED;
boolean wifi_connected;
boolean allow_updates = false;

// For communication to the API
BearSSL::WiFiClientSecure client;
BearSSL::X509List cert(rootCA);

#define DEFAULT_API F("https://api.chastikey.com/v0.5/checklock.php")

// These can be read at startup time from EEPROM
String ui_username;
String ui_pswd;
String wifi_ssid;
String wifi_pswd;
String api_key;
String api_secret;
String username;
String lockid;
String apiurl;
String safename;
String pinstr;
int    pin;

// Create the webserver structure for port 80
ESP8266WebServer server(80);

/////////////////////////////////////////

// Read/write EEPROM values

String get_pswd(int offset)
{ 
  char d[maxpwlen];
  String pswd;

  for (int i=0; i<maxpwlen; i++)
  { 
    d[i]=EEPROM.read(offset+i);
  }

  pswd=String(d);
  
  if (pswd.startsWith(eeprom_magic))
  { 
    pswd=pswd.substring(eeprom_magic_len);
  }
  else
  {
    pswd="";
  }

  return pswd;
}

void set_pswd(String s, int offset, bool commit=true)
{ 
  String pswd=eeprom_magic + s;

  for(int i=0; i < pswd.length(); i++)
  {
    EEPROM.write(offset+i, pswd[i]);
  }
  EEPROM.write(offset+pswd.length(), 0);
  if (commit)
    EEPROM.commit();
}

/////////////////////////////////////////

void send_text(String s)
{
  server.send(200, F("text/html"), s);
}

/////////////////////////////////////////

// Routine to talk to Chastikey

bool good_api_result;
  
void talk_to_api()
{
  good_api_result = false;

  if (lockid == "")
    return send_text(F("No lock has been set up.  The safe may be opened."));

  if (username == "" )
    return send_text(F("No username has been set up.  Unable to check lock status"));

  String us="";
  String u="";
  if (username.startsWith(F("u:")))
  {
    us=F("username");
    u=username.substring(2);
  }
  else if (username.startsWith(F("d:")))
  {
    us=F("discordID");
    u=username.substring(2);
  }
  else
    return send_text(F("Bad username details!"));

  HTTPClient https;
  https.useHTTP10(true);

  Serial.println(apiurl);
  if (https.begin(client, apiurl))
  { 
    https.addHeader(F("ClientID"),api_key);
    https.addHeader(F("ClientSecret"),api_secret);
    https.addHeader(F("Content-Type"),F("application/x-www-form-urlencoded"));

    // start connection and send HTTP header
    Serial.println(us + "=" + u + F("&lockID=") + lockid);
    int httpCode = https.POST(us + "=" + u + F("&lockID=") + lockid);

    if (httpCode != 200)
    {
      https.end();
      return send_text(F("Problems talking to API: Response ") + String(httpCode) + " " + https.errorToString(httpCode));
    }

    // Now we need to parse the JSON data

    // Filter just the elements we want to reduce memory usage
    StaticJsonDocument<400> filter;
    filter["response"]["status"] = true;
    filter["response"]["message"] = true;
    filter["locks"][0]["lockedBy"] = true;
    filter["locks"][0]["lockName"] = true;
    filter["locks"][0]["status"] = true;

    StaticJsonDocument<10000> raw_data;
    DeserializationError error = deserializeJson(raw_data, https.getStream(), DeserializationOption::Filter(filter));

    if (error)
      return send_text(F("Unable to decode JSON! ") + String(error.c_str()));

    Serial.println(F("JSON is:"));
    serializeJsonPretty(raw_data, Serial);
    Serial.println();
 
    if (raw_data["response"]["status"] != 200)
      return send_text((const char*)raw_data["response"]["message"]);

    int l = raw_data["locks"].size();

    if (l == 0)
      return send_text(F("No lock found for ID ") + String(lockid) + F("!  Either the lockID is wrong or you deleted it..."));

    good_api_result=true;
    String s = F("There are ") + String(l) + F(" locks for lock group id ") + String(lockid )+ F("<br>");

    bool to_unlock=false;
    for (int i=0;i<l;i++)
    {
      if (raw_data["locks"][i]["status"] == F("UnlockedReal"))
        to_unlock=true;
      s += F("Lock ") + String(i+1);
      if (raw_data["locks"][i]["lockName"] != "")
      {
        String l = raw_data["locks"][i]["lockName"];
        l.replace(F("<"),F("&lt;"));
        s += F(" (") + l + F(")");
      }
      if (raw_data["locks"][i]["lockedBy"] != "")
      {
        String l = raw_data["locks"][i]["lockedBy"];
        l.replace(F("<"),F("&lt;"));
        s += F(" locked by ") + l;
      }

      s += F(": ") + String((const char *)raw_data["locks"][i]["status"]) + F("<br>");
    }

    if (to_unlock)
    {
      s += F("<p>This lock is now available to unlock, and has been removed from the safe");
      lockid = "";
      set_pswd(lockid, lockid_offset);
      state=UNLOCKED;
    }

    https.end();
    return send_text(s);
  }
  else
    return send_text(F("Could not connect to server"));
}

/////////////////////////////////////////

// These functions manipulate the safe state.
// They look like they don't take any params, because they use the
// global server state variable to read the request.  This only works
// 'cos the main loop is single threaded!

void status()
{
  return talk_to_api();

#if 0
  if (state==UNLOCKED)
    send_text(F("Safe can be opened"));
  else
    send_text(F("Lock is still running; safe is locked"));
#endif
}

void opensafe()
{
  if (state == LOCKED)
  {
    send_text(F("Can not open; lock is still running"));
  }
  else
  {
    String d = server.arg("duration");
    int del=d.toInt();
    if (del==0) { del=5; }
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(pin, HIGH);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    send_text(F("Unlocking safe for ") + String(del) + F(" seconds<br>"));
    while(del--)
    {
      delay(1000);
      server.sendContent(String(del) + F("...\n"));
      if (del%10 == 0) server.sendContent(F("<br>"));
    }
    digitalWrite(pin, LOW);
    digitalWrite(LED_BUILTIN, HIGH);
    server.sendContent(F("Completed"));
    server.sendContent("");
  }
}

void display_auth()
{
  String us="";
  String ds="";
  String u="";
  if (username.startsWith(F("u:")))
  {
    us=F("selected");
    u=username.substring(2);
  }
  else if (username.startsWith(F("d:")))
  {
    ds=F("selected");
    u=username.substring(2);
  }

  String page = FPSTR(change_auth_html);
         page.replace(F("##apikey##"), api_key);
         page.replace(F("##apisecret##"), api_secret);
         page.replace(F("##usernameselected##"), us);
         page.replace(F("##discordselected##"), ds);
         page.replace(F("##idvalue##"), u);
         page.replace(F("##apiurl##"), apiurl);
         page.replace(F("##ui_username##"), ui_username);

  send_text(page);
}

void set_ap()
{
  if (server.hasArg("setwifi"))
  {
     Serial.println(F("Setting WiFi client"));
     safename=server.arg("safename");
     safename.replace(F(".local"),"");
     if (safename != "")
     {
       Serial.println(F("  Setting mDNS name"));
       set_pswd(safename,safename_offset);
     }

     pinstr=server.arg("pin");
     if (pinstr != "")
     {
       Serial.println(F("  Setting active pin"));
       set_pswd(pinstr,pin_offset);
     }

     if (server.arg("ssid") != "" && server.arg("password") != "")
     {
       Serial.println(F("  Setting network"));
       set_pswd(server.arg("ssid"), ui_wifi_ssid_offset, false);
       set_pswd(server.arg("password"), ui_wifi_pswd_offset);
     }
     send_text(F("Restarting in 5 seconds"));
     delay(5000);
     ESP.restart();
  }
  String page = FPSTR(change_ap_html);
         page.replace(F("##safename##"), safename);
         page.replace(F("##pin##"), String(pin));
  send_text(page);
}

void set_auth()
{
  Serial.println(F("Setting Auth details"));
  ui_username=server.arg("username");
  if (server.arg("password") != "")
    ui_pswd=server.arg("password");

  set_pswd(ui_username, ui_username_offset,false);
  set_pswd(ui_pswd, ui_pswd_offset);

  send_text(F("Password reset"));
}

void set_api()
{
  Serial.println(F("Setting API details"));
  api_key=server.arg("apikey");
  api_secret=server.arg("apisecret");
  set_pswd(api_key, api_key_offset, false);
  set_pswd(api_secret, api_secret_offset);
  send_text(F("API details updated"));
}

void set_apiurl()
{
  Serial.println(F("Setting API URL"));
  if (state == LOCKED)
    return send_text(F("Safe is locked - Can not change now"));

  String newurl = server.arg("apiurl");
  if (newurl != "" && newurl != apiurl)
  {
    apiurl=newurl;
    set_pswd(apiurl, apiurl_offset);
    send_text(F("URL updated"));
  }
  else
    send_text(F("No update made"));
}

void set_user()
{
  Serial.println(F("Setting API details"));
  username=server.arg("idtype") + ":" + server.arg("idvalue");
  set_pswd(username, username_offset);
  send_text(F("User details updated"));
}

void enable_update(bool enable)
{
  if (enable)
  {
    if (lockid == "")
    {
      allow_updates = true;
      send_text(F("Updates can be sent using BasicOTA to: ") + WiFi.localIP().toString());
    }
    else
    {
      allow_updates = false;
      send_text(F("Can not perform update while safe is locked"));
    }
  }
  else
  {
    allow_updates = false;
    send_text(F("Update server disabled"));
  }
}

void set_lock()
{
  Serial.println(F("Setting lock"));
  if (state == LOCKED)
    return send_text(F("Safe is already locked"));

  lockid=server.arg("session");
  talk_to_api();
  if (good_api_result)
    set_pswd(lockid, lockid_offset);
  else
    lockid="";

  if (lockid == "")
  {
    state=UNLOCKED;
    // Ensure update server is disabled
    enable_update(0);
  }
  else
    state=LOCKED;
}

/////////////////////////////////////////

boolean handleRequest()
{ 
  String path=server.uri();
  if (!wifi_connected)
  {
    // If we're in AP mode then all requests must go to change_ap
    // and there's no authn required
    ui_username = "";
    ui_pswd = "";
    path=F("/change_ap.html");
  }

  Serial.println(F("New client for >>>")+path+F("<<<"));

  for(int i=0;i<server.args();i++)
  {
    Serial.println(F("Arg ") + String(i) + F(": ") + server.argName(i) + F(" --- ") + server.arg(i));
  }

  // Ensure username/password have been passed
  if (ui_username != "" && !server.authenticate(ui_username.c_str(), ui_pswd.c_str()))
  {
    Serial.println(F("Bad authentication; login needed"));
    server.requestAuthentication();
    return true;
  }

       if (path == F("/"))                 { send_text(FPSTR(index_html)); }
  else if (path == F("/main_frame.html"))  { send_text(FPSTR(main_frame_html)); }
  else if (path == F("/menu_frame.html"))  { send_text(FPSTR(menu_frame_html)); }
  else if (path == F("/top_frame.html"))   { send_text(FPSTR(top_frame_html)); }
  else if (path == F("/change_auth.html")) { display_auth(); }
  else if (path == F("/change_ap.html"))   { set_ap(); }
  else if (path == F("/enable_update"))    { enable_update(1); }
  else if (path == F("/disable_update"))   { enable_update(0); }
  else if (path == F("/safe/"))
  {
         if (server.hasArg("status"))     { status(); }
    else if (server.hasArg("open"))       { opensafe(); }
    else if (server.hasArg("setauth"))    { set_auth(); }
    else if (server.hasArg("setapi"))     { set_api(); }
    else if (server.hasArg("setapiurl"))  { set_apiurl(); }
    else if (server.hasArg("setuser"))    { set_user(); }
    else if (server.hasArg("set_lock"))   { set_lock(); }
    else return false;
  }
  else
  {
    Serial.println(F("File not found"));
    return false;
  }
  return true;
}

/////////////////////////////////////////

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println(F("Starting..."));

  // Get the EEPROM contents into RAM
  EEPROM.begin(EEPROM_SIZE);

  // Ensure LED is off
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println(F("Getting passwords from EEPROM"));

  // Try reading the values from the EEPROM
  ui_username = get_pswd(ui_username_offset);
  ui_pswd     = get_pswd(ui_pswd_offset);
  wifi_ssid   = get_pswd(ui_wifi_ssid_offset);
  wifi_pswd   = get_pswd(ui_wifi_pswd_offset);
  api_key     = get_pswd(api_key_offset);
  api_secret  = get_pswd(api_secret_offset);
  username    = get_pswd(username_offset);
  lockid      = get_pswd(lockid_offset);
  apiurl      = get_pswd(apiurl_offset);
  safename    = get_pswd(safename_offset);
  pinstr      = get_pswd(pin_offset);

  if (apiurl == "")
    apiurl = DEFAULT_API;

  if (safename == "")
    safename=F("safe");

  if (pinstr != "")
    pin=pinstr.toInt();
  else
    pin=default_pin;

  // Set the safe state
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  if (lockid != "")
  { 
    state=LOCKED;
  }

  // This is a debugging line; it's only sent to the serial
  // port which can only be accessed when the safe is unlocked.
  // We don't exposed passwords!
  Serial.println(F("Found in EEPROM:"));
  Serial.println(F("  UI Username >>>")+ ui_username + F("<<<"));
  Serial.println(F("  UI Password >>>")+ ui_pswd + F("<<<"));
  Serial.println(F("  Wifi SSID   >>>")+ wifi_ssid + F("<<<"));
  Serial.println(F("  Wifi Pswd   >>>")+ wifi_pswd + F("<<<"));
  Serial.println(F("  Safe Name   >>>")+ safename + F("<<<"));
  Serial.println(F("  API Key     >>>")+ api_key + F("<<<"));
  Serial.println(F("  API Secret  >>>")+ api_secret + F("<<<"));
  Serial.println(F("  API URL     >>>")+ apiurl + F("<<<"));
  Serial.println(F("  Username    >>>")+ username + F("<<<"));
  Serial.println(F("  LockID      >>>")+ lockid + F("<<<"));
  Serial.println(F("  Safename    >>>")+ safename + F("<<<"));
  Serial.println(F("  Relay Pin   >>>")+ String(pin) + F("<<<"));

  // Connect to the network
  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  wifi_connected=false;
  if (wifi_ssid != "")
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pswd);
    Serial.print("Connecting to ");
    Serial.print(wifi_ssid); Serial.println(" ...");

    // Wait for the Wi-Fi to connect.  Give up after 60 seconds and
    // let us fall into AP mode
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i < 60)
    {
      Serial.print(++i); Serial.print(' ');
      delay(1000);
    }
    Serial.println('\n');
    wifi_connected = (WiFi.status() == WL_CONNECTED);
  }

  if (wifi_connected)
  {
    Serial.println("Connection established!");  
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname:\t");
    Serial.println(WiFi.hostname());

    // Get the current time.  We need this for TLS cert validation
    // it's not instant.  Timezone handling... GMT is always good :-)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for NTP time sync: ");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2)
    {
      delay(500);
      Serial.print(".");
      now = time(nullptr);
    }
    Serial.println("");
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.println(asctime(&timeinfo));
  }
  else
  {
    // Create an Access Point that mobile devices can connect to
    unsigned char mac[6];
    char macstr[7];
    WiFi.softAPmacAddress(mac);
    sprintf(macstr, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    String AP_name="Safe-"+String(macstr);
    Serial.println("No connection; creating access point: "+AP_name);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_name);
  }

  //initialize mDNS service.
  MDNS.begin(safename);
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS responder started");

  // This structure just lets us send all requests to the handler
  // If we can't handle it then send a 404 response
  server.onNotFound([]()
  {
    if (!handleRequest())
      server.send(404, "text/html", "Not found");
  });

  // Ensure that when we talk to Chastikey we're validating the cert
  client.setTrustAnchors(&cert);

  // Start TCP (HTTP) server
  server.begin();
  
  Serial.println("TCP server started");

  // Configure the OTA update service, but don't start it yet!
  ArduinoOTA.setHostname(safename.c_str());

  if (ui_pswd != "")
    ArduinoOTA.setPassword(ui_pswd.c_str());

  ArduinoOTA.onStart([]()
  {
    Serial.println(F("Starting updating"));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nEnd"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
  });

  ArduinoOTA.begin();

  Serial.println(F("OTA service configured"));
}

void loop()
{
  MDNS.update();
  server.handleClient();
  if (allow_updates)
    ArduinoOTA.handle();
}
