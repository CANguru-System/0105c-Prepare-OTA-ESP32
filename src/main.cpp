
#include <Arduino.h>
#include "Preferences.h"
#include "CANguruDefs.h"
#include <WebServer.h>
#include <WiFi.h>
#include "esp_timer.h"
#include "ticker.h"
#include <ArduinoOTA.h>
#include "OTA_include.h"
#include <ElegantOTA.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

Preferences preferences;

// Create AsyncWebServer object on port 80
WebServer server(80);

const uint8_t LED_BUILTIN_OWN = LED_BUILTIN;

Ticker tckr;
const float tckrTime = 0.01;

enum blinkStatus
{
  blinkFast = 0, // wartet auf Password
  blinkSlow,     // wartet auf WiFi
  blinkNo        // mit WiFi verbunden
};
blinkStatus blink;

const uint8_t veryBright = 0xFF;
const uint8_t bright = veryBright / 2;
const uint8_t smallBright = bright / 2;
const uint8_t dark = 0x00;

uint8_t setup_todo;

String liesEingabe()
{
  boolean newData = false;
  char receivedChars[numChars]; // das Array für die empfangenen Daten
  static byte ndx = 0;
  char endMarker = '\r';
  char rc;

  while (newData == false)
  {
    while (Serial.available() > 0)
    {
      rc = Serial.read();

      if (rc != endMarker)
      {
        receivedChars[ndx] = rc;
        Serial.print(rc);
        ndx++;
        if (ndx >= numChars)
        {
          ndx = numChars - 1;
        }
      }
      else
      {
        receivedChars[ndx] = '\0'; // Beendet den String
        log_i();
        ndx = 0;
        newData = true;
      }
    }
  }
  return receivedChars;
}

String netzwerkScan()
{
  // Zunächst Station Mode und Trennung von einem AccessPoint, falls dort eine Verbindung bestand
  //  WiFi.mode(WIFI_STA);
  //  WiFi.disconnect();
  delay(100);

  log_i("Scan-Vorgang gestartet");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  log_i("Scan-Vorgang beendet");
  if (n == 0)
  {
    log_i("Keine Netzwerke gefunden!");
  }
  else
  {
    log_i("%d Netzwerke gefunden", n);
    for (int i = 0; i < n; ++i)
    {
      // Drucke SSID and RSSI für jedes gefundene Netzwerk
      log_i("%d: %s (%d)", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      delay(10);
    }
  }
  uint8_t number;
  do
  {
    log_i("Bitte Netzwerk auswaehlen: ");
    String no = liesEingabe();
    number = uint8_t(no[0]) - uint8_t('0');
  } while ((number > n) || (number == 0));

  return WiFi.SSID(number - 1);
}

// der Timer steuert das Scannen der Slaves, das Blinken der LED
// sowie das Absenden des PING
void timer1s()
{
  static uint8_t secs = 0;
  static uint8_t slices = 0;
  slices++;
  switch (blink)
  {
  case blinkFast:
    if (slices >= 10)
    {
      slices = 0;
      secs++;
    }
    break;
  case blinkSlow:
    if (slices >= 40)
    {
      slices = 0;
      secs++;
    }
    break;
  case blinkNo:
    secs = 2;
    break;
  }
  if (secs % 2 == 0)
    // turn the LED on by making the voltage HIGH
    digitalWrite(BUILTIN_LED, HIGH);
  else
    // turn the LED off by making the voltage LOW
    digitalWrite(BUILTIN_LED, LOW);
}

void eraseEEPROM()
{
  preferences.clear();
  // setup in der Anwndung erzwingen
  setup_todo = setup_NOT_done;
  log_i("EEPROM erfolgreich geloescht!");
}

void setup()
{
  Serial.begin(bdrMonitor);
  delay(500);
  log_i("\r\n\rS t a r t   OTA");
  log_i("Variante ESP32");

  log_i("\r\nZeigt die eigene IP-Adresse");
  log_i("und bereitet OTA vor\r\n");

  log_i("\n on %s", ARDUINO_BOARD);
  log_i("CPU Frequency = %d Mhz", F_CPU / 1000000);

  if (preferences.begin("PREPARE_OTA", false))
  {
    log_i("Preferences erfolgreich gestartet");
  }
  tckr.attach(tckrTime, timer1s); // each sec
  blink = blinkFast;
  setup_todo = preferences.getUChar("setup_done", 0xFF);
  String answer;
  do
  {
    log_i("EEPROM loeschen (J/N)?: ");
    answer = liesEingabe();
    answer.toUpperCase();
  } while ((answer != "J") && (answer != "N"));
  if (answer == "J")
    eraseEEPROM();
  else
  {
    answer = "";
    do
    {
      log_i("Neue Netzwerkdaten (J/N)?: ");
      answer = liesEingabe();
      answer.toUpperCase();
    } while ((answer != "J") && (answer != "N"));
  }
  String ssid = "";
  String password = "";
  if (answer == "J")
  {
    // alles fürs erste Mal
    //
    ssid = netzwerkScan();
    preferences.putString("ssid", ssid);
    log_i();
    // liest das password ein
    log_i("Bitte das Passwort eingeben (Falls Sie sich dabei vertippen, muessen Sie den Prozess neu starten!!): ");
    password = liesEingabe();
    preferences.putString("password", password);
    log_i();
  }
  else
  {
    blink = blinkSlow;
    ssid = preferences.getString("ssid", "No SSID");
    password = preferences.getString("password", "No password");
  }
  char ssidCh[ssid.length() + 1];
  ssid.toCharArray(ssidCh, ssid.length() + 1);
  char passwordCh[password.length() + 1];
  password.toCharArray(passwordCh, password.length() + 1);

  // Connect to Wi-Fi network with SSID and password
  log_i("Verbinde mit dem Netzwerk: %s - Mit dem Passwort: %s", ssidCh, passwordCh);
  WiFi.begin(ssidCh, passwordCh);
  uint8_t trials = 0;
  blink = blinkSlow;
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    delay(2000);
    log_i(".");
    trials++;
    if (trials > 5)
    {
      // zuviele Versuche für diese Runde
      setup_todo = setup_NOT_done;
      preferences.putUChar("setup_done", setup_todo);
      ESP.restart();
    }
  }
  // WLAN hat funktioniert
  blink = blinkNo;
  // setup_done setzen
  preferences.putUChar("setup_done", setup_todo);
  // Print local IP address and start web server
  log_i();
  IPAddress IP = WiFi.localIP();
  char ip[4]; // prepare a buffer for the data
  for (uint8_t i = 0; i < 4; i++)
    ip[i] = IP[i];
  preferences.putBytes("IP0", ip, 4);
  log_i("Eigene IP-Adresse: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  tckr.detach();

  // Route for root / web page
  server.on("/", []()
            { server.send(200, "text/plain", "\r\n\rBitte geben Sie ein:\r\n\r'IP-adresse des Decoders'/update"); });

  // Start ElegantOTA
  ElegantOTA.begin(&server);
  // Start server
  server.begin();
  log_i("HTTP server started");
  log_i("\r\n\rBitte starten Sie nun den Browser auf Ihrem PC und geben Sie in der Adresszeile ein:\r\n\r");
  log_i("%d.%d.%d.%d/update", IP[0], IP[1], IP[2], IP[3]);
}

void loop()
{
   server.handleClient();
}