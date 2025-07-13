#include <WiFi.h>
#include <CTBot.h>
#include <ArduinoJson.h>
#include <WakeOnLan.h>
#include <ESPping.h>
#include <ESPmDNS.h>
#include <lwip/etharp.h>
#include <LittleFS.h>

// --- function prototypes ---
void setup();
void loop();
void handleMessage(TBMessage &message);
void addHostCommand(TBMessage &message);
int findHost(String name, String ip, String mac);
void removeHostCommand(TBMessage &message);
void listHostsCommand(TBMessage &message);
void wakeHostCommand(TBMessage &message);
bool addHost(String name, String ip, String mac);
void saveHosts();
void loadHosts();
String resolveMacAddress(IPAddress targetIp);

//--- bot cred and master user uid ---
#define BOT_TOKEN "TELEGRAM_BOT_TOKEN"
#define MASTER_UID "TELEGRAM_USER_ID"

//--- hosts save file ---
#define HOSTS_FILE "/hosts.json"

// --- max hosts ---
#define MAX_HOSTS 10

//--- network creds ---
const char *ssid = "WIFI_SSID";
const char *password = "WIFI_PASSWORD";

//--- global objects ---
CTBot bot;
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
        Serial.println(F("Failed on mounting LittleFS."));
        return;
    }

    // connecting to wifi and setting up WOL
    if (!bot.wifiConnect(ssid, password)) {
        Serial.println(F("WiFi connection failed!"));
        return;
    }
    Serial.println(F("WiFi connected via CTBot"));
    wol.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

    // CTBot setup
    bot.setTelegramToken(BOT_TOKEN);

    // load hosts from file
    loadHosts(); 
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
    // CTBot: message.sender.id is int64_t, MASTER_UID should be int64_t or string convertible
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
        addHostCommand(message);
    }
    else if (text.startsWith(F("/remove ")))
    {
        removeHostCommand(message);
    }
    else if (text == F("/list"))
    {
        listHostsCommand(message);
    }
    else if (text.startsWith(F("/wake ")))
    {
        wakeHostCommand(message);
    }
}

void addHostCommand(TBMessage &message)
{
    String text = message.text;
    text.replace(F("/add "), "");

    // Split the input string into name, ip, and optional mac
    // Using simple String functions for robustness
    int firstSpace = text.indexOf(' ');
    if (firstSpace == -1) {
        bot.sendMessage(message.sender.id, F("Parâmetros insuficientes. Use /add <nome> <ip> [mac]"));
        return;
    }
    String name = text.substring(0, firstSpace);
    String remaining = text.substring(firstSpace + 1);

    int secondSpace = remaining.indexOf(' ');
    String ipStr;
    String macStr = ""; // Default to empty

    if (secondSpace == -1) {
        // Only name and IP provided
        ipStr = remaining;
    } else {
        // Name, IP, and MAC provided
        ipStr = remaining.substring(0, secondSpace);
        macStr = remaining.substring(secondSpace + 1);
    }

    // Check if the IP is valid
    IPAddress ip_addr;
    if (!ip_addr.fromString(ipStr)) {
        bot.sendMessage(message.sender.id, F("Endereço IP inválido."));
        return;
    }

    // If MAC is not provided, try to resolve it
    if (macStr.length() == 0) {
        bot.sendMessage(message.sender.id, F("Tentando resolver endereço MAC para ") + ipStr + F("... Isso pode levar alguns segundos."));
        // This is the blocking call for ARP resolution.
        // In a real-world scenario with a more complex bot,
        // you might want to offload this to a separate task
        // or provide immediate feedback and update later.
        macStr = resolveMacAddress(ip_addr);
        if (macStr.length() == 0) {
            bot.sendMessage(message.sender.id, F("Não foi possível resolver o endereço MAC para ") + ipStr + F(". Por favor, forneça-o manualmente no formato AA:BB:CC:DD:EE:FF."));
            return;
        } else {
            bot.sendMessage(message.sender.id, F("Endereço MAC resolvido: ") + macStr);
        }
    } else {
        // If MAC is provided, validate it
        if (!macStr.matches("([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}")) {
            bot.sendMessage(message.sender.id, F("Endereço MAC inválido. Formato esperado: AA:BB:CC:DD:EE:FF"));
            return;
        }
    }

    // check if the hostname/ip/mac already exists
    int exists = findHost(name, ipStr, macStr);
    if (exists == 1) {
        bot.sendMessage(message.sender.id, F("Host com esse nome já existe."));
        return;
    } else if (exists == 2) {
        bot.sendMessage(message.sender.id, F("Host com esse IP já existe."));
        return;
    } else if (exists == 3) {
        bot.sendMessage(message.sender.id, F("Host com esse MAC já existe."));
        return;
    }

    // add the host
    if (addHost(name, ipStr, macStr)) {
        bot.sendMessage(message.sender.id, F("Host adicionado com sucesso."));
    } else {
        bot.sendMessage(message.sender.id, F("Não foi possível adicionar o host. A lista está cheia."));
    }

    // serial debug
    Serial.println(F("Request to add host: ") + name + F(", IP: ") + ipStr + F(", MAC: ") + macStr);
    Serial.print(F("Total hosts: "));
    Serial.println(hostCount);

    // save the hosts to file
    saveHosts();
    Serial.println(F("Hosts saved to LittleFS."));
}

