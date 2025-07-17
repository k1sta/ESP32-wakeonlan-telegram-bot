#include <WiFi.h>
#include <CTBot.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <lwip/etharp.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <WakeOnLan.h>

//--- bot cred and master user uid ---
#define BOT_TOKEN "token"
#define MASTER_UID "uid"

// --- max hosts ---
#define MAX_HOSTS 10

//--- network creds ---
const char *ssid = "ssid";
const char *password = "pass";

//--- global objects ---
CTBot bot;
WiFiUDP udp;
WakeOnLan WOL(udp);

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
    Serial.printf("Booting...\n");  

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

    WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

    // CTBot setup
    bot.setTelegramToken(BOT_TOKEN);

    loadHosts();
}

// --- Telegram polling interval (ms) ---
const unsigned long TELEGRAM_POLL_INTERVAL = 500; 
unsigned long lastPollTime = 0;

void loop()
{
    static TBMessage msg;
    unsigned long now = millis();
    if (now - lastPollTime >= TELEGRAM_POLL_INTERVAL) {
        lastPollTime = now;
        if (bot.getNewMessage(msg)) {
            handleMessage(msg);
        }
    }
    
    delay(1);
}

void handleMessage(TBMessage &message)
{
    if (String(message.sender.id) != String(MASTER_UID)) // only for me ;)
        return;

    Serial.printf("Received message from %lld: %s\n", message.sender.id, message.text.c_str());
    String text = message.text;
    if (text == "/start")
    {
        String welcome = F("Bem vindo ao seu HomeLab!\n\n/list - Lista todos os servidores registrados\n/add <nome> <ip> [mac] - Adiciona um novo servidor\n/remove <nome> - Remove um servidor\n/wake <nome> - Acorda um servidor\n/wakeall - Acorda todos os servidores\n");
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
    else if (text.startsWith(F("/wakeall")))
    {
        wakeAllHostsCommand(message);
    }
    else
    {
        bot.sendMessage(message.sender.id, F("Comando desconhecido. Use /start para ver os comandos disponíveis."));
    }
}


void addHostCommand(TBMessage &message)
{
    String input = message.text;
    input.replace(F("/add "), "");
    int spaceIndex = input.indexOf(' ');
    if (spaceIndex == -1) {
        bot.sendMessage(message.sender.id, F("Comando inválido. Use: /add <nome> <ip> [mac]"));
        return;
    }

    String name = input.substring(0, spaceIndex);
    name.trim();
    input.remove(0, spaceIndex + 1);
    spaceIndex = input.indexOf(' ');
    String ip = (spaceIndex == -1) ? input : input.substring(0, spaceIndex);
    ip.trim();
    String mac = (spaceIndex == -1) ? "" : input.substring(spaceIndex + 1);
    mac.trim();

    if (ip.isEmpty() || name.isEmpty()) {
        bot.sendMessage(message.sender.id, F("Comando inválido. Use: /add <nome> <ip> [mac]"));
        return;
    }

    if (mac.isEmpty()) {
        IPAddress targetIp;
        if (!targetIp.fromString(ip)) {
            bot.sendMessage(message.sender.id, F("IP inválido."));
            return;
        }
        mac = resolveMacAddress(targetIp);
        if (mac.isEmpty()) {
            bot.sendMessage(message.sender.id, F("Não foi possível resolver o endereço MAC."));
            return;
        }
    }

    if (addHost(name, ip, mac)) {
        saveHosts();
        bot.sendMessage(message.sender.id, "Servidor adicionado: " + name + " (" + ip + ") [" + mac + "]");
    } else {
        bot.sendMessage(message.sender.id, "Limite de servidores atingido.");
    }
}

bool addHost(String name, String ip, String mac)
{
    if (hostCount < MAX_HOSTS) {
        hosts[hostCount].name = name;
        hosts[hostCount].ip = ip;
        hosts[hostCount].mac = mac;
        hostCount++;
        return true;
    }
    return false;
}

String resolveMacAddress(IPAddress ip)
{
    // Try to resolve MAC address for a given IP using ARP table (safe workaround)
    Serial.printf("Resolving MAC for IP: %s\n", ip.toString().c_str());

    // Send a dummy UDP packet to trigger ARP resolution
    WiFiUDP udp;
    udp.beginPacket(ip, 9); // Port 9 is the discard port (WOL uses it too)
    udp.write(0); // Send a single null byte
    udp.endPacket();
    
    // Wait for ARP table to update (reduced for speed)
    delay(1000); 

    struct netif *netif = netif_list;
    if (!netif) return "";
    ip4_addr_t target_ip;
    target_ip.addr = (uint32_t)ip;
    struct eth_addr *mac_addr = nullptr;
    const ip4_addr_t *out_ip = nullptr;
    if (etharp_find_addr(netif, &target_ip, &mac_addr, &out_ip) >= 0 && mac_addr) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr->addr[0], mac_addr->addr[1], mac_addr->addr[2],
                 mac_addr->addr[3], mac_addr->addr[4], mac_addr->addr[5]);
        return String(macStr);
    }
    return "";
}

