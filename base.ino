//Base Debug script that connects to the server and opens a port with everything hardcodded. use this to check if the old one seems too complicated, also aded some similicity over the root file
//this is tested and worked finewith basic HTML pages an SSH(Slow as F**K)
//but yeah itworks

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "esp_system.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/md5.h"
#include "mbedtls/pkcs5.h"

// Board target: ESP32 Arduino core.
// Library needed: ArduinoJson.

static const char *WIFI_SSID = "SSID";
static const char *WIFI_PASSWORD = "PSSWD";

static const char *SERVER_ADDR = "domain.com";
static const uint16_t SERVER_PORT = 1000;
static const char *AUTH_TOKEN = "xxxxxxxxxxxxxxxxxxxxxxx";

static const char *PROXY_NAME = "port_1194";
static const char *PROXY_TYPE = "tcp";
static const char *LOCAL_IP = "192.168.1.34";
static const uint16_t LOCAL_PORT = 1194;
static const uint16_t REMOTE_PORT = 1194;

static const uint32_t TIMEOUT_MS = 10000;
static const char *BUILD_ID = "GhostP-esp32-stable-web-final-20260716";

static const uint8_t TYPE_LOGIN = 'o';
static const uint8_t TYPE_LOGIN_RESP = '1';
static const uint8_t TYPE_NEW_PROXY = 'p';
static const uint8_t TYPE_NEW_PROXY_RESP = '2';
static const uint8_t TYPE_NEW_WORK_CONN = 'w';
static const uint8_t TYPE_REQ_WORK_CONN = 'r';
static const uint8_t TYPE_START_WORK_CONN = 's';
static const uint8_t TYPE_PING = 'h';
static const uint8_t TYPE_PONG = '4';

static const uint8_t YAMUX_VERSION = 0;
static const uint8_t YAMUX_TYPE_DATA = 0;
static const uint8_t YAMUX_TYPE_WINDOW_UPDATE = 1;
static const uint8_t YAMUX_TYPE_PING = 2;
static const uint8_t YAMUX_TYPE_GOAWAY = 3;
static const uint16_t YAMUX_FLAG_SYN = 0x01;
static const uint16_t YAMUX_FLAG_ACK = 0x02;
static const uint16_t YAMUX_FLAG_FIN = 0x04;
static const uint16_t YAMUX_FLAG_RST = 0x08;
static const uint32_t YAMUX_STREAM_ID = 1;
static const uint32_t YAMUX_INITIAL_WINDOW = 6 * 1024 * 1024;

class BasicStream {
public:
  virtual ~BasicStream() {}
  virtual int readBytes(uint8_t *buf, size_t len) = 0;
  virtual bool writeBytes(const uint8_t *buf, size_t len) = 0;
  virtual bool availableData() = 0;
  virtual bool pollData() { return availableData(); }
  virtual bool connected() = 0;
  virtual void stop() = 0;
};

class ClientStream : public BasicStream {
public:
  explicit ClientStream(WiFiClientSecure *client) : client_(client) {}

  ~ClientStream() override {
    client_->stop();
    delete client_;
  }

  int readBytes(uint8_t *buf, size_t len) override {
    size_t got = 0;
    while (got < len) {
      int n = client_->read(buf + got, len - got);
      if (n > 0) {
        got += n;
        continue;
      }
      if (!client_->connected()) {
        return got;
      }
      delay(1);
    }
    return got;
  }

  bool writeBytes(const uint8_t *buf, size_t len) override {
    size_t sent = 0;
    while (sent < len) {
      size_t n = client_->write(buf + sent, len - sent);
      if (n == 0) {
        if (!client_->connected()) {
          return false;
        }
        delay(1);
        continue;
      }
      sent += n;
    }
    return true;
  }

  bool availableData() override {
    return client_->available() > 0;
  }

  bool connected() override {
    return client_->connected();
  }

  void stop() override {
    client_->stop();
  }

private:
  WiFiClientSecure *client_;
};

static bool readExact(BasicStream &stream, uint8_t *buf, size_t len) {
  return stream.readBytes(buf, len) == (int)len;
}

static void putBE16(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value & 0xff;
}

static void putBE32(uint8_t *buf, uint32_t value) {
  buf[0] = value >> 24;
  buf[1] = value >> 16;
  buf[2] = value >> 8;
  buf[3] = value & 0xff;
}

