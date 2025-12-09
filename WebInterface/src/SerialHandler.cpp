// ----------------- SerialHandler.cpp -----------------

#include "SerialHandler.h"
#include <Wire.h>
#include "setpumpfrequency.h"

// ---- external driver state (for STATUS pretty-print) ----
extern bool bPumpState[3];
extern unsigned int nPumpVoltageByte[3];
extern unsigned int nFrequencyByte[3];
extern const unsigned int pumpChannels[3];

// ---- local state for I2C management ----
static int g_sda = 21;
static int g_scl = 22;
static uint32_t g_i2c_hz = 100000;

// Common pins list to brute-force in I2C_AUTOFIND
static const int CANDIDATE_PINS[] = {4,5,12,13,14,15,16,17,18,19,21,22,25,26,27,32,33};

// ---- line buffer for CLI ----
static String line;

// ----------------- helpers -----------------
static bool parsePumpId(const String& s, uint8_t& idx) {
  if (!s.length()) return false;
  char c = toupper(s.charAt(0));
  if (c=='A') idx=0; else if (c=='B') idx=1; else if (c=='C') idx=2; else return false;
  return true;
}
static bool parseNum(const String& s, uint32_t& out) {
  if (!s.length()) return false;
  String t=s; t.trim();
  bool k = t.endsWith("k") || t.endsWith("K");
  if (k) t.remove(t.length()-1);
  float v = t.toFloat();
  if (!(v>0)) return false;
  if (k) v *= 1000.0f;
  out = (uint32_t)lroundf(v);
  return true;
}

static void help() {
  Serial.println();
  Serial.println("Serial Commands:");
  Serial.println("  HELP");
  Serial.println("  SCAN                    (I2C 0x01..0x7E)");
  Serial.println("  SCANTCA                 (probe 0x70..0x77)");
  Serial.println("  DIAG                    (per pump: mux+0x59)");
  Serial.println("  INIT                    (initialize all pumps)");
  Serial.println("  ON   <A|B|C> <Vpp> <Hz>");
  Serial.println("  OFF  <A|B|C>");
  Serial.println("  SETA <A|B|C> <Vpp>");
  Serial.println("  SETF <A|B|C> <Hz>");
  Serial.println("  BUZZ <Vpp> <Hz> <ms>");
  Serial.println("  STATUS");
  Serial.println("  I2C_LINES               (read SDA/SCL levels 1=HIGH/0=LOW)");
  Serial.println("  I2C_AUTOFIND            (try common pin pairs; keep first with TCA ACK)");
  Serial.println("  I2C_SET <SDA> <SCL> [Hz] (re-init Wire at runtime)");
  Serial.println();
}

static void scan_all() {
  Serial.println("[SCAN] I2C 0x01..0x7E:");
  for (uint8_t a=1; a<127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission()==0) Serial.printf("  0x%02X\n", a);
  }
}
static void scan_tca() {
  Serial.println("[SCANTCA] 0x70..0x77:");
  for (uint8_t a=0x70; a<=0x77; ++a) {
    Wire.beginTransmission(a);
    uint8_t rc = Wire.endTransmission();
    Serial.printf("  0x%02X -> rc=%u%s\n", a, rc, rc? " (no ACK)":"");
  }
}
static void diag() {
  for (uint8_t i=0;i<3;i++){
    tcaSelect(i);
    delay(2);
    Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
    uint8_t rc = Wire.endTransmission();
    Serial.printf("[DIAG] %c: mux ch %u  drv(0x59)=%s\n",
      'A'+i, pumpChannels[i], (rc==0)?"OK":"FAIL");
  }
}
static void status() {
  Serial.printf("[I2C] SDA=%d SCL=%d @ %lu Hz\n", g_sda, g_scl, (unsigned long)g_i2c_hz);
  Serial.println("ID  On  Vpp    Hz     ampB freqB TCAch");
  for (uint8_t i=0;i<3;i++){
    float vpp = (AMP_V_MAX * (float)nPumpVoltageByte[i]) / 255.0f;
    float hz  = 7.8125f * (float)(nFrequencyByte[i] ? nFrequencyByte[i] : 1);
    Serial.printf("%c   %s  %6.1f %6.1f  %4u  %4u   %u\n",
      'A'+i, bPumpState[i]?"yes":"no", vpp, hz,
      nPumpVoltageByte[i], nFrequencyByte[i], pumpChannels[i]);
  }
}

