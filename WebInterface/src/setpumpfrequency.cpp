// ----------------- setpumpfrequency.cpp -----------------

#include "setpumpfrequency.h"
#include <Wire.h>

// ── TCA channels (A,B,C) ────────────────────────────────────────
// Wiring has Pump A on channel 6 and Pump C on channel 2, so map accordingly.
const unsigned int pumpChannels[3] = { 6, 4, 2 };

// ── State ───────────────────────────────────────────────────────
bool bPumpState[3] = { false, false, false };
unsigned int nPumpVoltageByte[3] = { 0, 0, 0 };
unsigned int nFrequencyByte[3]   = { 0x0C, 0x0C, 0x0C }; // ~94–100 Hz

// ── Helpers ─────────────────────────────────────────────────────
static inline uint8_t clamp_u8(int v, int lo, int hi){
  if(v<lo) v=lo; if(v>hi) v=hi; return (uint8_t)v;
}
static inline uint8_t vpp_to_byte(unsigned int vpp){
  if((int)vpp < 0) vpp = 0;
  if(vpp > (unsigned)AMP_V_MAX) vpp = (unsigned)AMP_V_MAX;
  float s = (float)vpp * 255.0f / AMP_V_MAX;
  return clamp_u8((int)lroundf(s), 0, 255);
}
static inline uint8_t hz_to_code(unsigned int hz){
  if((int)hz < PUMP_HZ_MIN) hz = PUMP_HZ_MIN;
  if(hz > (unsigned)PUMP_HZ_MAX) hz = PUMP_HZ_MAX;
  float c = (float)hz / 7.8125f;        // ~7.8125 Hz/LSB
  int ci = (int)lroundf(c);
  return clamp_u8(ci, 1, 255);          // 0 invalid
}

// ── Page/select ─────────────────────────────────────────────────
void tcaSelect(unsigned int pump){
  if (pump > 2) return;
  unsigned int channel = pumpChannels[pump];
  Wire.beginTransmission(TCAADDR);
  Wire.write((uint8_t)(1u << channel));
  Wire.endTransmission();
}
void selectControlRegisters(){
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write(0xFF); // page reg
  Wire.write(0x00); // control
  Wire.endTransmission();
}
void selectMemoryRegisters(){
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write(0xFF); // page reg
  Wire.write(0x01); // memory
  Wire.endTransmission();
}

// ── Primitives: GO + memory block ───────────────────────────────
static inline void low_go(bool go){
  selectControlRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write(0x02);                           // SYSCTL (GO bit here)
  Wire.write((uint8_t)(go ? 0x01 : 0x00));
  Wire.endTransmission();
}
static void low_write_mem_block(uint8_t ampB, uint8_t freqB, uint8_t cycles=100, uint8_t envelope=0x00){
  selectMemoryRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write(0x00);         // mem base
  // header (simple synthesis window)
  Wire.write(0x05);         // header size - 1
  Wire.write(0x80);         // start hi
  Wire.write(0x06);         // start lo
  Wire.write(0x00);         // stop hi
  Wire.write(0x09);         // stop lo
  Wire.write(0x00);         // repeat = 0 (infinite)
  // data payload:
  Wire.write(ampB);                           // amplitude
  Wire.write(freqB ? freqB : (uint8_t)1);     // frequency code
  Wire.write(cycles);                         // cycles (not critical in repeat mode)
  Wire.write(envelope);                       // envelope off
  Wire.endTransmission();
}

static bool selectAndProbe(unsigned int pump){
  tcaSelect(pump);
  delay(2);
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  uint8_t rc = Wire.endTransmission(); // 0 = ACK
  if (rc != 0) {
    Serial.printf("[I2C] Pump %c (ch%u) NACK from 0x59 (rc=%u)\n", 'A'+pump, pumpChannels[pump], rc);
    return false;
  }
  return true;
}

// ── Public API ──────────────────────────────────────────────────
void Lowdriver_init(unsigned int pump){
  if (!selectAndProbe(pump)) {
    Serial.printf("[INIT] Pump %c FAIL: 0x59 not responding\n", 'A'+pump);
    bPumpState[pump] = false;
    nPumpVoltageByte[pump] = 0;
    nFrequencyByte[pump]   = 0x0C;
    return;
  }

  // Minimal sane defaults for gain/standby/sequence
  selectControlRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write(0x01);  // control block
  Wire.write(0x03);  // gain (use 0x03 to match your earlier working code)
  Wire.write(0x00);  // standby off
  Wire.write(0x01);  // waveform #1
  Wire.write(0x00);  // end
  Wire.endTransmission();

  // Safe default: 0 Vpp @ ~100 Hz
  nPumpVoltageByte[pump] = 0;
  nFrequencyByte[pump]   = 0x0C;

  // STOP -> write block -> GO
  selectControlRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS); Wire.write(0x02); Wire.write((uint8_t)0x00); Wire.endTransmission();
  selectMemoryRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x05);
  Wire.write((uint8_t)0x80); Wire.write((uint8_t)0x06);
  Wire.write((uint8_t)0x00); Wire.write((uint8_t)0x09);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);                 // amp=0
  Wire.write((uint8_t)nFrequencyByte[pump]); // freq code
  Wire.write((uint8_t)100);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  delay(6);
  selectControlRegisters();
  Wire.beginTransmission(I2C_LOWDRIVER_ADRESS); Wire.write(0x02); Wire.write((uint8_t)0x01); Wire.endTransmission();

  bPumpState[pump] = false;
  Serial.printf("[INIT] Pump %c OK (amp=%u, freqCode=%u)\n", 'A'+pump, nPumpVoltageByte[pump], nFrequencyByte[pump]);
}


void Lowdriver_setfrequency(unsigned int pump, unsigned int _frequency){
  if (!selectAndProbe(pump)) return;

  uint8_t code = hz_to_code(_frequency);
  nFrequencyByte[pump] = code;

  low_go(false);
  low_write_mem_block((uint8_t)nPumpVoltageByte[pump], (uint8_t)nFrequencyByte[pump], 100, 0x00);
  delay(4);
  low_go(true);

  Serial.printf("[SETF] %c -> code=%u (~%.1f Hz)\n", 'A'+pump, code, 7.8125f * code);
}

void Lowdriver_setvoltage(unsigned int pump, unsigned int _voltage){
  if (!selectAndProbe(pump)) return;

  uint8_t ampB = vpp_to_byte(_voltage);
  nPumpVoltageByte[pump] = ampB;

  low_go(false);
  low_write_mem_block((uint8_t)nPumpVoltageByte[pump], (uint8_t)nFrequencyByte[pump], 100, 0x00);
  delay(4);
  low_go(true);

  Serial.printf("[SETA] %c -> byte=%u (~%.1f Vpp)\n", 'A'+pump, ampB, (AMP_V_MAX * ampB) / 255.0f);
}

void stopPumps(){
  for (int i=0;i<3;i++){
    tcaSelect(i);
    delay(2);
    low_go(false);
    low_write_mem_block(0x00, (uint8_t)nFrequencyByte[i], 100, 0x00);
    delay(2);
    low_go(true);
    bPumpState[i] = false;
    nPumpVoltageByte[i] = 0;
  }
  Serial.println("[STOP] all pumps off");
}
