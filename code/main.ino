#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

static const char* DEFAULT_WIFI_SSID = "";
static const char* DEFAULT_WIFI_PASS = "";
static const char* DEFAULT_SERVER_HOST = "your-server-ip";
static const uint16_t DEFAULT_SERVER_PORT = 7000;
static const char* DEFAULT_TOKEN = "SuperSecretPassword123";
static const char* DEFAULT_PROXY_NAME = "esp32_tcp";
static const char* DEFAULT_LOCAL_HOST = "192.168.1.1";
static const uint16_t DEFAULT_LOCAL_PORT = 80;
static const uint16_t DEFAULT_REMOTE_PORT = 8080;
static const uint16_t DEFAULT_CONFIG_WEB_PORT = 8088;
static const uint8_t CONFIG_MODE_PIN = 4;
static const bool CONFIG_MODE_ACTIVE_HIGH = true;
static const size_t MAX_PROXIES = 6;

static const char TYPE_LOGIN = 'o';
static const char TYPE_LOGIN_RESP = '1';
static const char TYPE_NEW_PROXY = 'p';
static const char TYPE_NEW_PROXY_RESP = '2';
static const char TYPE_NEW_WORK_CONN = 'w';
static const char TYPE_REQ_WORK_CONN = 'r';
static const char TYPE_START_WORK_CONN = 's';

struct ProxyConfig {
  bool enabled;
  String name;
  String localHost;
  uint16_t localPort;
  uint16_t remotePort;
};

struct TunnelConfig {
  String wifiSsid;
  String wifiPass;
  String serverHost;
  uint16_t serverPort;
  String token;
  uint16_t configWebPort;
  bool forceConfigOnBoot;
  ProxyConfig proxies[MAX_PROXIES];
  bool autoStart;
};

class CryptoStream {
public:
  CryptoStream(WiFiClientSecure* stream, const String& token)
      : stream_(stream), encryptReady_(false), decryptReady_(false), readPos_(0), readLen_(0) {
    mbedtls_aes_init(&enc_);
    mbedtls_aes_init(&dec_);
    deriveKey(token);
  }

  ~CryptoStream() {
    mbedtls_aes_free(&enc_);
    mbedtls_aes_free(&dec_);
  }

  bool sendAll(const uint8_t* data, size_t len) {
    if (!encryptReady_) {
      uint8_t iv[16];
      for (size_t i = 0; i < sizeof(iv); i++) {
        iv[i] = (uint8_t)esp_random();
        encIv_[i] = iv[i];
      }
      if (!stream_->write(iv, sizeof(iv))) {
        return false;
      }
      mbedtls_aes_setkey_enc(&enc_, key_, 128);
      encryptReady_ = true;
    }

    uint8_t out[512];
    size_t offset = 0;
    while (offset < len) {
      size_t chunk = min(sizeof(out), len - offset);
      mbedtls_aes_crypt_cfb128(&enc_, MBEDTLS_AES_ENCRYPT, chunk, &encOffset_, encIv_, data + offset, out);
      if (stream_->write(out, chunk) != chunk) {
        return false;
      }
      offset += chunk;
    }
    return true;
  }

  int read(uint8_t* data, size_t len) {
    if (!decryptReady_) {
      if (!readPlainExact(decIv_, sizeof(decIv_))) {
        return -1;
      }
      mbedtls_aes_setkey_enc(&dec_, key_, 128);
      decryptReady_ = true;
    }

    while (readLen_ == 0) {
      int got = stream_->read(plainBuf_, sizeof(plainBuf_));
      if (got <= 0) {
        return got;
      }
      mbedtls_aes_crypt_cfb128(&dec_, MBEDTLS_AES_DECRYPT, got, &decOffset_, decIv_, plainBuf_, cryptBuf_);
      readPos_ = 0;
      readLen_ = got;
    }

    size_t take = min(len, readLen_);
    memcpy(data, cryptBuf_ + readPos_, take);
    readPos_ += take;
    readLen_ -= take;
    return (int)take;
  }

  bool readExact(uint8_t* data, size_t len) {
    size_t done = 0;
    while (done < len) {
      int got = read(data + done, len - done);
      if (got <= 0) {
        return false;
      }
      done += got;
    }
    return true;
  }

private:
  WiFiClientSecure* stream_;
  uint8_t key_[16];
  mbedtls_aes_context enc_;
  mbedtls_aes_context dec_;
  uint8_t encIv_[16] = {0};
  uint8_t decIv_[16] = {0};
  size_t encOffset_ = 0;
  size_t decOffset_ = 0;
  bool encryptReady_;
  bool decryptReady_;
  uint8_t plainBuf_[512];
  uint8_t cryptBuf_[512];
  size_t readPos_;
  size_t readLen_;