static uint16_t getBE16(const uint8_t *buf) {
  return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint32_t getBE32(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | buf[3];
}

static void putBE64(uint8_t *buf, uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    buf[i] = value & 0xff;
    value >>= 8;
  }
}

static uint64_t getBE64(const uint8_t *buf) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | buf[i];
  }
  return value;
}

class YamuxStream : public BasicStream {
public:
  explicit YamuxStream(BasicStream *base)
      : base_(base), buffered_(0), offset_(0), frameRemaining_(0),
        pendingWindowUpdate_(0), writeClosed_(false), closed_(false) {
    writeFrame(YAMUX_TYPE_WINDOW_UPDATE, YAMUX_FLAG_SYN, YAMUX_STREAM_ID,
               YAMUX_INITIAL_WINDOW, nullptr, 0);
  }

  ~YamuxStream() override {
    delete base_;
  }

  int readBytes(uint8_t *buf, size_t len) override {
    size_t copied = 0;
    while (copied < len) {
      if (offset_ >= buffered_) {
        buffered_ = 0;
        offset_ = 0;
        if (frameRemaining_ > 0) {
          size_t chunk = min(len - copied, (size_t)frameRemaining_);
          int got = base_->readBytes(buf + copied, chunk);
          if (got <= 0) {
            return copied;
          }
          frameRemaining_ -= got;
          copied += got;
          continue;
        }
        if (!readNextFrame()) {
          return copied;
        }
      }
      size_t chunk = min(len - copied, buffered_ - offset_);
      memcpy(buf + copied, buffer_ + offset_, chunk);
      offset_ += chunk;
      copied += chunk;
    }
    acknowledgeRead(copied);
    return copied;
  }

  int readSome(uint8_t *buf, size_t len) {
    if (len == 0) {
      return 0;
    }

    while (offset_ >= buffered_ && frameRemaining_ == 0) {
      buffered_ = 0;
      offset_ = 0;
      if (!readNextFrame()) {
        return 0;
      }
    }

    size_t copied = 0;
    if (offset_ < buffered_) {
      copied = min(len, buffered_ - offset_);
      memcpy(buf, buffer_ + offset_, copied);
      offset_ += copied;
    } else {
      copied = min(len, (size_t)frameRemaining_);
      int got = base_->readBytes(buf, copied);
      if (got <= 0) {
        return 0;
      }
      copied = got;
      frameRemaining_ -= got;
    }

    acknowledgeRead(copied);
    return copied;
  }

  bool writeBytes(const uint8_t *buf, size_t len) override {
    if (closed_ || writeClosed_) {
      return false;
    }
    const size_t chunkSize = 2048;
    size_t sent = 0;
    while (sent < len) {
      size_t chunk = min(chunkSize, len - sent);
      if (!writeFrame(YAMUX_TYPE_DATA, 0, YAMUX_STREAM_ID, chunk, buf + sent, chunk)) {
        return false;
      }
      sent += chunk;
    }
    return true;
  }

  bool availableData() override {
    return buffered_ > offset_ || frameRemaining_ > 0 || base_->availableData();
  }

  bool pollData() override {
    if (buffered_ > offset_ || frameRemaining_ > 0) {
      return true;
    }
    buffered_ = 0;
    offset_ = 0;
    return readNextFrame(false);
  }

  bool connected() override {
    return !closed_ && base_->connected();
  }

  void stop() override {
    if (!closed_) {
      shutdownWrite();
      closed_ = true;
    }
    base_->stop();
  }

  void shutdownWrite() {
    if (!closed_ && !writeClosed_) {
      writeFrame(YAMUX_TYPE_DATA, YAMUX_FLAG_FIN, YAMUX_STREAM_ID, 0, nullptr, 0);
      writeClosed_ = true;
    }
  }

private:
  void acknowledgeRead(size_t length) {
    pendingWindowUpdate_ += length;
    if (pendingWindowUpdate_ >= 32768) {
      writeFrame(YAMUX_TYPE_WINDOW_UPDATE, 0, YAMUX_STREAM_ID,
                 pendingWindowUpdate_, nullptr, 0);
      pendingWindowUpdate_ = 0;
    }
  }

