#include "ObdClient.h"
#include "config.h"
#include "Settings.h"

ObdClient obd;

// ELM327 initialisation handshake (sent one command at a time).
static const char* INIT_CMDS[] = {
  "ATZ",     // reset
  "ATE0",    // echo off  (essential: otherwise responses echo the command)
  "ATL0",    // linefeeds off
  "ATS0",    // spaces off (compact responses)
  "ATH0",    // headers off (response = data bytes only)
  "ATSP0",   // auto-detect protocol
  "0100",    // probe: forces protocol negotiation ("SEARCHING...")
};
static const uint8_t INIT_COUNT = sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]);

// Live PIDs polled round-robin while READY.
static const char* POLL_PIDS[] = { "010C", "0105", "010D", "0111" };
static const uint8_t POLL_COUNT = sizeof(POLL_PIDS) / sizeof(POLL_PIDS[0]);

// --- helpers ---------------------------------------------------------------
static uint8_t hexByte(const String& s, int i) {
  auto nib = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  if (i < 0 || i + 1 >= (int)s.length()) return 0;
  return (nib(s[i]) << 4) | nib(s[i + 1]);
}

// Clean a raw ELM response into an uppercase, whitespace-free string.
static String clean(const String& raw) {
  String s = raw;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(".", "");   // "SEARCHING..." dots
  return s;
}
static bool isNoise(const String& s) {
  return s.indexOf("NODATA") >= 0 || s.indexOf("UNABLE") >= 0 ||
         s.indexOf("STOPPED") >= 0 || s.indexOf("ERROR") >= 0 ||
         s.indexOf("SEARCHING") >= 0 || s.length() == 0;
}

// ---------------------------------------------------------------------------
void ObdClient::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                  // lower latency for polling
  if (settings.pass.length() == 0) WiFi.begin(settings.ssid.c_str());
  else                             WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  _state = ObdState::WIFI_CONNECTING;
  _stamp = millis();
}

const char* ObdClient::stateText() const {
  switch (_state) {
    case ObdState::IDLE:            return "Select a network";
    case ObdState::WIFI_CONNECTING: return "Joining adapter WiFi...";
    case ObdState::TCP_CONNECTING:  return "Connecting to ELM327...";
    case ObdState::INITIALIZING:    return "Initializing OBD...";
    case ObdState::READY:           return "Connected";
    case ObdState::DTC_SCAN:        return "Reading codes...";
    case ObdState::DTC_CLEAR:       return "Clearing codes...";
    case ObdState::FAILED:          return "Connection failed, retrying...";
  }
  return "";
}

void ObdClient::sendCmd(const char* cmd) {
  _client.print(cmd);
  _client.print('\r');
  _rsp = "";
  _awaiting = true;
  _cmdSentAt = millis();
}

// Reads available bytes; returns true once the '>' prompt terminates a reply.
bool ObdClient::pumpResponse() {
  while (_client.available()) {
    char c = (char)_client.read();
    if (c == '>') { _awaiting = false; return true; }
    _rsp += c;
    if (_rsp.length() > 512) _rsp.remove(0, 256);   // guard runaway
  }
  return false;
}

void ObdClient::toFailed(const char* why) {
  (void)why;
  _client.stop();
  _awaiting = false;
  _state = ObdState::FAILED;
  _stamp = millis();
}

void ObdClient::requestDtcScan()  { _dtcScanReq = true;  _dtcReady = false; }
void ObdClient::requestDtcClear() { _dtcClearReq = true; _dtcReady = false; }