  bool readPlainExact(uint8_t* data, size_t len) {
    size_t done = 0;
    while (done < len) {
      int got = stream_->read(data + done, len - done);
      if (got <= 0) {
        return false;
      }
      done += got;
    }
    return true;
  }

  void deriveKey(const String& token) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    const uint8_t salt[] = {'m', 'i', 'd', 'h', 'u', 'n', '-', 'l', 'i', 'n', 'k'};
    mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const uint8_t*)token.c_str(), token.length(), salt, sizeof(salt), 64, 16, key_);
    mbedtls_md_free(&ctx);
  }
};

Preferences prefs;
WebServer web(DEFAULT_CONFIG_WEB_PORT);
TunnelConfig cfg;
TaskHandle_t tunnelTaskHandle = nullptr;
volatile bool tunnelWanted = false;
String statusText = "idle";
String runId;
String lastRemoteAddr;
unsigned long workCount = 0;

String htmlEscape(const String& value) {
  String out = value;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}

void loadConfig() {
  prefs.begin("GhostPort-link", true);
  cfg.wifiSsid = prefs.getString("wifiSsid", DEFAULT_WIFI_SSID);
  cfg.wifiPass = prefs.getString("wifiPass", DEFAULT_WIFI_PASS);
  cfg.serverHost = prefs.getString("serverHost", DEFAULT_SERVER_HOST);
  cfg.serverPort = prefs.getUShort("serverPort", DEFAULT_SERVER_PORT);
  cfg.token = prefs.getString("token", DEFAULT_TOKEN);
  cfg.configWebPort = prefs.getUShort("configPort", DEFAULT_CONFIG_WEB_PORT);
  cfg.forceConfigOnBoot = false;
  for (size_t i = 0; i < MAX_PROXIES; i++) {
    String prefix = "p" + String(i);
    String defaultName = i == 0 ? String(DEFAULT_PROXY_NAME) : "esp32_tcp_" + String(i + 1);
    cfg.proxies[i].enabled = prefs.getBool((prefix + "en").c_str(), i == 0);
    cfg.proxies[i].name = prefs.getString((prefix + "name").c_str(), defaultName);
    cfg.proxies[i].localHost = prefs.getString((prefix + "host").c_str(), DEFAULT_LOCAL_HOST);
    cfg.proxies[i].localPort = prefs.getUShort((prefix + "lport").c_str(), DEFAULT_LOCAL_PORT);
    cfg.proxies[i].remotePort = prefs.getUShort((prefix + "rport").c_str(), DEFAULT_REMOTE_PORT + i);
  }
  cfg.autoStart = prefs.getBool("autoStart", false);
  prefs.end();
}

void saveConfig() {
  prefs.begin("GhostPort-link", false);
  prefs.putString("wifiSsid", cfg.wifiSsid);
  prefs.putString("wifiPass", cfg.wifiPass);
  prefs.putString("serverHost", cfg.serverHost);
  prefs.putUShort("serverPort", cfg.serverPort);
  prefs.putString("token", cfg.token);
  prefs.putUShort("configPort", cfg.configWebPort);
  for (size_t i = 0; i < MAX_PROXIES; i++) {
    String prefix = "p" + String(i);
    prefs.putBool((prefix + "en").c_str(), cfg.proxies[i].enabled);
    prefs.putString((prefix + "name").c_str(), cfg.proxies[i].name);
    prefs.putString((prefix + "host").c_str(), cfg.proxies[i].localHost);
    prefs.putUShort((prefix + "lport").c_str(), cfg.proxies[i].localPort);
    prefs.putUShort((prefix + "rport").c_str(), cfg.proxies[i].remotePort);
  }
  prefs.putBool("autoStart", cfg.autoStart);
  prefs.end();
}

String md5Hex(const String& value) {
  uint8_t digest[16];
  char hex[33];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  mbedtls_md(info, (const uint8_t*)value.c_str(), value.length(), digest);
  for (int i = 0; i < 16; i++) {
    sprintf(hex + (i * 2), "%02x", digest[i]);
  }
  hex[32] = '\0';
  return String(hex);
}