  bool writeFrame(uint8_t frameType, uint16_t flags, uint32_t streamId,
                  uint32_t length, const uint8_t *payload, size_t payloadLen) {
    uint8_t header[12];
    header[0] = YAMUX_VERSION;
    header[1] = frameType;
    putBE16(header + 2, flags);
    putBE32(header + 4, streamId);
    putBE32(header + 8, length);
    if (!base_->writeBytes(header, sizeof(header))) {
      return false;
    }
    return payloadLen == 0 || base_->writeBytes(payload, payloadLen);
  }

  bool readNextFrame(bool waitForData = true) {
    uint8_t header[12];
    while (true) {
      if (!waitForData && !base_->availableData()) {
        return false;
      }
      if (!readExact(*base_, header, sizeof(header))) {
        closed_ = true;
        return false;
      }
      if (header[0] != YAMUX_VERSION) {
        Serial.println("Unsupported yamux version");
        return false;
      }

      uint8_t frameType = header[1];
      uint16_t flags = getBE16(header + 2);
      uint32_t streamId = getBE32(header + 4);
      uint32_t length = getBE32(header + 8);

      uint8_t *payload = buffer_;
      if (frameType == YAMUX_TYPE_DATA && length > 0) {
        uint32_t toRead = min(length, (uint32_t)sizeof(buffer_));
        if (!readExact(*base_, payload, toRead)) {
          return false;
        }
        frameRemaining_ = length - toRead;
      }

      if (frameType == YAMUX_TYPE_DATA && streamId == YAMUX_STREAM_ID) {
        if (flags & YAMUX_FLAG_RST) {
          closed_ = true;
          return false;
        }
        if (length > 0) {
          buffered_ = min(length, (uint32_t)sizeof(buffer_));
          offset_ = 0;
          return true;
        }
        if (flags & YAMUX_FLAG_FIN) {
          closed_ = true;
          return false;
        }
      } else if (frameType == YAMUX_TYPE_PING && !(flags & YAMUX_FLAG_ACK)) {
        writeFrame(YAMUX_TYPE_PING, YAMUX_FLAG_ACK, 0, length, nullptr, 0);
      } else if (frameType == YAMUX_TYPE_GOAWAY) {
        closed_ = true;
        return false;
      }
    }
  }

  BasicStream *base_;
  uint8_t buffer_[2048];
  size_t buffered_;
  size_t offset_;
  uint32_t frameRemaining_;
  uint32_t pendingWindowUpdate_;
  bool writeClosed_;
  bool closed_;
};

class CryptoStream : public BasicStream {
public:
  CryptoStream(BasicStream *base, const char *token)
      : base_(base), encryptReady_(false), decryptReady_(false), decOffset_(0), decBuffered_(0) {
    deriveKey(token);
    mbedtls_aes_init(&enc_);
    mbedtls_aes_init(&dec_);
  }

  ~CryptoStream() override {
    mbedtls_aes_free(&enc_);
    mbedtls_aes_free(&dec_);
  }

  int readBytes(uint8_t *buf, size_t len) override {
    size_t copied = 0;
    while (copied < len) {
      if (decOffset_ >= decBuffered_) {
        decOffset_ = 0;
        decBuffered_ = 0;
        if (!fillDecryptBuffer()) {
          return copied;
        }
      }
      size_t chunk = min(len - copied, decBuffered_ - decOffset_);
      memcpy(buf + copied, decBuffer_ + decOffset_, chunk);
      decOffset_ += chunk;
      copied += chunk;
    }
    return copied;
  }

  bool writeBytes(const uint8_t *buf, size_t len) override {
    if (!encryptReady_) {
      uint8_t iv[16];
      for (size_t i = 0; i < sizeof(iv); i += 4) {
        uint32_t rnd = esp_random();
        memcpy(iv + i, &rnd, min((size_t)4, sizeof(iv) - i));
      }
      memcpy(encIv_, iv, sizeof(encIv_));
      encOff_ = 0;
      mbedtls_aes_setkey_enc(&enc_, key_, 128);
      if (!base_->writeBytes(iv, sizeof(iv))) {
        return false;
      }
      encryptReady_ = true;
    }

    uint8_t out[1024];
    size_t done = 0;
    while (done < len) {
      size_t chunk = min(sizeof(out), len - done);
      mbedtls_aes_crypt_cfb128(&enc_, MBEDTLS_AES_ENCRYPT, chunk, &encOff_, encIv_,
                               buf + done, out);
      if (!base_->writeBytes(out, chunk)) {
        return false;
      }
      done += chunk;
    }
    return true;
  }

