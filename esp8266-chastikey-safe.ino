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

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#include "html.h"
#include "rootCA.h"

// This is pin D6 on most boards; this is the pin that needs to be
// connected to the relay
#define pin D6

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
//
// For simplicitly we'll limit them to 100 characters each and sprinkle
// them through the EEPROM at 128 byte offsets
// It's not very efficient, but we have up to 4096 bytes to play with
// so...
//
// We put a magic string in front to detect if the values are good or not

#define EEPROM_SIZE 2048
#define maxpwlen 100
#define eeprom_magic "PSWD:"
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

enum safestate
{
  UNLOCKED,
  LOCKED
};

/////////////////////////////////////////

// Global variables
int state=UNLOCKED;
boolean wifi_connected;

// For communication to the API
BearSSL::WiFiClientSecure client;
BearSSL::X509List cert(rootCA);

#define DEFAULT_API "https://api.chastikey.com/v0.5/checklock.php"

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
  server.send(200, "text/html", s);
}

/////////////////////////////////////////

// Routine to talk to Chastikey

bool good_api_result;
  
void talk_to_api()
{
  good_api_result = false;

  if (lockid == "")
    return send_text("No lock has been set up.  The safe may be opened.");

  if (username == "" )
    return send_text("No username has been set up.  Unable to check lock status");

  String us="";
  String u="";
  if (username.startsWith("u:"))
  {
    us="username";
    u=username.substring(2);
  }
  else if (username.startsWith("d:"))
  {
    us="discordID";
    u=username.substring(2);
  }
  else
    return send_text("Bad username details!");

  HTTPClient https;
  https.useHTTP10(true);

  Serial.println(apiurl);
  if (https.begin(client, apiurl))
  { 
    https.addHeader("ClientID",api_key);
    https.addHeader("ClientSecret",api_secret);
    https.addHeader("Content-Type","application/x-www-form-urlencoded");

    // start connection and send HTTP header
    Serial.println(us + "=" + u + "&lockID=" + lockid);
    int httpCode = https.POST(us + "=" + u + "&lockID=" + lockid);

    if (httpCode != 200)
    {
      https.end();
      return send_text("Problems talking to API: Response " + String(httpCode) + " " + https.errorToString(httpCode));
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
      return send_text("Unable to decode JSON! " + String(error.c_str()));

    Serial.println("JSON is:");
    serializeJsonPretty(raw_data, Serial);
    Serial.println();
 
    if (raw_data["response"]["status"] != 200)
      return send_text((const char*)raw_data["response"]["message"]);

    int l = raw_data["locks"].size();

    if (l == 0)
      return send_text("No lock found for ID " + String(lockid) + "!  Either the lockID is wrong or you deleted it...");

    good_api_result=true;
    String s = "There are " + String(l) + " locks for lock group id " + String(lockid )+ "<br>";

    bool to_unlock=false;
    for (int i=0;i<l;i++)
    {
      if (raw_data["locks"][i]["status"] == "UnlockedReal")
        to_unlock=true;
      s += "Lock " + String(i+1);
      if (raw_data["locks"][i]["lockName"] != "")
      {
        s += " (" + String((const char*)raw_data["locks"][i]["lockName"]) + ")";
      }
      if (raw_data["locks"][i]["lockedBy"] != "")
      {
        s += " locked by " + String((const char*)raw_data["locks"][i]["lockedBy"]);
      }

      s += ": " + String((const char *)raw_data["locks"][i]["status"]) + "<br>";
    }

    if (to_unlock)
    {
      s += "<p>This lock is now available to unlock, and has been removed from the safe";
      lockid = "";
      set_pswd(lockid, lockid_offset);
      state=UNLOCKED;
    }

    https.end();
    return send_text(s);
  }
  else
    return send_text("Could not connect to server");
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
    send_text("Safe can be opened");
  else
    send_text("Lock is still running; safe is locked");
#endif
}

void opensafe()
{
  if (state == LOCKED)
  {
    send_text("Can not open; lock is still running");
  }
  else
  {
    String d = server.arg("duration");
    int del=d.toInt();
    if (del==0) { del=5; }
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(pin, HIGH);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    send_text("Unlocking safe for " + String(del) + " seconds<br>");
    while(del--)
    {
      delay(1000);
      server.sendContent(String(del) + "...\n");
      if (del%10 == 0) server.sendContent("<br>");
    }
    digitalWrite(pin, LOW);
    digitalWrite(LED_BUILTIN, HIGH);
    server.sendContent("Completed");
    server.sendContent("");
  }
}

void display_auth()
{
  String us="";
  String ds="";
  String u="";
  if (username.startsWith("u:"))
  {
    us="selected";
    u=username.substring(2);
  }
  else if (username.startsWith("d:"))
  {
    ds="selected";
    u=username.substring(2);
  }

  String page = change_auth_html;
         page.replace("##apikey##", api_key);
         page.replace("##apisecret##", api_secret);
         page.replace("##usernameselected##", us);
         page.replace("##discordselected##", ds);
         page.replace("##idvalue##", u);
         page.replace("##apiurl##", apiurl);
         page.replace("##ui_username##", ui_username);

  send_text(page);
}

void set_ap()
{
  if (server.hasArg("setwifi"))
  {
     Serial.println("Setting WiFi client");
     safename=server.arg("safename");
     safename.replace(".local","");
     if (safename != "")
     {
       Serial.println("  Setting mDNS name");
       set_pswd(safename,safename_offset);
     }

     if (server.arg("ssid") != "" && server.arg("password") != "")
     {
       Serial.println("  Setting network");
       set_pswd(server.arg("ssid"), ui_wifi_ssid_offset, false);
       set_pswd(server.arg("password"), ui_wifi_pswd_offset);
     }
     send_text("Restarting in 5 seconds");
     delay(5000);
     ESP.restart();
  }
  String page = change_ap_html;
         page.replace("##safename##", safename);
  send_text(page);
}

void set_auth()
{
  Serial.println("Setting Auth details");
  ui_username=server.arg("username");
  if (server.arg("password") != "")
    ui_pswd=server.arg("password");

  set_pswd(ui_username, ui_username_offset,false);
  set_pswd(ui_pswd, ui_pswd_offset);

  send_text("Password reset");
}

void set_api()
{
  Serial.println("Setting API details");
  api_key=server.arg("apikey");
  api_secret=server.arg("apisecret");
  set_pswd(api_key, api_key_offset, false);
  set_pswd(api_secret, api_secret_offset);
  send_text("API details updated");
}

void set_apiurl()
{
  Serial.println("Setting API URL");
  if (state == LOCKED)
    return send_text("Safe is locked - Can not change now");

  String newurl = server.arg("apiurl");
  if (newurl != "" && newurl != apiurl)
  {
    apiurl=newurl;
    set_pswd(apiurl, apiurl_offset);
    send_text("URL updated");
  }
  else
    send_text("No update made");
}

void set_user()
{
  Serial.println("Setting API details");
  username=server.arg("idtype") + ":" + server.arg("idvalue");
  set_pswd(username, username_offset);
  send_text("User details updated");
}

void set_lock()
{
  Serial.println("Setting lock");
  if (state == LOCKED)
    return send_text("Safe is already locked");

  lockid=server.arg("session");
  talk_to_api();
  if (good_api_result)
    set_pswd(lockid, lockid_offset);
  else
    lockid="";

  if (lockid == "")
    state=UNLOCKED;
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
    path="/change_ap.html";
  }

  Serial.println("New client for >>>"+path+"<<<");

  for(int i=0;i<server.args();i++)
  {
    Serial.println("Arg " + String(i) + ": " + server.argName(i) + " --- " + server.arg(i));
  }

  // Ensure username/password have been passed
  if (ui_username != "" && !server.authenticate(ui_username.c_str(), ui_pswd.c_str()))
  {
    Serial.println("Bad authentication; login needed");
    server.requestAuthentication();
    return true;
  }

       if (path == "/")                 { send_text(index_html); }
  else if (path == "/main_frame.html")  { send_text(main_frame_html); }
  else if (path == "/menu_frame.html")  { send_text(menu_frame_html); }
  else if (path == "/top_frame.html")   { send_text(top_frame_html); }
  else if (path == "/change_auth.html") { display_auth(); }
  else if (path == "/change_ap.html")   { set_ap(); }
  else if (path == "/safe/")
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
    Serial.println("File not found");
    return false;
  }
  return true;
}

