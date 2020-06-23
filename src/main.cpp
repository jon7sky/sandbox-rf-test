#include <Arduino.h>
#include <EEPROM.h>

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>

//Libraries for OLED Display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "rf_lora.hpp"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define LINE(x) ((x) * 8)

// Hack for TTGO LORA32 V2 -- Although you can build for that board, the pins_arduino.h file is missing.
// So the hack is to configure the build to use the V1 board and override the OLED pins here.
#ifdef OLED_SDA_OVERRIDE
#undef OLED_SDA
#define OLED_SDA OLED_SDA_OVERRIDE
#undef OLED_SCL
#define OLED_SCL OLED_SCL_OVERRIDE
#endif

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

RfLora rf;

void setup() {
  char txt[32];
  unsigned char txMode;

#ifdef MODE_CHANGE_BUTTON_PIN
  pinMode(MODE_CHANGE_BUTTON_PIN, INPUT);
#endif

  EEPROM.begin(4);

  //reset OLED display via software
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  //initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println("SSD1306 allocation failed");
    while (true);
  }
  
  //initialize Serial Monitor
  Serial.begin(115200);
  Serial.println(txt);

  rf.setup();

  while (true) {
    int cfgIdx;

    txMode = EEPROM.read(0) == 't' ? true : false;
    cfgIdx = EEPROM.read(1);
    if (cfgIdx > rf.cfgCnt) {
      cfgIdx = 0;
    }
    rf.setTxMode(txMode);
    if (cfgIdx == rf.cfgCnt) {
      rf.setSeqMode(true);
    } else {
      rf.setSeqMode(false);
      rf.setCfgIdx(cfgIdx);
    }

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0,LINE(0));
    display.print(rf.desc());
    display.setCursor(0,10);
    display.println(cfgIdx >= rf.cfgCnt ? "Sequence" : rf.cfg[cfgIdx].desc);

    display.setCursor(0, LINE(3));
#ifdef MODE_CHANGE_BUTTON_PIN
    display.println("Press PGM button");
#else
    display.println("Press RST button");
#endif
    display.setCursor(0, LINE(4));
    display.println("to change TX/RX mode");
    display.display();

#ifndef MODE_CHANGE_BUTTON_PIN
    int origCfgIdx = cfgIdx;
    int origTxMode = txMode;
    // Change to the next mode
    if (++cfgIdx > rf.cfgCnt) {
      txMode = !txMode;
      cfgIdx = 0;
    }
    EEPROM.write(0, txMode ? 't' : 'r');
    EEPROM.write(1, cfgIdx);
    EEPROM.commit();
    delay(2000);
    EEPROM.write(0, origTxMode ? 't' : 'r');
    EEPROM.write(1, origCfgIdx);
    EEPROM.commit();
    break;
#else // MODE_CHANGE_BUTTON_PIN
    {
      int cnt;

      for (cnt = 200; cnt > 0; cnt--) {
        if (digitalRead(MODE_CHANGE_BUTTON_PIN) == 0) {
          // Change to the next mode
          if (++cfgIdx > rf.cfgCnt) {
            txMode = !txMode;
            cfgIdx = 0;
          }
          EEPROM.write(0, txMode ? 't' : 'r');
          EEPROM.write(1, cfgIdx);
          EEPROM.commit();
          // Wait for button to be released.
          do {
            delay(10);
          } while (digitalRead(MODE_CHANGE_BUTTON_PIN) == 0);
          break;
        }
        delay(10);
      }

      if (cnt == 0) {
        break;
      }
    }
#endif //MODE_CHANGE_BUTTON_PIN
  }
}

void loopFixedMode(void) {
  char msg[32];
  char txt[32];
  static int testNum = 0;
  int cfgIdx = rf.getCfgIdx();
  int rssi;
  int snr;

  display.clearDisplay();
  display.setCursor(0, LINE(0));
  display.print(rf.desc());
  display.setCursor(0, LINE(1));
  display.print(rf.cfg[cfgIdx].desc);
  display.display();

  if (rf.isTxMode()) {
    sprintf(msg, "HelloThere-%d", testNum++);
    sprintf(txt, "TX: %s", msg);
    display.setCursor(0, LINE(3));
    display.print(txt);
    display.display();
    rf.tx(msg);
    display.setCursor(0, LINE(4));
    display.print("Sent");
    display.display();
    delay(10000);     
  }
  if (rf.isRxMode()) {
    if (rf.rxMsgReady()) {
      String msg = rf.rx();
      if (msg.length() >= rf.txMsgLen) {
        rssi = rf.getRssi();
        snr = rf.getSnr();
        Serial.println(msg);
        sprintf(txt, "RX:   %s", msg.c_str());
        display.setCursor(0, LINE(3));
        display.print(txt);
        sprintf(txt, "RSSI: %ddB", rssi);
        display.setCursor(0, LINE(4));
        display.print(txt);
        sprintf(txt, "SNR:  %ddB", snr);
        display.setCursor(0, LINE(5));
        display.print(txt);            
        display.display();
        delay(9000);
      }
    }
  }
}