bool writePlainMsg(WiFiClientSecure& client, char type, const String& payload) {
  uint8_t header[9];
  header[0] = (uint8_t)type;
  uint64_t len = payload.length();
  for (int i = 0; i < 8; i++) {
    header[8 - i] = (uint8_t)(len & 0xff);
    len >>= 8;
  }
  if (client.write(header, sizeof(header)) != sizeof(header)) {
    return false;
  }
  return client.write((const uint8_t*)payload.c_str(), payload.length()) == payload.length();
}

bool readPlainExact(WiFiClientSecure& client, uint8_t* data, size_t len) {
  size_t done = 0;
  while (done < len) {
    int got = client.read(data + done, len - done);
    if (got <= 0) {
      return false;
    }
    done += got;
  }
  return true;
}

bool readPlainMsg(WiFiClientSecure& client, char& type, String& payload) {
  uint8_t header[9];
  if (!readPlainExact(client, header, sizeof(header))) {
    return false;
  }
  type = (char)header[0];
  uint64_t len = 0;
  for (int i = 1; i < 9; i++) {
    len = (len << 8) | header[i];
  }
  if (len > 8192) {
    return false;
  }
  payload = "";
  payload.reserve((size_t)len + 1);
  while (payload.length() < len) {
    char buf[257];
    size_t need = min((size_t)256, (size_t)(len - payload.length()));
    int got = client.read((uint8_t*)buf, need);
    if (got <= 0) {
      return false;
    }
    buf[got] = '\0';
    payload += buf;
  }
  return true;
}

bool writeCryptoMsg(CryptoStream& stream, char type, const String& payload) {
  uint8_t header[9];
  header[0] = (uint8_t)type;
  uint64_t len = payload.length();
  for (int i = 0; i < 8; i++) {
    header[8 - i] = (uint8_t)(len & 0xff);
    len >>= 8;
  }
  if (!stream.sendAll(header, sizeof(header))) {
    return false;
  }
  return stream.sendAll((const uint8_t*)payload.c_str(), payload.length());
}

bool readCryptoMsg(CryptoStream& stream, char& type, String& payload) {
  uint8_t header[9];
  if (!stream.readExact(header, sizeof(header))) {
    return false;
  }
  type = (char)header[0];
  uint64_t len = 0;
  for (int i = 1; i < 9; i++) {
    len = (len << 8) | header[i];
  }
  if (len > 8192) {
    return false;
  }
  uint8_t buf[257];
  payload = "";
  payload.reserve((size_t)len + 1);
  while (payload.length() < len) {
    size_t need = min((size_t)256, (size_t)(len - payload.length()));
    if (!stream.readExact(buf, need)) {
      return false;
    }
    buf[need] = 0;
    payload += (const char*)buf;
  }
  return true;
}

String buildLoginJson() {
  time_t now = time(nullptr);
  if (now < 100000) {
    now = millis() / 1000;
  }

  StaticJsonDocument<512> doc;
  doc["version"] = "GhostPort-link-esp32/0.1";
  doc["hostname"] = WiFi.getHostname() ? WiFi.getHostname() : "esp32";
  doc["os"] = "esp32";
  doc["arch"] = "xtensa";
  doc["privilege_key"] = md5Hex(cfg.token + String((long long)now));
  doc["timestamp"] = (long long)now;
  doc["run_id"] = "";
  doc["pool_count"] = 1;

  String out;
  serializeJson(doc, out);
  return out;
}

String buildNewProxyJson(size_t proxyIndex) {
  StaticJsonDocument<256> doc;
  doc["proxy_name"] = cfg.proxies[proxyIndex].name;
  doc["proxy_type"] = "tcp";
  doc["remote_port"] = cfg.proxies[proxyIndex].remotePort;
  String out;
  serializeJson(doc, out);
  return out;
}

String buildNewWorkConnJson(size_t proxyIndex) {
  StaticJsonDocument<192> doc;
  doc["run_id"] = runId;
  doc["proxy_name"] = cfg.proxies[proxyIndex].name;
  String out;
  serializeJson(doc, out);
  return out;
}

bool connectServer(WiFiClientSecure& client) {
  client.setInsecure();
  client.setTimeout(10000);
  return client.connect(cfg.serverHost.c_str(), cfg.serverPort);
}

int findProxyIndexByName(const String& proxyName) {
  if (proxyName.length() == 0) {
    for (size_t i = 0; i < MAX_PROXIES; i++) {
      if (cfg.proxies[i].enabled) {
        return (int)i;
      }
    }
  }
  for (size_t i = 0; i < MAX_PROXIES; i++) {
    if (cfg.proxies[i].enabled && cfg.proxies[i].name == proxyName) {
      return (int)i;
    }
  }
  return -1;
}