/////////////////////////////////////////

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("Starting...");

  // Get the EEPROM contents into RAM
  EEPROM.begin(EEPROM_SIZE);

  // Ensure LED is off
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Set the safe state
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  Serial.println("Getting passwords from EEPROM");

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
  if (apiurl == "")
    apiurl = DEFAULT_API;
  safename    = get_pswd(safename_offset);
  if (safename == "")
    safename="safe";

  if (lockid != "")
  { 
    state=LOCKED;
  }

  // This is a debugging line; it's only sent to the serial
  // port which can only be accessed when the safe is unlocked.
  // We don't exposed passwords!
  Serial.println("Found in EEPROM:");
  Serial.println("  UI Username >>>"+ ui_username + "<<<");
  Serial.println("  UI Password >>>"+ ui_pswd + "<<<");
  Serial.println("  Wifi SSID   >>>"+ wifi_ssid + "<<<");
  Serial.println("  Wifi Pswd   >>>"+ wifi_pswd + "<<<");
  Serial.println("  Safe Name   >>>"+ safename + "<<<");
  Serial.println("  API Key     >>>"+ api_key + "<<<");
  Serial.println("  API Secret  >>>"+ api_secret + "<<<");
  Serial.println("  API URL     >>>"+ apiurl + "<<<");
  Serial.println("  Username    >>>"+ username + "<<<");
  Serial.println("  LockID      >>>"+ lockid + "<<<");

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
}

void loop()
{
  MDNS.update();
  server.handleClient();
}