// ---------------------------------------------------------------------------
void ObdClient::loop() {
  switch (_state) {

    case ObdState::IDLE:
      break;                               // do nothing until begin() is called

    case ObdState::WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        _state = ObdState::TCP_CONNECTING;
        _stamp = millis();
      } else if (millis() - _stamp > OBD_WIFI_TIMEOUT) {
        toFailed("wifi timeout");
      }
      break;

    case ObdState::TCP_CONNECTING:
      _client.setTimeout(OBD_TCP_TIMEOUT / 1000);
      if (_client.connect(OBD_HOST, OBD_PORT)) {
        _state = ObdState::INITIALIZING;
        _initStep = 0;
        _awaiting = false;
      } else if (millis() - _stamp > OBD_TCP_TIMEOUT) {
        toFailed("tcp timeout");
      }
      break;

    case ObdState::INITIALIZING:
      if (!_client.connected()) { toFailed("dropped"); break; }
      if (!_awaiting) {
        if (_initStep >= INIT_COUNT) { _state = ObdState::READY; break; }
        sendCmd(INIT_CMDS[_initStep]);
      } else {
        // ATZ and the 0100 probe can be slow; be lenient and advance anyway.
        if (pumpResponse() || millis() - _cmdSentAt > OBD_RSP_TIMEOUT) {
          _awaiting = false;
          _initStep++;
        }
      }
      break;

    case ObdState::READY:
      if (!_client.connected()) { toFailed("dropped"); break; }
      if (_dtcScanReq)  { _dtcScanReq  = false; _awaiting = false; _state = ObdState::DTC_SCAN;  break; }
      if (_dtcClearReq) { _dtcClearReq = false; _awaiting = false; _state = ObdState::DTC_CLEAR; break; }
      handleReady();
      break;

    case ObdState::DTC_SCAN:
      if (!_client.connected()) { toFailed("dropped"); break; }
      if (!_awaiting) { sendCmd("03"); }
      else if (pumpResponse()) {
        parseDtc(_rsp);
        _dtcReady = true;
        _state = ObdState::READY;
      } else if (millis() - _cmdSentAt > OBD_RSP_TIMEOUT) {
        _dtcCodes.clear(); _dtcReady = true; _awaiting = false;
        _state = ObdState::READY;
      }
      break;

    case ObdState::DTC_CLEAR:
      if (!_client.connected()) { toFailed("dropped"); break; }
      if (!_awaiting) { sendCmd("04"); }
      else if (pumpResponse() || millis() - _cmdSentAt > OBD_RSP_TIMEOUT) {
        _awaiting = false;
        _dtcCodes.clear();
        _dtcReady = false;
        _state = ObdState::READY;
      }
      break;

    case ObdState::FAILED:
      if (millis() - _stamp > 3000) begin();   // auto-retry
      break;
  }
}

void ObdClient::handleReady() {
  if (!_awaiting) {
    if (millis() - _lastPoll < OBD_POLL_INTERVAL) return;
    sendCmd(POLL_PIDS[_pollIdx]);
  } else if (pumpResponse()) {
    parseLivePid(_rsp);
    _awaiting = false;
    _pollIdx = (_pollIdx + 1) % POLL_COUNT;
    _lastPoll = millis();
  } else if (millis() - _cmdSentAt > OBD_RSP_TIMEOUT) {
    _awaiting = false;
    _pollIdx = (_pollIdx + 1) % POLL_COUNT;   // skip stuck PID
    _lastPoll = millis();
  }
}

void ObdClient::parseLivePid(const String& raw) {
  String s = clean(raw);
  if (isNoise(s)) return;
  int i;
  if ((i = s.indexOf("410C")) >= 0) {                 // RPM
    uint8_t A = hexByte(s, i + 4), B = hexByte(s, i + 6);
    rpm = ((A << 8) | B) / 4;
  }
  if ((i = s.indexOf("4105")) >= 0)                    // coolant temp
    coolantC = (int)hexByte(s, i + 4) - 40;
  if ((i = s.indexOf("410D")) >= 0)                    // speed
    speedKmh = hexByte(s, i + 4);
  if ((i = s.indexOf("4111")) >= 0)                    // throttle
    throttle = hexByte(s, i + 4) * 100 / 255;
}

// Decode mode-03 response into SAE codes (e.g. "P0301").
void ObdClient::parseDtc(const String& raw) {
  _dtcCodes.clear();
  String s = clean(raw);
  if (isNoise(s)) return;
  int i = s.indexOf("43");
  if (i < 0) return;
  const char CAT[] = { 'P', 'C', 'B', 'U' };
  int p = i + 2;
  while (p + 4 <= (int)s.length()) {          // need 4 hex chars = one DTC
    uint8_t A = hexByte(s, p), B = hexByte(s, p + 2);
    p += 4;
    if (A == 0 && B == 0) continue;                    // padding / no code
    char buf[6];
    buf[0] = CAT[(A & 0xC0) >> 6];
    buf[1] = '0' + ((A & 0x30) >> 4);
    buf[2] = "0123456789ABCDEF"[A & 0x0F];
    buf[3] = "0123456789ABCDEF"[(B & 0xF0) >> 4];
    buf[4] = "0123456789ABCDEF"[B & 0x0F];
    buf[5] = 0;
    _dtcCodes.push_back(String(buf));
  }
}
