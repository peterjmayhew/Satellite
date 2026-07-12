#include <Arduino.h>
#include <string.h>
#include <math.h>
#if USE_UBX

    #include "gps_ubx.h"
    #include "timesync.h"
    #include "gnss_extra.h"   // skySats[], skyAddSat(), skyReset()

    // Globals defined in main.cpp
    extern float altitudeM, hdop;
    extern float accH_m, accV_m, vspeed_ms, hae_m, geoidSep_m, speedAcc_ms, headAcc_deg, vdop, pdop;
    extern int fixType;
    extern bool sbasUsed;
    extern int rfJamState, rfJamInd, rfAgcPct, rfAntStatus, rfNoise, spoofState;
    extern uint32_t ttffMs;
    extern uint32_t odoDist, odoTotal;
    extern float errMajorM, errMinorM, errOrientDeg;
    extern int sbasSys, sbasPrn, sbasCnt;                 // NAV-SBAS augmentation status
    extern String rxModule, rxFw, rxProto, rxGnss;        // MON-VER receiver identity
    extern int uartTxPct, uartTxPeak, uartRxPct, uartOvf; // MON-COMMS UART link load
    extern uint8_t spectrum[256];                         // MON-SPAN power bins
    extern bool specValid;
    extern uint32_t specSpanHz, specResHz, specCenterHz;
    extern int specPga;
    extern int geoState, geoStatus;                       // NAV-GEOFENCE status

    // Link diagnostics (defined in main.cpp)
    extern uint32_t diagBytesRx, diagNmeaCount, diagUbxSync, diagUbxFrames, diagUbxBadCk, diagCfgSends, diagLastByteMs;
    extern uint32_t diagNavPvt, diagNavDop, diagNavStatus, diagNavCov, diagNavSat, diagMonRf;
    extern uint8_t diagRing[64];
    extern uint8_t diagRingIdx;

    // ---- Config ----
    // 5 Hz keeps NAV-PVT+NAV-DOP+NAV-SAT within the 38400-baud UART budget.
    static const uint16_t NAV_RATE_HZ = 5;
    static const bool ENABLE_NAV_SAT = true;

    // ---------------- UBX framing ----------------
    static const uint8_t SYNC1 = 0xB5;
    static const uint8_t SYNC2 = 0x62;

    static uint8_t cls_ = 0, id_ = 0;
    static uint16_t len_ = 0;
    static uint16_t payloadPos_ = 0;
    static uint8_t ckA_ = 0, ckB_ = 0;
    static uint8_t rxCkA_ = 0, rxCkB_ = 0;

    static enum { WAIT1, WAIT2, C, I, L1, L2, PAY, CKA, CKB } st_ = WAIT1;
    static uint8_t payload_[512];

    static inline void ckUpdate(uint8_t b){ ckA_ = ckA_ + b; ckB_ = ckB_ + ckA_; }
    static inline uint16_t u16(const uint8_t *p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
    static inline uint32_t u32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    static inline int32_t  i32(const uint8_t *p){ return (int32_t)u32(p); }
    static inline float    f32(const uint8_t *p){ float f; memcpy(&f, p, 4); return f; } // LE IEEE-754
    static inline String two(int v){ return (v < 10) ? "0" + String(v) : String(v); }

    static void setUtcStrings(int year,int mon,int day,int hr,int mi,int se){
    timeUTC = two(hr)+":"+two(mi)+":"+two(se);
    dateUTC = two(day)+"/"+two(mon)+"/"+String(year);
    }

    // --------------- UBX send/config ---------------
    static void sendUbx(HardwareSerial &gps, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t n) {
    uint8_t a=0,b=0;
    auto upd=[&](uint8_t x){ a=a+x; b=b+a; };

    gps.write(SYNC1); gps.write(SYNC2);
    gps.write(cls); upd(cls);
    gps.write(id);  upd(id);

    uint8_t l1=(uint8_t)(n & 0xFF), l2=(uint8_t)(n >> 8);
    gps.write(l1); upd(l1);
    gps.write(l2); upd(l2);

    for (uint16_t i=0;i<n;i++){ gps.write(pl[i]); upd(pl[i]); }

    gps.write(a); gps.write(b);
    }

    static void cfgMsgRate(HardwareSerial &gps, uint8_t msgClass, uint8_t msgId, uint8_t rateUart1) {
    uint8_t p[8] = { msgClass, msgId, 0, rateUart1, 0, 0, 0, 0 };
    sendUbx(gps, 0x06, 0x01, p, 8);
    }

    // CFG-VALSET a single U1 (byte) configuration key into RAM (M9 config interface).
    static void cfgValSetU1(HardwareSerial &gps, uint32_t key, uint8_t val) {
    uint8_t p[9] = {
        0x00,        // version
        0x01,        // layer = RAM
        0x00, 0x00,  // reserved
        (uint8_t)(key & 0xFF), (uint8_t)((key >> 8) & 0xFF),
        (uint8_t)((key >> 16) & 0xFF), (uint8_t)((key >> 24) & 0xFF),
        val
    };
    sendUbx(gps, 0x06, 0x8A, p, 9);
    }

    static void cfgRate(HardwareSerial &gps, uint16_t hz) {
    if (hz < 1) hz = 1;
    if (hz > 25) hz = 25;

    uint16_t measRate = (uint16_t)(1000 / hz);
    uint8_t p[6] = {
        (uint8_t)(measRate & 0xFF), (uint8_t)(measRate >> 8),
        1, 0,   // navRate=1
        0, 0    // timeRef=UTC
    };
    sendUbx(gps, 0x06, 0x08, p, 6);
    }

    static void configureForUbx(HardwareSerial &gps) {
    diagCfgSends++;
    cfgRate(gps, NAV_RATE_HZ);

    // Enable NAV-PVT (01 07) every epoch
    cfgMsgRate(gps, 0x01, 0x07, 1);

    // Enable NAV-DOP (01 04) every epoch — full DOP suite (real HDOP/VDOP/PDOP)
    cfgMsgRate(gps, 0x01, 0x04, 1);

    // Enable NAV-SAT (01 35) every 5th epoch (~1 Hz at 5 Hz base) to save bandwidth
    if (ENABLE_NAV_SAT) cfgMsgRate(gps, 0x01, 0x35, 5);

    // Enable NAV-STATUS (01 03) ~1 Hz — time-to-first-fix + spoofing detection
    cfgMsgRate(gps, 0x01, 0x03, 5);

    // Enable NAV-COV (01 36) ~1 Hz — position covariance -> horizontal error ellipse
    cfgMsgRate(gps, 0x01, 0x36, 5);

    // Enable MON-RF (0A 38) every ~5 s — RF interference/jamming + antenna health
    cfgMsgRate(gps, 0x0A, 0x38, 25);

    // Enable MON-COMMS (0A 36) every ~5 s — UART port buffer load / overruns
    // (the "UART link load" gauge). Slow-changing, so a low rate is plenty.
    cfgMsgRate(gps, 0x0A, 0x36, 25);

    // Enable MON-SPAN (0A 31) every ~5 s — live RF spectrum (256 bins). It is a
    // ~276-byte frame, so keep the rate low to stay within the 38400-baud budget.
    cfgMsgRate(gps, 0x0A, 0x31, 25);

    // Enable NAV-GEOFENCE (01 39) every epoch — cheap 10-byte status. Emits
    // meaningfully only once a fence is configured (done at first fix).
    cfgMsgRate(gps, 0x01, 0x39, 1);

    // Ensure SBAS is enabled (EGNOS over Europe) — usually default-on; make it explicit.
    // CFG-SIGNAL-SBAS_ENA = 0x10310020. Non-fatal if the module ignores it.
    cfgValSetU1(gps, 0x10310020, 1);

    // Enable the RF interference monitor so MON-RF jammingState is populated.
    // CFG-ITFM-ENABLE = 0x1041000d. Non-fatal if the module ignores it.
    cfgValSetU1(gps, 0x1041000d, 1);

    // AssistNow Autonomous: the receiver predicts satellite orbits on-chip (no
    // network needed) so cold starts after a power cycle acquire much faster.
    // CFG-ANA-USE_ANA = 0x10230001. Non-fatal if the module ignores it.
    cfgValSetU1(gps, 0x10230001, 1);

    // On-chip odometer: hardware-filtered cumulative ground distance, accurate at
    // low speed and immune to GPS jitter. Enable it, pick a walking/cycling-ish
    // profile, and emit NAV-ODO (01 09) ~1 Hz.
    cfgValSetU1(gps, 0x10220001, 1);   // CFG-ODO-USE_ODO = 1
    cfgValSetU1(gps, 0x20220005, 0);   // CFG-ODO-PROFILE = 0 (running) — low-speed tuned
    cfgMsgRate(gps, 0x01, 0x09, 5);    // NAV-ODO every 5th epoch (~1 Hz)

    // Zero the trip odometer at boot so NAV-ODO "distance" reads distance-since
    // -power-on; UBX-NAV-RESETODO (01 10) takes an empty payload.
    sendUbx(gps, 0x01, 0x10, nullptr, 0);

    // Enable NAV-SBAS (01 32) ~1 Hz — real augmentation status: which SBAS system
    // (EGNOS over the UK), the GEO PRN in use, and how many SVs carry corrections.
    cfgMsgRate(gps, 0x01, 0x32, 5);

    // Poll MON-VER (0A 04) once — module type, firmware (SPG x.xx), protocol
    // version and the enabled GNSS list. Static, so poll rather than stream.
    sendUbx(gps, 0x0A, 0x04, nullptr, 0);

    // Disable common NMEA sentences (class 0xF0)
    cfgMsgRate(gps, 0xF0, 0x00, 0); // GGA
    cfgMsgRate(gps, 0xF0, 0x03, 0); // GSV
    cfgMsgRate(gps, 0xF0, 0x04, 0); // RMC
    cfgMsgRate(gps, 0xF0, 0x05, 0); // VTG
    cfgMsgRate(gps, 0xF0, 0x02, 0); // GSA
    cfgMsgRate(gps, 0xF0, 0x08, 0); // ZDA
    }

    // --------------- UBX handlers ---------------
    // NAV-PVT (01 07) length 92 — the primary fix message (position, velocity,
    // accuracy estimates, time). Byte offsets per the u-blox M9 interface desc.
    static void handleNavPvt(const uint8_t *p, uint16_t n) {
    if (n < 92) return;

    uint16_t year = u16(p + 4);
    uint8_t month = p[6], day = p[7], hour = p[8], minute = p[9], sec = p[10];
    uint8_t valid = p[11];

    uint8_t ftype = p[20];
    uint8_t flags = p[21];
    uint8_t numSV = p[23];

    int32_t  lon_i        = i32(p + 24);
    int32_t  lat_i        = i32(p + 28);
    int32_t  hEll_mm      = i32(p + 32);   // height above WGS84 ellipsoid (mm)
    int32_t  hMSL_mm      = i32(p + 36);   // height above mean sea level (mm)
    uint32_t hAcc_mm      = u32(p + 40);   // horizontal accuracy estimate (mm)
    uint32_t vAcc_mm      = u32(p + 44);   // vertical accuracy estimate (mm)
    int32_t  velD_mms     = i32(p + 56);   // NED down velocity (mm/s)
    int32_t  gSpeed_mms   = i32(p + 60);   // 2D ground speed (mm/s)
    int32_t  headMot_1e5  = i32(p + 64);   // heading of motion (1e-5 deg)
    uint32_t sAcc_mms     = u32(p + 68);   // speed accuracy estimate (mm/s)
    uint32_t headAcc_1e5  = u32(p + 72);   // heading accuracy estimate (1e-5 deg)
    uint16_t pDOP_e2      = u16(p + 76);   // position DOP (0.01 units)

    bool validTime = (valid & 0x03) == 0x03;
    bool fixOk = (flags & 0x01) != 0;

    fixType         = (int)ftype;
    sbasUsed        = (flags & 0x02) != 0;   // diffSoln: differential/SBAS corrections applied
    satellitesSCRN1 = (int)numSV;
    gpsFix = fixOk && (ftype >= 2);

    if (gpsFix) {
        longitude   = (float)lon_i * 1e-7f;
        latitude    = (float)lat_i * 1e-7f;
        altitudeM   = (float)hMSL_mm / 1000.0f;
        hae_m       = (float)hEll_mm / 1000.0f;
        geoidSep_m  = (float)(hEll_mm - hMSL_mm) / 1000.0f;
        accH_m      = (float)hAcc_mm / 1000.0f;
        accV_m      = (float)vAcc_mm / 1000.0f;
        vspeed_ms   = -(float)velD_mms / 1000.0f;         // NED "down" -> positive up
        speedKmph   = ((float)gSpeed_mms / 1000.0f) * 3.6f;
        speedAcc_ms = (float)sAcc_mms / 1000.0f;
        heading     = (float)headMot_1e5 * 1e-5f;
        headAcc_deg = (float)headAcc_1e5 * 1e-5f;
        pdop        = (float)pDOP_e2 * 0.01f;             // NAV-DOP refines the full DOP suite

        if (validTime) {
        setUtcStrings((int)year, (int)month, (int)day, (int)hour, (int)minute, (int)sec);
        syncTimeFromGPS(dateUTC, timeUTC);
        }
    }
    }

    // NAV-DOP (01 04) length 18 — dilution-of-precision suite (0.01 units each).
    // This provides the genuine HDOP/VDOP (NAV-PVT only carries pDOP).
    static void handleNavDop(const uint8_t *p, uint16_t n) {
    if (n < 18) return;
    pdop = (float)u16(p + 6)  * 0.01f;
    vdop = (float)u16(p + 10) * 0.01f;
    hdop = (float)u16(p + 12) * 0.01f;
    }

    // NAV-STATUS (01 03) length 16 — time-to-first-fix + spoofing detection.
    static void handleNavStatus(const uint8_t *p, uint16_t n) {
    if (n < 16) return;
    uint8_t flags2 = p[7];
    spoofState = (flags2 >> 3) & 0x03;   // 0 unknown, 1 none, 2 indicated, 3 multiple
    ttffMs = u32(p + 8);
    }

    // MON-RF (0A 38) — RF interference/jamming + antenna diagnostics.
    // 4-byte header (version, nBlocks, reserved[2]) then 24 bytes per RF block.
    static void handleMonRf(const uint8_t *p, uint16_t n) {
    if (n < 4) return;
    uint8_t nBlocks = p[1];
    if (nBlocks < 1 || n < (uint16_t)(4 + 24)) return;

    const uint8_t *b = p + 4;              // first RF block (single band on M9N)
    rfJamState  = b[1] & 0x03;             // flags: jammingState (0..3)
    rfAntStatus = b[2];                    // 0 init,1 unknown,2 ok,3 short,4 open
    rfNoise = (int)u16(b + 12);            // noisePerMS, broadband noise floor
    uint16_t agc = u16(b + 14);            // agcCnt, 0..8191
    rfAgcPct = (int)((uint32_t)agc * 100u / 8191u);
    rfJamInd = b[16];                      // CW jamming indicator, 0..255
    }

    // NAV-ODO (01 09) length 20 — on-chip odometer (ground distance in metres).
    static void handleNavOdo(const uint8_t *p, uint16_t n) {
    if (n < 20) return;
    odoDist  = u32(p + 8);                  // distance since last reset (m)
    odoTotal = u32(p + 12);                 // lifetime total distance (m)
    }

    // NAV-SBAS (01 32) — SBAS augmentation status.
    static void handleNavSbas(const uint8_t *p, uint16_t n) {
    if (n < 12) return;
    int prevSys = sbasSys, prevPrn = sbasPrn, prevCnt = sbasCnt;
    sbasPrn = p[4];                         // GEO PRN in use
    sbasSys = (int8_t)p[6];                 // -1 unknown, 0 WAAS, 1 EGNOS, 2 MSAS, 3 GAGAN, 16 GPS
    sbasCnt = p[8];                         // number of SVs carrying SBAS corrections
    if (sbasSys != prevSys || sbasPrn != prevPrn || sbasCnt != prevCnt) {
        Serial.printf("[gps] NAV-SBAS sys=%d geo=%d cnt=%d mode=%d\n",
                      sbasSys, sbasPrn, sbasCnt, (int)p[5]);
    }
    }

    // MON-COMMS (0A 36) — per-port comms load. Header (8 B): version, nPorts,
    // txErrors, reserved, protIds[4]; then nPorts * 40-byte port blocks from
    // offset 8. We track UART1 (portId 0x0100), the receiver->ESP32 data path:
    // its TX buffer fills when the host reads too slowly; overruns = dropped bytes.
    // Offsets verified against the NEO-M9N Interface Description (UBX-19035940, proto 32).
    static void handleMonComms(const uint8_t *p, uint16_t n) {
    if (n < 8) return;
    uint8_t nPorts = p[1];
    static bool loggedPorts = false;
    for (uint8_t i = 0; i < nPorts; i++) {
        uint16_t off = 8 + (uint16_t)i * 40;
        if (off + 40 > n) break;
        const uint8_t *b = p + off;
        if (u16(b) == 0x0100) {                 // UART1
            int prevTx = uartTxPct, prevPeak = uartTxPeak, prevOvf = uartOvf;
            uartTxPct  = b[8];                  // txUsage: max TX buffer fill last period (%)
            uartTxPeak = b[9];                  // txPeakUsage: all-time peak TX fill (%)
            uartRxPct  = b[16];                 // rxUsage: max RX buffer fill last period (%)
            uartOvf    = (int)u16(b + 18);      // overrunErrs: 100 ms slots with RX overrun
            if (uartTxPct != prevTx || uartTxPeak != prevPeak || uartOvf != prevOvf) {
                Serial.printf("[gps] MON-COMMS UART1 tx=%d%% peak=%d%% rx=%d%% ovf=%d\n",
                              uartTxPct, uartTxPeak, uartRxPct, uartOvf);
            }
        }
    }
    if (!loggedPorts) {                         // one-time: confirm the port map on hardware
        loggedPorts = true;
        Serial.printf("[gps] MON-COMMS nPorts=%u ports:", nPorts);
        for (uint8_t i = 0; i < nPorts; i++) {
            uint16_t off = 8 + (uint16_t)i * 40;
            if (off + 40 > n) break;
            Serial.printf(" 0x%04X", u16(p + off));
        }
        Serial.printf("\n");
    }
    }

    // MON-SPAN (0A 31) — RF spectrum. Header 4 B (version, numRfBlocks, reserved[2]);
    // then numRfBlocks * 272-byte blocks from offset 4: spectrum[256], span(U4),
    // res(U4), center(U4), pga(U1), reserved[3]. We keep block 0 (M9N single L1 path).
    static void handleMonSpan(const uint8_t *p, uint16_t n) {
    if (n < 4) return;
    uint8_t numRfBlocks = p[1];
    if (numRfBlocks < 1) return;
    if (4 + 272 > n) return;                    // need at least one full block
    const uint8_t *b = p + 4;                   // block 0
    memcpy(spectrum, b, 256);
    specSpanHz   = u32(b + 256);
    specResHz    = u32(b + 260);
    specCenterHz = u32(b + 264);
    specPga      = b[268];
    if (!specValid) {
        Serial.printf("[gps] MON-SPAN blocks=%u center=%.2fMHz span=%.1fMHz res=%lukHz pga=%ddB\n",
                      numRfBlocks, specCenterHz / 1e6, specSpanHz / 1e6,
                      (unsigned long)(specResHz / 1000), specPga);
    }
    specValid = true;
    }

    // NAV-GEOFENCE (01 39) — geofence status. Header 8 B: iTOW(U4), version, status,
    // numFences, combState; then numFences * 2-byte {state,id}. combState/state:
    // 0 unknown, 1 inside, 2 outside. Only trust it when status==1 (active).
    static void handleNavGeofence(const uint8_t *p, uint16_t n) {
    if (n < 8) return;
    int prevState = geoState, prevStatus = geoStatus;
    geoStatus = p[5];                           // 0 = not active, 1 = active
    geoState  = (geoStatus == 1) ? p[7] : 0;    // combined inside/outside, gated on active
    if (geoState != prevState || geoStatus != prevStatus) {
        Serial.printf("[gps] NAV-GEOFENCE status=%d state=%d (%s)\n",
                      geoStatus, geoState,
                      geoState == 1 ? "inside" : (geoState == 2 ? "outside" : "unknown"));
    }
    }

    // ACK-ACK (05 01) / ACK-NAK (05 00) — log the outcome of config messages
    // (notably the deprecated CFG-GEOFENCE, to confirm it was accepted).
    static void handleAck(uint8_t id, const uint8_t *p, uint16_t n) {
    if (n < 2) return;
    Serial.printf("[gps] %s for cls=0x%02X id=0x%02X\n",
                  id == 0x01 ? "ACK" : "NAK", p[0], p[1]);
    }

    // MON-VER (0A 04) — receiver identity. sw(30) + hw(10) + N * 30-byte extension
    // strings ("MOD=NEO-M9N", "PROTVER=32.01", "FWVER=SPG 4.04", the GNSS list).
    static void handleMonVer(const uint8_t *p, uint16_t n) {
    if (n < 40) return;
    char sw[31]; memcpy(sw, p, 30); sw[30] = 0;   // swVersion (0..29)
    uint16_t nExt = (n - 40) / 30;
    for (uint16_t i = 0; i < nExt; i++) {
        char e[31]; memcpy(e, p + 40 + i * 30, 30); e[30] = 0;
        if (!strncmp(e, "MOD=", 4))          rxModule = String(e + 4);
        else if (!strncmp(e, "PROTVER=", 8)) rxProto  = String(e + 8);
        else if (!strncmp(e, "FWVER=", 6))   rxFw     = String(e + 6);
        else if (strchr(e, ';') && (strstr(e,"GPS")||strstr(e,"GLO")||strstr(e,"GAL")||strstr(e,"BDS")))
            rxGnss = String(e);
    }
    if (rxFw.length() == 0) rxFw = String(sw);    // fall back to swVersion
    Serial.printf("[gps] MON-VER mod=%s fw=%s proto=%s gnss=%s\n",
                  rxModule.c_str(), rxFw.c_str(), rxProto.c_str(), rxGnss.c_str());
    }

    // NAV-COV (01 36) length 64 — position/velocity covariance (NED, m^2).
    // Derive the horizontal error ellipse from the 2x2 N/E position covariance.
    static void handleNavCov(const uint8_t *p, uint16_t n) {
    if (n < 64) return;
    if (p[5] == 0) return;                 // posCovValid == 0 -> not usable

    float cNN = f32(p + 16);               // posCovNN (m^2)
    float cNE = f32(p + 20);               // posCovNE
    float cEE = f32(p + 28);               // posCovEE

    // Eigenvalues of [[cNN,cNE],[cNE,cEE]]
    float tr = cNN + cEE;
    float det = cNN * cEE - cNE * cNE;
    float disc = tr * tr / 4.0f - det;
    if (disc < 0) disc = 0;
    float s = sqrtf(disc);
    float l1 = tr / 2.0f + s;               // larger eigenvalue
    float l2 = tr / 2.0f - s;               // smaller eigenvalue
    if (l1 < 0) l1 = 0;
    if (l2 < 0) l2 = 0;

    errMajorM = sqrtf(l1);                   // 1-sigma std dev (m)
    errMinorM = sqrtf(l2);

    // Bearing of the major axis from North (deg), folded to 0..180
    float ang = 0.5f * atan2f(2.0f * cNE, cNN - cEE) * 180.0f / 3.14159265f;
    if (ang < 0) ang += 180.0f;
    errOrientDeg = ang;
    }

    // NAV-SAT (01 35) length = 8 + 12*numSvs
    // NAV-SAT (01 35) length = 8 + 12*numSvs
    static void handleNavSat(const uint8_t *p, uint16_t n) {
        if (!ENABLE_NAV_SAT) return;
        if (n < 8) return;

        uint8_t numSvs = p[5];
        uint16_t need = 8 + (uint16_t)numSvs * 12;
        if (n < need) return;

        // UBX delivers every satellite in a single NAV-SAT message per epoch,
        // so clear the list once at the start of each message.
        skyReset();

        for (uint8_t i = 0; i < numSvs; i++) {
            const uint8_t *sv = p + 8 + (uint16_t)i * 12;

            uint8_t gnssId = sv[0];
            uint8_t svId   = sv[1];
            uint8_t cno    = sv[2];         // C/N0 in dB-Hz
            int8_t elev    = (int8_t)sv[3];
            int16_t azim   = (int16_t)u16(sv + 4);
            uint32_t svFlags = u32(sv + 8); // per-SV flags word
            bool used = (svFlags >> 3) & 0x01; // bit 3 = svUsed (used in solution)

            char talker='?';
            if (gnssId == 0) talker='P';      // GPS
            else if (gnssId == 6) talker='L'; // GLONASS
            else if (gnssId == 2) talker='A'; // Galileo
            else if (gnssId == 3) talker='B'; // BeiDou
            else if (gnssId == 5) talker='Q'; // QZSS

            int prn = (int)svId;
            int snr = (int)cno;

            if (prn > 0) {
            skyAddSat(prn, (int)elev, (int)azim, snr, talker, used);
            }
        }
        
    }
    static void dispatchUbx(uint8_t cls, uint8_t id, const uint8_t *p, uint16_t n) {
    if (cls == 0x01 && id == 0x07)      { diagNavPvt++;    handleNavPvt(p, n); }
    else if (cls == 0x01 && id == 0x04) { diagNavDop++;    handleNavDop(p, n); }
    else if (cls == 0x01 && id == 0x03) { diagNavStatus++; handleNavStatus(p, n); }
    else if (cls == 0x01 && id == 0x36) { diagNavCov++;    handleNavCov(p, n); }
    else if (cls == 0x01 && id == 0x35) { diagNavSat++;    handleNavSat(p, n); }
    else if (cls == 0x01 && id == 0x09) {                  handleNavOdo(p, n); }
    else if (cls == 0x01 && id == 0x32) {                  handleNavSbas(p, n); }
    else if (cls == 0x01 && id == 0x39) {                  handleNavGeofence(p, n); }
    else if (cls == 0x0A && id == 0x38) { diagMonRf++;     handleMonRf(p, n); }
    else if (cls == 0x0A && id == 0x36) {                  handleMonComms(p, n); }
    else if (cls == 0x0A && id == 0x31) {                  handleMonSpan(p, n); }
    else if (cls == 0x0A && id == 0x04) {                  handleMonVer(p, n); }
    else if (cls == 0x05) {                                handleAck(id, p, n); }
    }

    // --------------- Public API ---------------
    static bool g_ubxSeen = false;      // set once a valid UBX frame is received
    static uint32_t g_lastCfgMs = 0;    // last time configuration was (re)sent

    void gpsParserInit(HardwareSerial &gps) {
    delay(200);                          // let the receiver finish booting first
    configureForUbx(gps);
    g_ubxSeen = false;
    g_lastCfgMs = millis();
    }

    void gpsParserProcess(HardwareSerial &gps) {
    while (gps.available()) {
        uint8_t b = (uint8_t)gps.read();
        diagBytesRx++;
        diagLastByteMs = millis();
        diagRing[diagRingIdx] = b; diagRingIdx = (diagRingIdx + 1) & 63;
        if (b == 0x24) diagNmeaCount++;   // '$' = start of an NMEA sentence

        switch (st_) {
        case WAIT1:
            if (b == SYNC1) st_ = WAIT2;
            break;
        case WAIT2:
            if (b == SYNC2) { st_ = C; diagUbxSync++; }
            else st_ = WAIT1;
            break;
        case C:
            cls_ = b; ckA_=0; ckB_=0; ckUpdate(b); st_ = I; break;
        case I:
            id_ = b; ckUpdate(b); st_ = L1; break;
        case L1:
            len_ = b; ckUpdate(b); st_ = L2; break;
        case L2:
            len_ |= ((uint16_t)b << 8); ckUpdate(b);
            payloadPos_ = 0;
            if (len_ > sizeof(payload_)) st_ = WAIT1;
            else st_ = (len_ == 0) ? CKA : PAY;
            break;
        case PAY:
            payload_[payloadPos_++] = b; ckUpdate(b);
            if (payloadPos_ >= len_) st_ = CKA;
            break;
        case CKA:
            rxCkA_ = b; st_ = CKB; break;
        case CKB:
            rxCkB_ = b;
            if (rxCkA_ == ckA_ && rxCkB_ == ckB_) {
            diagUbxFrames++;
            if (!g_ubxSeen) { g_ubxSeen = true; Serial.println("[ubx] link up - receiving UBX frames"); }
            dispatchUbx(cls_, id_, payload_, len_);
            } else {
            diagUbxBadCk++;
            }
            st_ = WAIT1;
            break;
        }
    }

    // If no UBX frame has arrived yet, keep re-sending the configuration. This
    // recovers from a slow receiver boot, or a receiver still in NMEA mode.
    // If you see this message repeat forever, the config is not reaching the
    // module -> check the ESP TX (GPIO17) -> module RX wiring.
    if (!g_ubxSeen && (millis() - g_lastCfgMs > 3000)) {
        Serial.println("[ubx] no UBX frames yet - resending configuration...");
        configureForUbx(gps);
        g_lastCfgMs = millis();
    }
    }

    // Configure one circular geofence via legacy UBX-CFG-GEOFENCE (0x06 0x69):
    // 8-byte header + one 12-byte fence block {lat(I4,1e-7deg), lon(I4,1e-7deg),
    // radius(U4, CENTIMETRES)}. Watch serial for ACK-ACK (accepted) vs NAK.
    void gpsConfigureGeofence(HardwareSerial &gps, double lat, double lon, uint32_t radiusM) {
    int32_t  latE7 = (int32_t)lround(lat * 1e7);
    int32_t  lonE7 = (int32_t)lround(lon * 1e7);
    uint32_t radCm = radiusM * 100;             // radius field is in 1e-2 m (cm)
    uint8_t pl[20];
    memset(pl, 0, sizeof(pl));
    pl[1] = 0x01;                               // numFences = 1 (version, confLvl, pio = 0)
    memcpy(pl + 8,  &latE7, 4);
    memcpy(pl + 12, &lonE7, 4);
    memcpy(pl + 16, &radCm, 4);
    sendUbx(gps, 0x06, 0x69, pl, 20);
    Serial.printf("[gps] CFG-GEOFENCE set lat=%.6f lon=%.6f r=%um (%ucm)\n",
                  lat, lon, (unsigned)radiusM, (unsigned)radCm);
    }

// ... rest of file
#endif