  bool availableData() override {
    return decBuffered_ > decOffset_ || base_->availableData();
  }

  bool pollData() override {
    return decBuffered_ > decOffset_ || base_->pollData();
  }

  bool connected() override {
    return base_->connected();
  }

  void stop() override {
    base_->stop();
  }

private:
  void deriveKey(const char *token) {
    const uint8_t salt[] = {'f', 'r', 'p'};
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA1,
                                  (const uint8_t *)token, strlen(token),
                                  salt, sizeof(salt), 64, sizeof(key_), key_);
  }

  bool fillDecryptBuffer() {
    if (!decryptReady_) {
      uint8_t iv[16];
      if (!readExact(*base_, iv, sizeof(iv))) {
        return false;
      }
      memcpy(decIv_, iv, sizeof(decIv_));
      decOff_ = 0;
      mbedtls_aes_setkey_enc(&dec_, key_, 128);
      decryptReady_ = true;
    }

    uint8_t in[1024];
    int n = base_->readBytes(in, 1);
    if (n != 1) {
      return false;
    }
    while (n < (int)sizeof(in) && base_->availableData()) {
      int got = base_->readBytes(in + n, 1);
      if (got != 1) {
        break;
      }
      n += got;
    }
    mbedtls_aes_crypt_cfb128(&dec_, MBEDTLS_AES_DECRYPT, n, &decOff_, decIv_, in, decBuffer_);
    decBuffered_ = n;
    return true;
  }

  BasicStream *base_;
  uint8_t key_[16];
  mbedtls_aes_context enc_;
  mbedtls_aes_context dec_;
  bool encryptReady_;
  bool decryptReady_;
  uint8_t encIv_[16];
  uint8_t decIv_[16];
  size_t encOff_;
  size_t decOff_;
  uint8_t decBuffer_[1024];
  size_t decOffset_;
  size_t decBuffered_;
};

static String md5Hex(const String &input) {
  uint8_t digest[16];
  char out[33];
  mbedtls_md5((const uint8_t *)input.c_str(), input.length(), digest);
  for (int i = 0; i < 16; ++i) {
    sprintf(out + i * 2, "%02x", digest[i]);
  }
  out[32] = '\0';
  return String(out);
}

static bool writeMsg(BasicStream &stream, uint8_t typeByte, const JsonDocument &doc) {
  String payload;
  serializeJson(doc, payload);

  uint8_t header[9];
  header[0] = typeByte;
  putBE64(header + 1, payload.length());
  return stream.writeBytes(header, sizeof(header)) &&
         stream.writeBytes((const uint8_t *)payload.c_str(), payload.length());
}

