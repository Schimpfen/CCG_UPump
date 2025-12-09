// -- setpumpfrequency.h ---------------------------------

#pragma once
#include <Arduino.h>

// ── I2C addresses ───────────────────────────────────────────────
#define TCAADDR               0x70
#define I2C_LOWDRIVER_ADRESS  0x59

// ── TCA channels for pumps A,B,C ────────────────────────────────
extern const unsigned int pumpChannels[3];

// ── Public status ───────────────────────────────────────────────
extern bool bPumpState[3];            // on/off (logical)
extern unsigned int nPumpVoltageByte[3]; // 0..255
extern unsigned int nFrequencyByte[3];   // 1..255

// ── Bounds & defaults ───────────────────────────────────────────
static constexpr int   PUMP_HZ_MIN = 8;
static constexpr int   PUMP_HZ_MAX = 2000;
static constexpr float AMP_V_MIN   = 0.0f;
static constexpr float AMP_V_MAX   = 150.0f;

// ── Lowlevel helpers ────────────────────────────────────────────
void tcaSelect(unsigned int pump);
void selectControlRegisters();
void selectMemoryRegisters();

// ── Driver API (per-pump) ───────────────────────────────────────
void Lowdriver_init(unsigned int pump);
void Lowdriver_setfrequency(unsigned int pump, unsigned int _frequency);
void Lowdriver_setvoltage(unsigned int pump, unsigned int _voltage);
void stopPumps();

// (optional) utility for your own logs
inline void setPumpParameters(const char* pumpId, float amp, float freq, float flow) {
  Serial.printf("[FUNC] Setting Pump %s -> Amp=%.2f, Freq=%.2f, Flow=%.2f\n",
                pumpId, amp, freq, flow);
}