void relayTcp(WiFiClient& local, WiFiClientSecure& work) {
  uint8_t buf[512];
  unsigned long lastActivity = millis();

  while (tunnelWanted && local.connected() && work.connected()) {
    int localReady = local.available();
    if (localReady > 0) {
      int got = local.read(buf, min((int)sizeof(buf), localReady));
      if (got > 0) {
        work.write(buf, got);
        lastActivity = millis();
      }
    }

    int workReady = work.available();
    if (workReady > 0) {
      int got = work.read(buf, min((int)sizeof(buf), workReady));
      if (got > 0) {
        local.write(buf, got);
        lastActivity = millis();
      }
    }

    if (millis() - lastActivity > 300000UL) {
      break;
    }
    delay(1);
  }

  local.stop();
  work.stop();
}

void workTask(void* param) {
  size_t proxyIndex = *((size_t*)param);
  delete (size_t*)param;
  if (proxyIndex >= MAX_PROXIES || !cfg.proxies[proxyIndex].enabled) {
    vTaskDelete(nullptr);
    return;
  }

  WiFiClientSecure work;
  WiFiClient local;
  String payload;
  char type;

  if (!connectServer(work)) {
    statusText = "work connect failed";
    vTaskDelete(nullptr);
    return;
  }

  if (!writePlainMsg(work, TYPE_NEW_WORK_CONN, buildNewWorkConnJson(proxyIndex))) {
    statusText = "work handshake send failed";
    work.stop();
    vTaskDelete(nullptr);
    return;
  }

  if (!readPlainMsg(work, type, payload) || type != TYPE_START_WORK_CONN) {
    statusText = "work handshake read failed";
    work.stop();
    vTaskDelete(nullptr);
    return;
  }

  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload);
  const char* error = doc["error"] | "";
  if (strlen(error) > 0) {
    statusText = String("server refused work: ") + error;
    work.stop();
    vTaskDelete(nullptr);
    return;
  }

  if (!local.connect(cfg.proxies[proxyIndex].localHost.c_str(), cfg.proxies[proxyIndex].localPort)) {
    statusText = "local target connect failed: " + cfg.proxies[proxyIndex].name;
    work.stop();
    vTaskDelete(nullptr);
    return;
  }

  workCount++;
  statusText = "relaying: " + cfg.proxies[proxyIndex].name;
  relayTcp(local, work);
  statusText = "registered";
  vTaskDelete(nullptr);
}

void startWorkTask(size_t proxyIndex) {
  size_t* taskProxyIndex = new size_t(proxyIndex);
  if (xTaskCreatePinnedToCore(workTask, "GhostPort-work", 8192, taskProxyIndex, 1, nullptr, 1) != pdPASS) {
    delete taskProxyIndex;
    statusText = "work task start failed";
  }
}