void listHostsCommand(TBMessage &message)
{
    String hostList = F("Servidores registrados:\n");
    for (int i = 0; i < hostCount; i++) {
        hostList += String(i + 1) + ". " + hosts[i].name + " (" + hosts[i].ip + ") [" + hosts[i].mac + "]\n";
    }
    bot.sendMessage(message.sender.id, hostList);

    if (hostCount == 0) {
        bot.sendMessage(message.sender.id, F("Nenhum servidor registrado."));
        return;
    }
}

void removeHostCommand(TBMessage &message)
{
    String input = message.text;
    input.replace(F("/remove "), "");
    input.trim();

    if (input.isEmpty()) {
        bot.sendMessage(message.sender.id, F("Comando inválido. Use: /remove <nome>"));
        return;
    }

    String name = input;

    if (removeHost(name)) {
        bot.sendMessage(message.sender.id, "Servidor removido: " + name);
    } else {
        bot.sendMessage(message.sender.id, "Servidor não encontrado: " + name);
    }
}

bool removeHost(String name)
{
    for (int i = 0; i < hostCount; i++) {
        if (hosts[i].name.equalsIgnoreCase(name)) {
            for (int j = i; j < hostCount - 1; j++) {
                hosts[j] = hosts[j + 1];
            }
            hostCount--;
            return true;
        }
    }
    return false;
} 

void wakeHostCommand(TBMessage &message)
{
    String input = message.text;
    input.replace(F("/wake "), "");
    input.trim();

    if (input.isEmpty()) {
        bot.sendMessage(message.sender.id, F("Comando inválido. Use: /wake <nome>"));
        return;
    }

    String name = input;

    if (wakeHost(name)) {
        bot.sendMessage(message.sender.id, "Servidor despertado: " + name);
    } else {
        bot.sendMessage(message.sender.id, "Servidor não encontrado: " + name);
    }
}

bool wakeHost(String name)
{

    for (int i = 0; i < hostCount; i++) {
        if (hosts[i].name.equalsIgnoreCase(name)) {
            WOL.sendMagicPacket(hosts[i].mac);
            return true;
        }
    }
    
    return false;
}

void wakeAllHostsCommand(TBMessage &message)
{
    if (hostCount == 0) {
        bot.sendMessage(message.sender.id, F("Nenhum servidor registrado."));
        return;
    }

    for (int i = 0; i < hostCount; i++) {
        WOL.sendMagicPacket(hosts[i].mac);
        delay(100); // Delay to avoid flooding the network
    }
    
    bot.sendMessage(message.sender.id, F("Todos os servidores foram despertados."));
}

void loadHosts()
{
    File file = LittleFS.open("/hosts.json", "r");
    if (!file) {
        Serial.println(F("Failed to open hosts.json for reading."));
        return;
    }

    String content = file.readString();
    file.close();

    // ArduinoJson v5
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& doc = jsonBuffer.parseObject(content);
    if (!doc.success()) {
        Serial.println(F("Failed to parse hosts.json."));
        return;
    }

    hostCount = doc["hostCount"];
    if (hostCount > MAX_HOSTS) {
        hostCount = MAX_HOSTS;
    }

    JsonArray& hostsArray = doc["hosts"];
    for (int i = 0; i < hostCount; i++) {
        hosts[i].name = String((const char*)hostsArray[i]["name"]);
        hosts[i].ip = String((const char*)hostsArray[i]["ip"]);
        hosts[i].mac = String((const char*)hostsArray[i]["mac"]);
    }
}

void saveHosts()
{
    // ArduinoJson v5
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& doc = jsonBuffer.createObject();
    doc["hostCount"] = hostCount;

    JsonArray& hostsArray = doc.createNestedArray("hosts");
    for (int i = 0; i < hostCount; i++) {
        JsonObject& hostObj = hostsArray.createNestedObject();
        hostObj["name"] = hosts[i].name;
        hostObj["ip"] = hosts[i].ip;
        hostObj["mac"] = hosts[i].mac;
    }

    File file = LittleFS.open("/hosts.json", "w");
    if (!file) {
        Serial.println(F("Failed to open hosts.json for writing."));
        return;
    }

    doc.printTo(file);
    file.close();
}