// Re-init Wire with new pins/speed
static void i2c_reinit(int sda, int scl, uint32_t hz) {
  Wire.end(); delay(2);
  Wire.begin(sda, scl, hz);
  Wire.setTimeOut(50);
  g_sda = sda; g_scl = scl; g_i2c_hz = hz;
  Serial.printf("[I2C] Reinit SDA=%d SCL=%d @ %lu Hz\n", sda, scl, (unsigned long)hz);
}

static void do_scan_bus() {
  Serial.println("[SCAN] I2C 0x01..0x7E:");
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("  0x%02X\n", a);
  }
}

static void i2c_autofind() {
  Serial.println("[AUTO] Trying I2C pin pairs…");
  for (unsigned i=0;i<sizeof(CANDIDATE_PINS)/sizeof(int);++i){
    for (unsigned j=0;j<sizeof(CANDIDATE_PINS)/sizeof(int);++j){
      if (i==j) continue;
      int sda = CANDIDATE_PINS[i], scl = CANDIDATE_PINS[j];
      Wire.end(); delay(2);
      Wire.begin(sda, scl, g_i2c_hz);
      Wire.setTimeOut(30);
      bool any=false;
      for (uint8_t a=0x70;a<=0x77;++a){
        Wire.beginTransmission(a);
        if (Wire.endTransmission()==0){ any=true; break; }
      }
      Serial.printf("  Pins SDA=%d SCL=%d -> %s\n", sda, scl, any?"TCA? (ACK)":"no ACK");
      if (any) {
        // lock these in for the session
        g_sda = sda; g_scl = scl;
        Serial.println("[AUTO] Found ACK in 0x70..0x77 — keeping these pins.");
        do_scan_bus();
        return;
      }
    }
  }
  Serial.println("[AUTO] No ACK on any tried pair — check power, pull-ups, and /RESET.");
}