static bool readMsg(BasicStream &stream, uint8_t &typeByte, DynamicJsonDocument &doc) {
  uint8_t header[9];
  if (!readExact(stream, header, sizeof(header))) {
    return false;
  }
  typeByte = header[0];
  uint64_t len = getBE64(header + 1);
  if (len > 8192) {
    Serial.println("GhostP message too large for sketch buffer");
    return false;
  }

  char payload[8193];
  if (!readExact(stream, (uint8_t *)payload, len)) {
    return false;
  }
  payload[len] = '\0';

  doc.clear();
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

static YamuxStream *openMessageStream(WiFiClientSecure **tlsClientOut = nullptr) {
  WiFiClientSecure *tls = new WiFiClientSecure();
  tls->setInsecure();
  tls->setTimeout(TIMEOUT_MS / 1000);

  Serial.printf("Connecting to %s:%u... heap=%u\n", SERVER_ADDR, SERVER_PORT, ESP.getFreeHeap());
  if (!tls->connect(SERVER_ADDR, SERVER_PORT)) {
    Serial.printf("TLS connection failed, heap=%u\n", ESP.getFreeHeap());
    delete tls;
    return nullptr;
  }
  Serial.printf("TLS connected, heap=%u\n", ESP.getFreeHeap());

  ClientStream *base = new ClientStream(tls);
  Serial.println("Starting yamux stream");
  YamuxStream *yamux = new YamuxStream(base);
  Serial.println("Yamux stream ready");
  if (tlsClientOut) {
    *tlsClientOut = tls;
  }
  return yamux;
}

static void buildLogin(DynamicJsonDocument &doc) {
  uint32_t timestamp = time(nullptr);
  doc.clear();
  doc["version"] = "arduino-GhostP/0.1";
  doc["hostname"] = "esp32-GhostP";
  doc["os"] = "arduino";
  doc["arch"] = "esp32";
  doc["privilege_key"] = md5Hex(String(AUTH_TOKEN) + String(timestamp));
  doc["timestamp"] = timestamp;
  doc["pool_count"] = 0;
}

static void buildNewProxy(DynamicJsonDocument &doc) {
  doc.clear();
  doc["proxy_name"] = PROXY_NAME;
  doc["proxy_type"] = PROXY_TYPE;
  doc["remote_port"] = REMOTE_PORT;
}

static void buildNewWorkConn(DynamicJsonDocument &doc, const String &runId) {
  doc.clear();
  doc["run_id"] = runId;
}

static void buildPing(DynamicJsonDocument &doc) {
  uint32_t timestamp = time(nullptr);
  doc.clear();
  doc["privilege_key"] = md5Hex(String(AUTH_TOKEN) + String(timestamp));
  doc["timestamp"] = timestamp;
}

struct RelayArgs {
  WiFiClient *local;
  YamuxStream *work;
};

static volatile bool workSlotBusy = false;

static void relayTask(void *param) {
  RelayArgs *args = (RelayArgs *)param;
  WiFiClient *local = args->local;
  YamuxStream *work = args->work;
  uint8_t buf[2048];

  Serial.printf("Relaying %s to %s:%u\n", PROXY_NAME, LOCAL_IP, LOCAL_PORT);
  bool localFinished = false;
  while (work->connected()) {
    int localAvailable = local->available();
    int ln = localAvailable > 0 ? local->read(buf, sizeof(buf)) : 0;
    if (ln > 0 && !work->writeBytes(buf, ln)) {
      break;
    }

    if (!local->connected() && local->available() == 0) {
      work->shutdownWrite();
      localFinished = true;
      break;
    }

    if (work->availableData()) {
      int wn = work->readSome(buf, sizeof(buf));
      if (wn <= 0) {
        break;
      }
      size_t sent = 0;
      while (sent < (size_t)wn) {
        size_t n = local->write(buf + sent, wn - sent);
        if (n == 0) {
          break;
        }
        sent += n;
      }
      if (sent != (size_t)wn) {
        break;
      }
    }

    delay(1);
  }

  if (localFinished) {
    delay(100);
  }

  work->stop();
  local->stop();
  delete work;
  delete local;
  delete args;
  workSlotBusy = false;
  Serial.println("Relay ended");
  vTaskDelete(nullptr);
}

static void handleWorkConnTask(void *param) {
  String runId = *((String *)param);
  delete (String *)param;

  YamuxStream *work = openMessageStream();
  if (!work) {
    workSlotBusy = false;
    vTaskDelete(nullptr);
  }

  DynamicJsonDocument doc(2048);
  buildNewWorkConn(doc, runId);
  if (!writeMsg(*work, TYPE_NEW_WORK_CONN, doc)) {
    Serial.println("Failed to send NewWorkConn");
    work->stop();
    delete work;
    workSlotBusy = false;
    vTaskDelete(nullptr);
  }

  uint8_t typeByte = 0;
  bool gotStart = readMsg(*work, typeByte, doc);
  if (!gotStart || typeByte != TYPE_START_WORK_CONN) {
    String payload;
    serializeJson(doc, payload);
    Serial.printf("Expected StartWorkConn, got %s type %c: %s\n",
                  gotStart ? "message" : "no message",
                  gotStart ? typeByte : '?',
                  payload.c_str());
    work->stop();
    delete work;
    workSlotBusy = false;
    vTaskDelete(nullptr);
  }
  if (doc["error"].is<const char *>() && strlen(doc["error"]) > 0) {
    Serial.printf("StartWorkConn error: %s\n", doc["error"].as<const char *>());
    work->stop();
    delete work;
    workSlotBusy = false;
    vTaskDelete(nullptr);
  }

  WiFiClient *local = new WiFiClient();
  local->setTimeout(TIMEOUT_MS / 1000);
  if (!local->connect(LOCAL_IP, LOCAL_PORT)) {
    Serial.println("Local service connection failed");
    work->stop();
    delete work;
    delete local;
    workSlotBusy = false;
    vTaskDelete(nullptr);
  }

  RelayArgs *args = new RelayArgs{local, work};
  relayTask(args);
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi SSID %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi connected: %s\n", WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for time");
  while (time(nullptr) < 1700000000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

static void GhostPTask(void *param) {
  (void)param;
  YamuxStream *control = openMessageStream();
  if (!control) {
    vTaskDelete(nullptr);
  }

  DynamicJsonDocument doc(4096);
  buildLogin(doc);
  if (!writeMsg(*control, TYPE_LOGIN, doc)) {
    Serial.println("Failed to send Login");
    control->stop();
    vTaskDelete(nullptr);
  }

  uint8_t typeByte = 0;
  if (!readMsg(*control, typeByte, doc)) {
    Serial.println("Failed to read LoginResp");
    control->stop();
    vTaskDelete(nullptr);
  }

  String loginJson;
  serializeJson(doc, loginJson);
  Serial.printf("Received type %c: %s\n", typeByte, loginJson.c_str());
  if (typeByte != TYPE_LOGIN_RESP || (doc["error"].is<const char *>() && strlen(doc["error"]) > 0)) {
    Serial.println("Login failed");
    control->stop();
    vTaskDelete(nullptr);
  }

  String runId = doc["run_id"].as<String>();
  Serial.printf("Login OK. RunID: %s\n", runId.c_str());

  CryptoStream *crypto = new CryptoStream(control, AUTH_TOKEN);
  buildNewProxy(doc);
  if (!writeMsg(*crypto, TYPE_NEW_PROXY, doc)) {
    Serial.println("Failed to send NewProxy");
    crypto->stop();
    vTaskDelete(nullptr);
  }

  uint32_t lastHeartbeat = millis();
  while (true) {
    if (millis() - lastHeartbeat >= 25000) {
      buildPing(doc);
      if (!writeMsg(*crypto, TYPE_PING, doc)) {
        Serial.println("Failed to send heartbeat");
        crypto->stop();
        delay(5000);
        ESP.restart();
      }
      Serial.println("Heartbeat sent");
      lastHeartbeat = millis();
    }

    if (!crypto->pollData()) {
      if (!crypto->connected()) {
        Serial.println("Control connection closed");
        crypto->stop();
        delay(5000);
        ESP.restart();
      }
      delay(10);
      continue;
    }

    doc.clear();
    if (!readMsg(*crypto, typeByte, doc)) {
      Serial.println("Control message read failed");
      crypto->stop();
      delay(5000);
      ESP.restart();
    }

    if (typeByte == TYPE_NEW_PROXY_RESP) {
      if (doc["error"].is<const char *>() && strlen(doc["error"]) > 0) {
        Serial.printf("NewProxyResp error: %s\n", doc["error"].as<const char *>());
        crypto->stop();
        Serial.println("Retrying GhostP registration in 30 seconds");
        delay(30000);
        ESP.restart();
        vTaskDelete(nullptr);
      }
      Serial.printf("Proxy started: %s\n", doc["remote_addr"] | "");
    } else if (typeByte == TYPE_PONG) {
      if (doc["error"].is<const char *>() && strlen(doc["error"]) > 0) {
        Serial.printf("Heartbeat error: %s\n", doc["error"].as<const char *>());
      }
    } else if (typeByte == TYPE_REQ_WORK_CONN) {
      if (workSlotBusy) {
        Serial.println("Work slot busy; ignoring extra request");
        continue;
      }
      workSlotBusy = true;
      String *rid = new String(runId);
      if (xTaskCreatePinnedToCore(handleWorkConnTask, "GhostP-work", 24576,
                                  rid, 1, nullptr, 1) != pdPASS) {
        Serial.println("Failed to create work task");
        delete rid;
        workSlotBusy = false;
      }
    } else {
      String payload;
      serializeJson(doc, payload);
      Serial.printf("Received type %c: %s\n", typeByte, payload.c_str());
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("Firmware: %s\n", BUILD_ID);
  connectWifi();
  xTaskCreatePinnedToCore(GhostPTask, "GhostP-main", 24576, nullptr, 1, nullptr, 1);
}

void loop() {
  delay(1000);
}
