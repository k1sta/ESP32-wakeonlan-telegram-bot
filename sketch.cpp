#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WakeOnLan.h>
#include <ESP32Ping.h>
#include <ESPmDNS.h>
#include <lwip/etharp.h>
#include <LittleFS.h>

//--- bot cred and master user uid ---
#define BOT_TOKEN "TELEGRAM_BOT_TOKEN"
#define AUTHORIZED_UID "TELEGRAM_USER_ID"

//--- hosts save file ---
#define HOSTS_FILE "/hosts.json"

// --- max hosts ---
#define MAX_HOSTS 10

//--- network creds ---
const char *ssid = "WIFI_SSID";
const char *password = "WIFI_PASSWORD";

//--- global objects ---
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
WiFiUDP udp;
WakeOnLan wol(udp);

//--- host struct ---
struct Host
{
    String name;
    String ip;
    String mac;
};

// --- global host variables ---
Host hosts[MAX_HOSTS];
int hostCount = 0;

void setup()
{
    Serial.begin(115200);

    // initialize LittleFS]
    if (!LittleFS.begin(true))
    {
        Serial.println(F("Falha em dar mount no LittleFS."));
        return;
    }


    // connecting to wifi and setting up WOL
    WiFi.begin(ssid, password);
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // add root certificate for telegram
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println(F("Connecting to WiFi..."));
    }
    Serial.println(F("WiFi connected"));
    wol.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

    // set bot commands
    bot.setMyCommands({
        {"/start", "Inicia o bot"},
        {"/list", "Lista todos os servidores registrados"},
        {"/add", "Adiciona um novo servidor"},
        {"/remove", "Remove um servidor"},
        {"/wake", "Acorda um servidor"}
    });

    // load hosts from file
    loadHosts(); 
}

void loop()
{
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    for (int i = 0; i < numNewMessages; i++)
    {
        handleMessage(bot.messages[i]);
    }
}

void handleMessage(telegramMessage &message)
{
    if (message.from_id != AUTHORIZED_UID)
    {
        return;
    } // only for me ;)

    if (message.text == "/start")
    {
        String welcome = F("Bem vindo ao seu HomeLab!\n\n");
        welcome += F("/list - Lista todos os servidores registrados\n");
        welcome += F("/add <nome> <ip> [mac] - Adiciona um novo servidor\n");
        welcome += F("/remove <nome> - Remove um servidor\n");
        welcome += F("/wake <nome> - Acorda um servidor\n");
        bot.sendMessage(message.chat_id, welcome, "Markdown");
    }
    else if (message.text.startsWith(F("/add ")))
    {
        addHostCommand(message);
    }
    else if (message.text.startsWith(F("/remove ")))
    {
        removeHostCommand(message);
    }
    else if (message.text == F("/list"))
    {
        listHostsCommand(message.chat_id);
    }
    else if (message.text.startsWith(F("/wake ")))
    {
        wakeHostCommand(message);
    }
}

void addHostCommand(telegramMessage &message)
{
    String text = message.text;
    text.replace(F("/add "), "");

    char *str = strdup(text.c_str());
    char *name = strtok(str, " ");
    char *ip = strtok(NULL, " ");
    char *mac = strtok(NULL, " ");

    if (name && ip)
    {
        String macStr;
        // se mac nao inserido (geralmente o caso)
        if (!mac)
        {
            ip4_addr_t ip_addr;
            if (ip4addr_aton(ip, &ip_addr))
            {
                struct eth_addr *eth_ret;
                struct netif *netif_ret;
                if (etharp_find_addr(NULL, &ip_addr, &eth_ret, &netif_ret) >= 0 && eth_ret)
                {
                    char macBuf[18];
                    sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X",
                            eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                            eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
                    macStr = String(macBuf);
                }
            }
            if (macStr.length() == 0)
            {
                bot.sendMessage(message.chat_id, F("Endereço MAC não fornecido e não pôde ser encontrado na LAN. Por favor, especifique o MAC."));
                free(str);
                return;
            }
        }
        else
        {
            macStr = String(mac);
        }

        // check if the IP is valid
        ip4_addr_t ip_addr;
        if (!ip4addr_aton(ip, &ip_addr))
        {
            bot.sendMessage(message.chat_id, F("Endereço IP inválido."));
            free(str);
            return;
        }

        // check if the MAC is valid
        if (!macStr.matches("([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"))
        {
            bot.sendMessage(message.chat_id, F("Endereço MAC inválido."));
            free(str);
            return;
        }

        // check if the hostname/ip/mac already exists
        String nameStr = String(name);
        for (int i = 0; i < hostCount; i++)
        {
            if (hosts[i].name.equalsIgnoreCase(nameStr))
            {
                bot.sendMessage(message.chat_id, F("Host com esse nome já existe."));
                free(str);
                return;
            }

            if (hosts[i].ip.equalsIgnoreCase(ip))
            {
                bot.sendMessage(message.chat_id, F("Host com esse IP já existe."));
                free(str);
                return;
            }

            if (hosts[i].mac.equalsIgnoreCase(macStr))
            {
                bot.sendMessage(message.chat_id, F("Host com esse MAC já existe."));
                free(str);
                return;
            }
        }

        // add the host
        if (addHost(name, ip, macStr))
        {
            bot.sendMessage(message.chat_id, F("Host adicionado com sucesso. MAC: ") + macStr);
        }
        else
        {
            bot.sendMessage(message.chat_id, F("Não foi possível adicionar o host. A lista está cheia."));
        }
    }
    else
    {
        bot.sendMessage(message.chat_id, F("Formato inválido. Use: /add <nome> <ip> [mac]"));
    }

    // cleanup the mess
    free(str);

    // serial debug
    Serial.println(F("Request to add host: ") + String(name) + F(", IP: ") + String(ip) + F(", MAC: ") + macStr);
    Serial.print(F("Total hosts: "));
    Serial.println(hostCount);
}