// ----------------- command handler -----------------
static void handle_cmd(String in) {
  in.trim(); if (!in.length()) return;
  String cmd,a,b,c;
  int sp=in.indexOf(' ');
  if (sp<0) cmd=in; else {
    cmd=in.substring(0,sp); String rest=in.substring(sp+1); rest.trim();
    int sp2=rest.indexOf(' ');
    if (sp2<0) a=rest; else {
      a=rest.substring(0,sp2); String r2=rest.substring(sp2+1); r2.trim();
      int sp3=r2.indexOf(' ');
      if (sp3<0) b=r2; else { b=r2.substring(0,sp3); c=r2.substring(sp3+1); c.trim(); }
    }
  }
  cmd.toUpperCase();

  if (cmd=="HELP")    { help(); return; }
  if (cmd=="SCAN")    { scan_all(); return; }
  if (cmd=="SCANTCA") { scan_tca(); return; }
  if (cmd=="DIAG")    { diag(); return; }
  if (cmd=="STATUS")  { status(); return; }

  if (cmd=="INIT") {
    for (uint8_t i=0;i<3;i++) Lowdriver_init(i);
    status();
    return;
  }

  if (cmd=="ON") {
    uint8_t idx; if (!parsePumpId(a, idx)) { Serial.println("Usage: ON <A|B|C> <Vpp> <Hz>"); return; }
    uint32_t vpp_u, hz_u;
    if (!parseNum(b, vpp_u) || !parseNum(c, hz_u)) { Serial.println("Usage: ON <A|B|C> <Vpp> <Hz>"); return; }
    Lowdriver_setvoltage(idx, vpp_u);
    Lowdriver_setfrequency(idx, hz_u);
    bPumpState[idx] = true;
    Serial.printf("[ON] %c\n", 'A'+idx);
    return;
  }

  if (cmd=="OFF") {
    uint8_t idx; if (!parsePumpId(a, idx)) { Serial.println("Usage: OFF <A|B|C>"); return; }
    Lowdriver_setvoltage(idx, 0);
    bPumpState[idx] = false;
    Serial.printf("[OFF] %c\n", 'A'+idx);
    return;
  }

  if (cmd=="SETA") {
    uint8_t idx; if (!parsePumpId(a, idx)) { Serial.println("Usage: SETA <A|B|C> <Vpp>"); return; }
    uint32_t vpp_u; if (!parseNum(b, vpp_u)) { Serial.println("Usage: SETA <A|B|C> <Vpp>"); return; }
    Lowdriver_setvoltage(idx, vpp_u);
    return;
  }

  if (cmd=="SETF") {
    uint8_t idx; if (!parsePumpId(a, idx)) { Serial.println("Usage: SETF <A|B|C> <Hz>"); return; }
    uint32_t hz_u; if (!parseNum(b, hz_u)) { Serial.println("Usage: SETF <A|B|C> <Hz>"); return; }
    Lowdriver_setfrequency(idx, hz_u);
    return;
  }

  if (cmd=="BUZZ") {
    uint32_t vpp_u, hz_u, ms_u;
    if (!parseNum(a, vpp_u) || !parseNum(b, hz_u) || !parseNum(c, ms_u)) {
      Serial.println("Usage: BUZZ <Vpp> <Hz> <ms>"); return;
    }
    for (uint8_t i=0;i<3;i++){
      Lowdriver_setvoltage(i, vpp_u);
      Lowdriver_setfrequency(i, hz_u);
      bPumpState[i] = true;
      delay(ms_u);
      Lowdriver_setvoltage(i, 0);
      bPumpState[i] = false;
      delay(150);
    }
    return;
  }

  // --- I2C diagnostics ---
  if (cmd=="I2C_LINES") {
    pinMode(g_sda, INPUT_PULLUP);
    pinMode(g_scl, INPUT_PULLUP);
    Serial.printf("[LINES] SDA=%d SCL=%d (1=HIGH,0=LOW)\n", digitalRead(g_sda), digitalRead(g_scl));
    return;
  }

// Usage: TCA_WR <mask>   e.g., TCA_WR 01 enables channel 0
if (cmd=="TCA_WR") {
  uint32_t m;
  if (!parseNum(a, m)) { Serial.println("Usage: TCA_WR <hex/dec mask>"); return; }
  Wire.beginTransmission(0x70);
  Wire.write((uint8_t)(m & 0xFF));
  uint8_t rc = Wire.endTransmission();
  Serial.printf("[TCA_WR] rc=%u\n", rc);
  return;
}

// Read back the TCA control register (returns current channel bitmap).
// Usage: TCA_RD
if (cmd=="TCA_RD") {
  Wire.beginTransmission(0x70);
  uint8_t rc = Wire.endTransmission(false);         // repeated start
  if (rc) { Serial.printf("[TCA_RD] addr NACK rc=%u\n", rc); return; }
  uint8_t val=0xFF;
  if (Wire.requestFrom(0x70, 1) == 1) val = Wire.read();
  Serial.printf("[TCA_RD] mask=0x%02X\n", val);
  return;
}

// Walk channels: enable each channel one at a time and probe 0x59 downstream.
if (cmd=="TCA_CHK") {
  for (uint8_t ch=0; ch<8; ++ch) {
    Wire.beginTransmission(0x70);
    Wire.write((uint8_t)(1u<<ch));
    uint8_t rc = Wire.endTransmission();
    if (rc) { Serial.printf("[TCA_CHK] ch%u: TCA set rc=%u\n", ch, rc); continue; }
    delay(2);
    Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
    rc = Wire.endTransmission();
    Serial.printf("[TCA_CHK] ch%u: drv(0x59) %s\n", ch, rc==0?"ACK":"NACK");
  }
  // Clear all channels
  Wire.beginTransmission(0x70); Wire.write((uint8_t)0x00); Wire.endTransmission();
  return;
}

  if (cmd=="I2C_AUTOFIND") {
    i2c_autofind();
    return;
  }

  if (cmd=="I2C_SET") {
    if (!a.length() || !b.length()) { Serial.println("Usage: I2C_SET <SDA> <SCL> [Hz]"); return; }
    int sda = a.toInt();
    int scl = b.toInt();
    uint32_t hz = g_i2c_hz;
    if (c.length()) hz = (uint32_t)strtoul(c.c_str(), nullptr, 10);
    i2c_reinit(sda, scl, hz);
    return;
  }

  Serial.println("Unknown. Type HELP.");
}

// ----------------- public entrypoints -----------------
void SerialHandler_begin(int sda_pin, int scl_pin, uint32_t i2c_hz) {
  g_sda = sda_pin; g_scl = scl_pin; g_i2c_hz = i2c_hz;
  Serial.println("\n[BOOT] Serial CLI ready. Type HELP.");
  Serial.printf("[I2C] SDA=%d SCL=%d @ %lu Hz\n", g_sda, g_scl, (unsigned long)g_i2c_hz);
  Serial.print("> ");
}

void SerialHandler_poll() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch=='\r') continue;
    if (ch=='\n') { Serial.println(); handle_cmd(line); line=""; Serial.print("> "); }
    else if (ch==0x08 || ch==0x7F) { if (line.length()) { line.remove(line.length()-1); Serial.print("\b \b"); } }
    else { if (line.length()<200) { line+=ch; Serial.print(ch); } }
  }
}


