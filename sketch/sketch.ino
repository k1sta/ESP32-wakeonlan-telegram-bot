#include <WiFi.h>
#include <CTBot.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

//--- bot cred and master user uid ---
#define BOT_TOKEN "TELEGRAM_BOT_TOKEN"
#define MASTER_UID "TELEGRAM_USER_ID"

//--- network creds ---
const char *ssid = "WIFI_SSID";
const char *password = "WIFI_PASSWORD";

//--- global objects ---
CTBot bot;
WiFiUDP udp;

void setup()
{
    Serial.begin(115200);

    // initialize LittleFS]
    if (!LittleFS.begin(true))
    {
        Serial.println(F("Failed on mounting LittleFS."));
        return;
    }

    // connecting to wifi and setting up WOL
    if (!bot.wifiConnect(ssid, password)) {
        Serial.println(F("WiFi connection failed!"));
        return;
    }
    Serial.println(F("WiFi connected via CTBot"));

    // CTBot setup
    bot.setTelegramToken(BOT_TOKEN);
}

void loop()
{
    TBMessage msg;
    if (bot.getNewMessage(msg)) {
        handleMessage(msg);
    }
}

void handleMessage(TBMessage &message)
{
    if (String(message.sender.id) != String(MASTER_UID)) // only for me ;)
        return;

    String text = message.text;
    if (text == "/start")
    {
        String welcome = F("Bem vindo ao seu HomeLab!\n\n");
        welcome += F("/list - Lista todos os servidores registrados\n");
        welcome += F("/add <nome> <ip> [mac] - Adiciona um novo servidor\n");
        welcome += F("/remove <nome> - Remove um servidor\n");
        welcome += F("/wake <nome> - Acorda um servidor\n");
        bot.sendMessage(message.sender.id, welcome);
    }
    else if (text.startsWith(F("/add ")))
    {
        // addHostCommand(message);
    }
    else if (text.startsWith(F("/remove ")))
    {
        // removeHostCommand(message);
    }
    else if (text == F("/list"))
    {
        // listHostsCommand(message);
    }
    else if (text.startsWith(F("/wake ")))
    {
        // wakeHostCommand(message);
    }
}