void removeHostCommand(telegramMessage &message)
{
    String nameToRemove = message.text;
    nameToRemove.replace(F("/remove "), "");

    // check if the name exists on the list
    for (int i = 0; i < hostCount; i++)
    {
        if (nameToRemove.equalsIgnoreCase(hosts[i].name))
        {
            for (int j = i; j < hostCount - 1; j++)
            {
                hosts[j] = hosts[j + 1];
            }
            hostCount--;
            bot.sendMessage(message.chat_id, F("Host '") + nameToRemove + F("' removed."));
            return;
        }
    }
    bot.sendMessage(message.chat_id, F("Host not found."));
}

void listHostsCommand(String chat_id)
{   
    // empty list check
    if (hostCount == 0)
    {
        bot.sendMessage(chat_id, F("Nenhum host registrado."));
        return;
    }

    // list all hosts
    String list = F("*Servidores registrados:*\n");
    for (int i = 0; i < hostCount; i++)
    {   
        // send 1 icmp ping request to each host to check if its up
        bool isUp = Ping.ping(hosts[i].ip.c_str(), 1);
        String status = (isUp) ? F("✅") : F("❌");
        list += hosts[i].name + F(" (") + hosts[i].ip + F(") - ") + status + F("\n");
    }
    bot.sendMessage(chat_id, list, "Markdown");
}

void wakeHostCommand(telegramMessage &message)
{
    String nameToWake = message.text;
    nameToWake.replace(F("/wake "), "");

    for (int i = 0; i < hostCount; i++)
    {
        if (hosts[i].name == nameToWake)
        {
            wol.sendMagicPacket(hosts[i].mac.c_str());
            bot.sendMessage(message.chat_id, F("Pacote WOL enviado para ") + hosts[i].name);
            return;
        }
    }
    bot.sendMessage(message.chat_id, F("Host não encontrado."));
}

bool addHost(String name, String ip, String mac)
{
    if (hostCount >= MAX_HOSTS)
    {
        return false;
    }
    hosts[hostCount].name = name;
    hosts[hostCount].ip = ip;
    hosts[hostCount].mac = mac;
    hostCount++;
    return true;
}

void saveHosts() {
  // create a JSON document
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  // add each host to the JSON array
  for (int i = 0; i < hostCount; i++) {
    JsonObject obj = array.add<JsonObject>();
    obj["name"] = hosts[i].name;
    obj["ip"] = hosts[i].ip;
    obj["mac"] = hosts[i].mac;
  }

  // lets write it down!
  File file = LittleFS.open(HOSTS_FILE, "w");
  if (!file) {
    Serial.println(F("Failed to open file for writing"));
    return;
  }

  // serialize the JSON document to the file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  } else {
    Serial.println(F("Hosts saved to LittleFS."));
  }
  
  file.close();
}

void loadHosts() {
  // lets read a file!
  File file = LittleFS.open(HOSTS_FILE, "r");
  if (!file) {
    Serial.println(F("Arquivo não encontrado, usando configuração padrão (vazia)."));
    return;
  }

  // deserialize the JSON document
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Falha em ler o arquivo JSON, usando configuração padrão (vazia)."));
    return;
  }

  // create the hosts array from the JSON document
  JsonArray array = doc.as<JsonArray>();
  hostCount = 0; // reset host count (just in case)
  for (JsonObject obj : array) {
    if (hostCount < MAX_HOSTS) {
      hosts[hostCount].name = obj["name"].as<String>();
      hosts[hostCount].ip = obj["ip"].as<String>();
      hosts[hostCount].mac = obj["mac"].as<String>();
      hostCount++;
    }
  }

  // print the loaded hosts for debugging
  Serial.print(hostCount);
  Serial.println(F(" hosts carregados do backup."));
  file.close();
}