void tunnelTask(void*) {
  while (tunnelWanted) {
    WiFiClientSecure controlClient;
    statusText = "connecting server";

    if (!connectServer(controlClient)) {
      statusText = "server connect failed";
      delay(5000);
      continue;
    }

    if (!writePlainMsg(controlClient, TYPE_LOGIN, buildLoginJson())) {
      statusText = "login send failed";
      controlClient.stop();
      delay(5000);
      continue;
    }

    char type;
    String payload;
    if (!readPlainMsg(controlClient, type, payload) || type != TYPE_LOGIN_RESP) {
      statusText = "login response failed";
      controlClient.stop();
      delay(5000);
      continue;
    }

    StaticJsonDocument<512> loginResp;
    deserializeJson(loginResp, payload);
    const char* loginError = loginResp["error"] | "";
    if (strlen(loginError) > 0) {
      statusText = String("login failed: ") + loginError;
      controlClient.stop();
      delay(5000);
      continue;
    }
    runId = (const char*)(loginResp["run_id"] | "");

    CryptoStream control(&controlClient, cfg.token);
    size_t enabledCount = 0;
    bool registerFailed = false;
    for (size_t i = 0; i < MAX_PROXIES; i++) {
      if (!cfg.proxies[i].enabled || cfg.proxies[i].name.length() == 0) {
        continue;
      }
      enabledCount++;
      if (!writeCryptoMsg(control, TYPE_NEW_PROXY, buildNewProxyJson(i))) {
        statusText = "proxy register send failed";
        registerFailed = true;
        break;
      }
    }
    if (registerFailed) {
      controlClient.stop();
      delay(5000);
      continue;
    }
    if (enabledCount == 0) {
      statusText = "no enabled proxies";
      controlClient.stop();
      delay(5000);
      continue;
    }

    statusText = "registering proxy";
    while (tunnelWanted && controlClient.connected()) {
      if (!readCryptoMsg(control, type, payload)) {
        break;
      }

      if (type == TYPE_NEW_PROXY_RESP) {
        StaticJsonDocument<512> proxyResp;
        deserializeJson(proxyResp, payload);
        String proxyName = (const char*)(proxyResp["proxy_name"] | "");
        const char* error = proxyResp["error"] | "";
        if (strlen(error) > 0) {
          statusText = String("proxy failed ") + proxyName + ": " + error;
        } else {
          lastRemoteAddr = (const char*)(proxyResp["remote_addr"] | "");
          statusText = "registered";
        }
      } else if (type == TYPE_REQ_WORK_CONN) {
        StaticJsonDocument<256> req;
        deserializeJson(req, payload);
        String proxyName = (const char*)(req["proxy_name"] | "");
        int proxyIndex = findProxyIndexByName(proxyName);
        if (proxyIndex >= 0) {
          startWorkTask((size_t)proxyIndex);
        } else {
          statusText = String("unknown proxy request: ") + proxyName;
        }
      }
    }

    controlClient.stop();
    if (tunnelWanted) {
      statusText = "control disconnected";
      delay(3000);
    }
  }

  statusText = "stopped";
  tunnelTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void startTunnel() {
  if (tunnelTaskHandle != nullptr) {
    return;
  }
  tunnelWanted = true;
  xTaskCreatePinnedToCore(tunnelTask, "GhostPort-tunnel", 12288, nullptr, 1, &tunnelTaskHandle, 1);
}

void stopTunnel() {
  tunnelWanted = false;
}

String page() {
  String checked = cfg.autoStart ? "checked" : "";
  String running = tunnelWanted ? "Running" : "Stopped";
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  String body = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  body += "<title>GhostPort Link ESP32</title><style>";
  body += "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f6f7f9;color:#1e293b}";
  body += "main{max-width:760px;margin:0 auto;padding:24px}h1{font-size:26px;margin:0 0 4px}";
  body += ".bar{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}.pill{background:#fff;border:1px solid #d8dee8;border-radius:6px;padding:8px 10px}";
  body += "form{background:#fff;border:1px solid #d8dee8;border-radius:8px;padding:18px;display:grid;gap:14px}";
  body += "label{display:grid;gap:6px;font-size:13px;font-weight:650}input{font:inherit;padding:10px;border:1px solid #cbd5e1;border-radius:6px}";
  body += ".row{display:grid;grid-template-columns:1fr 120px;gap:12px}.proxy{display:grid;grid-template-columns:70px 1fr 1fr 110px 110px;gap:10px;align-items:end;border-top:1px solid #e2e8f0;padding-top:12px}.actions{display:flex;gap:10px;flex-wrap:wrap}";
  body += "button,a.button{font:inherit;text-decoration:none;background:#155e75;color:white;border:0;border-radius:6px;padding:10px 14px;cursor:pointer}";
  body += "a.stop{background:#9f1239}.muted{color:#64748b;font-size:14px}.check{display:flex;gap:8px;align-items:center;font-size:13px;font-weight:650}@media(max-width:760px){.row,.proxy{grid-template-columns:1fr}}";
  body += "</style></head><body><main><h1>GhostPort Link ESP32</h1><div class='muted'>Local TCP tunnel client</div>";
  body += "<div class='bar'><div class='pill'>State: " + htmlEscape(running) + "</div>";
  body += "<div class='pill'>Status: " + htmlEscape(statusText) + "</div>";
  body += "<div class='pill'>IP: " + htmlEscape(ip) + "</div>";
  body += "<div class='pill'>Work conns: " + String(workCount) + "</div></div>";
  if (lastRemoteAddr.length()) {
    body += "<p class='muted'>Remote listener: " + htmlEscape(lastRemoteAddr) + "</p>";
  }
  body += "<form method='post' action='/save'>";
  body += "<div class='row'><label>Wi-Fi SSID<input name='wifiSsid' value='" + htmlEscape(cfg.wifiSsid) + "'></label>";
  body += "<label>Wi-Fi password<input type='password' name='wifiPass' value='" + htmlEscape(cfg.wifiPass) + "'></label></div>";
  body += "<div class='row'><label>Server host<input name='serverHost' value='" + htmlEscape(cfg.serverHost) + "'></label>";
  body += "<label>Server port<input type='number' name='serverPort' value='" + String(cfg.serverPort) + "'></label></div>";
  body += "<label>Token<input type='password' name='token' value='" + htmlEscape(cfg.token) + "'></label>";
  body += "<div><strong>Ports</strong><div class='muted'>Enable each remote server port you want bridged to a local device.</div></div>";
  for (size_t i = 0; i < MAX_PROXIES; i++) {
    String prefix = "p" + String(i);
    String rowChecked = cfg.proxies[i].enabled ? "checked" : "";
    body += "<div class='proxy'>";
    body += "<label class='check'><input type='checkbox' name='" + prefix + "en' " + rowChecked + "> On</label>";
    body += "<label>Name<input name='" + prefix + "name' value='" + htmlEscape(cfg.proxies[i].name) + "'></label>";
    body += "<label>Local host<input name='" + prefix + "host' value='" + htmlEscape(cfg.proxies[i].localHost) + "'></label>";
    body += "<label>Local port<input type='number' name='" + prefix + "lport' value='" + String(cfg.proxies[i].localPort) + "'></label>";
    body += "<label>Remote port<input type='number' name='" + prefix + "rport' value='" + String(cfg.proxies[i].remotePort) + "'></label>";
    body += "</div>";
  }
  body += "<label class='check'><input type='checkbox' name='autoStart' " + checked + "> Start tunnel after boot</label>";
  body += "<div class='muted'>Config UI: http://" + htmlEscape(ip) + ":" + String(DEFAULT_CONFIG_WEB_PORT) + "/. Hold GPIO" + String(CONFIG_MODE_PIN) + " HIGH at boot to force setup mode.</div>";
  body += "<div class='actions'><button type='submit'>Save</button><a class='button' href='/start'>Start</a><a class='button stop' href='/stop'>Stop</a></div>";
  body += "</form></main></body></html>";
  return body;
}

void handleRoot() {
  web.send(200, "text/html", page());
}

void handleSave() {
  cfg.wifiSsid = web.arg("wifiSsid");
  cfg.wifiPass = web.arg("wifiPass");
  cfg.serverHost = web.arg("serverHost");
  cfg.serverPort = (uint16_t)web.arg("serverPort").toInt();
  cfg.token = web.arg("token");
  for (size_t i = 0; i < MAX_PROXIES; i++) {
    String prefix = "p" + String(i);
    cfg.proxies[i].enabled = web.hasArg(prefix + "en");
    cfg.proxies[i].name = web.arg(prefix + "name");
    cfg.proxies[i].localHost = web.arg(prefix + "host");
    cfg.proxies[i].localPort = (uint16_t)web.arg(prefix + "lport").toInt();
    cfg.proxies[i].remotePort = (uint16_t)web.arg(prefix + "rport").toInt();
  }
  cfg.autoStart = web.hasArg("autoStart");
  saveConfig();
  web.sendHeader("Location", "/");
  web.send(303);
}

bool isConfigPinActive() {
  pinMode(CONFIG_MODE_PIN, CONFIG_MODE_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  delay(20);
  int level = digitalRead(CONFIG_MODE_PIN);
  return CONFIG_MODE_ACTIVE_HIGH ? level == HIGH : level == LOW;
}

void connectWifi(bool forceSetupAp) {
  if (forceSetupAp) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GhostPortLink-Setup", "GhostPortlink");
    statusText = "setup ap forced";
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("GhostPort-link-esp32");
  if (cfg.wifiSsid.length() > 0) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(250);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GhostPortLink-Setup", "GhostPortlink");
    statusText = "setup ap";
  } else {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    statusText = "wifi connected";
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  loadConfig();
  cfg.forceConfigOnBoot = isConfigPinActive();
  connectWifi(cfg.forceConfigOnBoot);

  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/start", HTTP_GET, []() {
    startTunnel();
    web.sendHeader("Location", "/");
    web.send(303);
  });
  web.on("/stop", HTTP_GET, []() {
    stopTunnel();
    web.sendHeader("Location", "/");
    web.send(303);
  });
  web.begin();

  Serial.print("Config UI: http://");
  Serial.print(WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP());
  Serial.print(":");
  Serial.print(DEFAULT_CONFIG_WEB_PORT);
  Serial.println("/");

  if (WiFi.isConnected() && cfg.autoStart && !cfg.forceConfigOnBoot) {
    startTunnel();
  }
}

void loop() {
  web.handleClient();
  delay(2);
}