// Find host by name, ip, or mac. Returns 1 if name exists, 2 if ip exists, 3 if mac exists, 0 if not found
int findHost(String name, String ip, String mac) {
    for (int i = 0; i < hostCount; i++) {
        if (hosts[i].name.equalsIgnoreCase(name)) return 1;
        if (hosts[i].ip == ip) return 2;
        if (hosts[i].mac.equalsIgnoreCase(mac)) return 3;
    }
    return 0;
}

void removeHostCommand(TBMessage &message)
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
            bot.sendMessage(message.sender.id, F("Host '") + nameToRemove + F("' removido."));
            
            // save the hosts to file
            saveHosts();
            Serial.println(F("Hosts saved to LittleFS."));

            return;
        }
    }
    bot.sendMessage(message.sender.id, F("Host não encontrado."));
}

void listHostsCommand(TBMessage &message)
{ 
    // empty list check
    if (hostCount == 0)
    {
        bot.sendMessage(message.sender.id, F("Nenhum host registrado."));
        return;
    }

    // prompt loading message
    String loadingMsg = F("Carregando status dos servidores, aguarde...");
    int loadingMsgId = bot.sendMessage(message.sender.id, loadingMsg);


    String list = F("*Servidores registrados:*\n");
    for (int i = 0; i < hostCount; i++)
    {   
        // send 1 icmp ping request to each host to check if its up
        bool isUp = Ping.ping(hosts[i].ip.c_str(), 1);
        String status = (isUp) ? F("✅") : F("❌");
        list += hosts[i].name + F(" (") + hosts[i].ip + F(") - ") + status + F("\n");
    }
    // if the edit fails, send a new message
    if (!(bot.editMessageText(message.sender.id, loadingMsgId, list, "Markdown"))) {
        bot.sendMessage(message.sender.id, list, "Markdown");
    }
}

void wakeHostCommand(TBMessage &message)
{
    String nameToWake = message.text;
    nameToWake.replace(F("/wake "), "");

    for (int i = 0; i < hostCount; i++)
    {
        if (hosts[i].name == nameToWake)
        {
            wol.sendMagicPacket(hosts[i].mac.c_str());
            bot.sendMessage(message.sender.id, F("Pacote WOL enviado para ") + hosts[i].name);
            return;
        }
    }
    bot.sendMessage(message.sender.id, F("Host não encontrado."));
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
    Serial.println(F("File not found, using default (empty) configuration."));
    return;
  }

  // deserialize the JSON document
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to parse JSON file, using default (empty) configuration."));
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
  Serial.println(F(" hosts loaded from backup."));
  file.close();
}

String resolveMacAddress(IPAddress targetIp) {
    struct eth_addr *eth_ret = nullptr;
    ip4_addr_t ip_addr_lwip;
    ip_addr_lwip.addr = targetIp; // Convert IPAddress to lwIP ip4_addr_t

    // Try to find in ARP cache first (might be there from previous communication)
    if (etharp_find_addr(netif_list, (const ip4_addr_t *)&ip_addr_lwip, (struct eth_addr **)&eth_ret, NULL) >= 0 && eth_ret) {
        char macBuf[18];
        sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X",
                eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
        return String(macBuf);
    }

    // If not in cache, send an ARP request
    Serial.println(F("MAC not in cache, sending ARP request..."));
    // Ensure the netif is valid. For WiFi STA, it's usually netif_list
    err_t err = etharp_request(netif_list, &ip_addr_lwip); 
    if (err != ERR_OK) {
        Serial.printf("Failed to send ARP request, error: %d\n", err);
        return "";
    }

    // Give it some time for the ARP reply to come back and update the cache
    // This is a blocking delay, which is okay for this specific operation
    // where we need the MAC immediately for WOL.
    for (int i = 0; i < 5; i++) { // Try up to 5 times with a delay
        delay(500); // Wait 500ms
        // Try to find in ARP cache again after delay
        if (etharp_find_addr(netif_list, (const ip4_addr_t *)&ip_addr_lwip, (struct eth_addr **)&eth_ret, NULL) >= 0 && eth_ret) {
            char macBuf[18];
            sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X",
                    eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                    eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
            return String(macBuf);
        }
    }
    
    Serial.println(F("Failed to resolve MAC address after ARP request."));
    return ""; // Return empty string if not found
}