void loopSeqMode(void) {
  static bool firstTime = true;
  static unsigned long baseMillis = 0;
  int tMs;
  int cfgIdx;
  static int lastCfgIdx = -1;
  char msg[32];
  char txt[32];
  static int testNum = 0;
  static int rssi[20] = {0, };
  static int snr[20] = {0, };
  static int quality[20] = {0, };
  static int msgRxCnt[20] = {0, };
  static bool gotMsg = false;
  const char baseMsg[] = "HelloThere";

  if (firstTime) {
    if (rf.isTxMode()) {
      baseMillis = millis();
      firstTime = false;
    } else {
      display.clearDisplay();
      display.setCursor(0, LINE(0));
      display.print(rf.desc());
      display.setCursor(0, LINE(2));
      display.print("Syncing...");
      display.display();
      rf.setCfgIdx(0);
      while (!rf.rxMsgReady());
      baseMillis = millis() - 1000;
      String msg = rf.rx();
      Serial.println(msg);
      firstTime = false;
    }
  }

  tMs = (millis() - baseMillis) % rf.testPeriodMs;
  cfgIdx = tMs / rf.testDurationMs;

  // Do this stuff if we moved on to the next config index.
  if (cfgIdx != lastCfgIdx) {
    display.clearDisplay();
    display.setCursor(0, LINE(0));
    display.print(rf.desc());
    Serial.print("cfgIdx: "); Serial.println(cfgIdx);

    // Display table if receive mode
    if (rf.isRxMode()) {
      for (int i = 0; i < rf.cfgCnt; i++) {
        //sprintf(txt, "%s", rf.cfg[i].name);
        if (i == cfgIdx) {
          sprintf(txt, "%s *** ***", rf.cfg[i].name);
        } else if (rssi[i] < 0) {
          // sprintf(&txt[2], "%4d %3d", (msgRxCnt[i] > 0 ? rssi[i] / msgRxCnt[i] : 0), quality[i] * (100 / (rf.txCnt * rf.txMsgLen)));  // RSSI, Quality
          sprintf(txt, "%s%4d %3d", rf.cfg[i].name, (msgRxCnt[i] > 0 ? snr[i] / msgRxCnt[i] : 0),  quality[i] * (100 / (rf.txCnt * rf.txMsgLen)));  // SNR, Quality
        } else {
          sprintf(txt, "%s --- ---", rf.cfg[i].name);
        }
        display.setCursor((i % rf.numColumns) * (128 / rf.numColumns), LINE(3 + (i / rf.numColumns)));
        display.print(txt);
      }
      rssi[cfgIdx] = 0;
      quality[cfgIdx] = 0;
      msgRxCnt[cfgIdx] = 0;
    }
    display.display();
    gotMsg = false;

    // Display configuration parameters.
    if (cfgIdx < rf.cfgCnt) {
      display.setCursor(0, LINE(1));
      display.print(rf.cfg[cfgIdx].desc);
      display.display();

      rf.setCfgIdx(cfgIdx);

      // If transmit mode, send a message X times.
      if (rf.isTxMode()) {
        delay(500);
        // Send some dummy messages.
        for (int i = 0; i < 1; i++) {
          rf.tx("X");
          delay(100);
        }
        // Send our test messages.
        sprintf(msg, "%s-%d", baseMsg, testNum++);
        sprintf(txt, "%s", msg);
        display.setCursor(0, LINE(2));
        display.print(txt);
        sprintf(txt, "TX:");
        display.setCursor(0, LINE(3));
        display.print(txt);        
        display.display();
        for (int i = 0; i < rf.txCnt; i++) {
          rf.tx(msg);
          display.setCursor(16 + (i * 3), LINE(3));
          display.print("|");
          display.display();
          delay(100);
        }
        //display.setCursor(0, LINE(3));
        //display.print("Sent");
        //display.display();
      }
    } else {
      if (rf.isRxMode()) {
        int totRssi = 0;
        int totSnr = 0;
        int totMsgRxCnt = 0;
        for (int i = 0; i < rf.cfgCnt; i++) {
          totRssi += rssi[i];
          totSnr += snr[i];
          totMsgRxCnt += msgRxCnt[i];
        }
        sprintf(txt, "Avg RSSI: %ddB", totMsgRxCnt > 0 ? totRssi / totMsgRxCnt : 0);
        display.setCursor(0, LINE(1));
        display.print(txt);
        sprintf(txt, "Avg SNR:  %ddB", totMsgRxCnt > 0 ? totSnr / totMsgRxCnt : 0);
        display.setCursor(0, LINE(2));
        display.print(txt);        
        display.display();
      }
    }
    lastCfgIdx = cfgIdx;
  }

  // If receive mode, process any incoming messages.
  if (rf.isRxMode()) {
    if (rf.rxMsgReady()) {
      String msg = rf.rx();
      const char *msgp = msg.c_str();
      if (msg.length() >= rf.txMsgLen) {
        for (int i = 0; i < rf.txMsgLen; i++) {
          if (msgp[i] == baseMsg[i]) {
            quality[cfgIdx]++;
          }
        }
        display.setCursor(84 + (msgRxCnt[cfgIdx] * 3), LINE(2));
        display.print("|");
        display.display();
        rssi[cfgIdx] += rf.getRssi();
        snr[cfgIdx] += rf.getSnr();
        msgRxCnt[cfgIdx]++;
        Serial.println(msg);
        if (!gotMsg) {
          sprintf(txt, "RSSI: %4d RX:", rssi[cfgIdx]);
          display.setCursor(0, LINE(2));
          display.print(txt);
          display.display();
          gotMsg = true;
        }
      }
    }
  }
}

void loop(void) {
  if (rf.isSeqMode()) {
    loopSeqMode();
  } else {
    loopFixedMode();
  }
}