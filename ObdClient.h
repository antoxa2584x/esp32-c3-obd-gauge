// ============================================================================
//  ObdClient.h  --  Non-blocking ELM327-over-WiFi client.
//
//  Single-core ESP32-C3: this MUST never block. Call obd.loop() every
//  iteration of the Arduino loop(); it advances an internal state machine
//  and updates cached live values that the UI reads.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>

enum class ObdState {
  IDLE,             // waiting for a network to be chosen (begin() not called yet)
  WIFI_CONNECTING,
  TCP_CONNECTING,
  INITIALIZING,     // sending ATZ/ATE0/... handshake
  READY,            // polling live PIDs
  DTC_SCAN,         // one-shot: reading stored trouble codes
  DTC_CLEAR,        // one-shot: clearing trouble codes
  FAILED            // will auto-retry
};

class ObdClient {
 public:
  void begin();
  void loop();

  ObdState state() const { return _state; }
  const char* stateText() const;
  bool ready() const { return _state == ObdState::READY; }

  // Cached live values (native OBD units)
  int   rpm       = 0;
  int   coolantC  = 0;
  int   speedKmh  = 0;
  int   throttle  = 0;   // %
  float voltage   = 0.0f;   // adapter battery voltage (V), from ATRV

  // DTC (Diagnostic Trouble Code) support
  void  requestDtcScan();
  void  requestDtcClear();
  bool  dtcBusy() const { return _dtcScanReq || _dtcClearReq ||
                                 _state == ObdState::DTC_SCAN ||
                                 _state == ObdState::DTC_CLEAR; }
  bool  dtcReady() const { return _dtcReady; }        // results available
  const std::vector<String>& dtcCodes() const { return _dtcCodes; }

 private:
  WiFiClient _client;
  ObdState   _state = ObdState::IDLE;

  // command / response plumbing
  String   _rsp;                 // accumulating response text
  uint32_t _cmdSentAt = 0;
  bool     _awaiting  = false;   // true while waiting for '>' prompt
  uint32_t _stamp     = 0;       // generic timer for the current state

  // init handshake progress
  uint8_t  _initStep  = 0;
  bool     _isCan     = true;    // CAN inserts a DTC-count byte in mode 03 (ATDPN)

  // live poll round-robin
  uint8_t  _pollIdx   = 0;
  uint32_t _lastPoll  = 0;

  // dtc flags
  bool _dtcScanReq  = false;
  bool _dtcClearReq = false;
  bool _dtcReady    = false;
  bool _dtcProbed   = false;   // re-queried ATDPN at scan start (framing depends on it)
  uint8_t _dtcTry   = 0;       // mode-03 attempts (slow ISO 9141-2 often NO-DATAs first)
  std::vector<String> _dtcCodes;

  void   sendCmd(const char* cmd);
  bool   pumpResponse();                 // returns true when '>' received
  void   handleReady();
  void   parseLivePid(const String& r);
  void   parseVoltage(const String& r);
  void   parseDtc(const String& r);
  void   detectProtocol(const String& r);   // sets _isCan from ATDPN reply
  void   toFailed(const char* why);
};

extern ObdClient obd;
