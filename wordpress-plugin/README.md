# Satellite GPS Tracker — server + tooling

The WordPress half of the ESP32 satellite receiver project, plus test tooling.

```
  ESP32-S3 + NEO-M9N                 WordPress (your VPS)
 ┌────────────────────┐   HTTPS      ┌──────────────────────────────┐
 │ GNSS parse + WiFi   │  POST JSON  │ /wp-json/satgps/v1/ingest     │
 │ (src/wifi_uplink)   ├────────────►│  → validate API key           │
 │                     │  X-API-Key  │  → store in wp_satgps_fixes   │
 └────────────────────┘             │                              │
                                     │ Dashboard (admin + shortcode) │
   Browser ◄───────────────────────┤  map · sky plot · charts ·    │
                                     │  constellations · trip stats  │
                                     └──────────────────────────────┘
```

## 1. Install the plugin

1. Upload `satellite-gps-tracker.zip` via **Plugins → Add New → Upload Plugin**
   (or copy the `satellite-gps-tracker/` folder into `wp-content/plugins/`).
2. Activate it.
3. Open **Satellite GPS → Settings**. Note the **Ingest endpoint** and **API key**.

## 2. Point the device at WordPress

Edit `include/secrets.h` in the firmware (copy from `secrets.example.h`):

```c
#define WIFI_SSID       "your-wifi"
#define WIFI_PASS       "your-password"
#define SATGPS_ENDPOINT "https://your-site/wp-json/satgps/v1/ingest"
#define SATGPS_API_KEY  "<the key from the settings page>"
#define SATGPS_DEVICE_ID "sat-01"
```

Build & flash: `pio run -t upload`. Telemetry appears on **Satellite GPS → Dashboard**.

To show it publicly: tick **Public dashboard** in settings and put
`[satgps_dashboard]` on any page (optionally `[satgps_dashboard device="sat-01"]`).

## 3. Test WITHOUT the hardware (recommended first)

Everything lives in `tools/` and needs only Python 3 (standard library).

**a) See the exact packet the device sends**

```
python tools/simulate_device.py --dry-run
```

**b) Feed your real WordPress site with a simulated moving track**

```
python tools/simulate_device.py \
    --url https://your-site/wp-json/satgps/v1/ingest \
    --key YOUR_API_KEY --device sim-01 --count 0 --interval 2
```

Watch it appear live on the dashboard. `--count 0` runs until Ctrl-C.

**c) Full offline preview (no WordPress needed)**

```
# terminal 1 — fake server
python tools/mock_server.py --port 8787 --key testkey123

# terminal 2 — feed it
python tools/simulate_device.py \
    --url http://127.0.0.1:8787/wp-json/satgps/v1/ingest \
    --key testkey123 --device sim-01 --count 0 --interval 2

# terminal 3 — serve the preview page, then open it in a browser
python -m http.server 8080
#   http://127.0.0.1:8080/tools/preview.html
```

`preview.html` loads the plugin's real CSS/JS against the mock server, so you can
see the dashboard working before deploying.

## Quality checks

| Check | Command | Result |
|-------|---------|--------|
| PHP syntax (all files) | `python tools/php_lint.py` | 8/8 pass |
| JS syntax | `node --check assets/js/*.js` | pass |
| Device↔server JSON contract | `python tools/contract_check.py` | pass |
| End-to-end ingest + read-back | mock server + simulator | pass |

## Security notes

* Ingest requires the secret API key in the `X-API-Key` header (constant-time
  compared with `hash_equals`).
* Read endpoints are admin-only unless **Public dashboard** is enabled.
* All DB access uses `$wpdb->prepare`; all output is escaped; inputs are
  sanitised on ingest.
* Deleting the plugin drops its table and options (`uninstall.php`).
