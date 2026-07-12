=== Satellite GPS Tracker ===
Contributors: peterjmayhew
Tags: gps, gnss, tracker, esp32, map
Requires at least: 5.8
Tested up to: 6.6
Requires PHP: 7.4
Stable tag: 1.4.7
License: GPLv2 or later

Receive live GNSS telemetry from an ESP32 satellite receiver and present it as an
interactive map, sky plot, signal charts and trip statistics.

== Description ==

Satellite GPS Tracker is the server half of a DIY GPS logger built around an
ESP32-S3 and a u-blox NEO-M9N. The device POSTs JSON telemetry to a REST endpoint
provided by this plugin; the plugin stores it and renders a rich dashboard:

* Live status (fix, satellites, speed, altitude, HDOP/accuracy, heading)
* Interactive Leaflet map with a speed-coloured track and heading marker
* Sky plot of every satellite in view, coloured by signal strength
* Signal-strength (C/N0) bar chart and per-constellation breakdown
* Speed and altitude history charts
* Trip statistics (distance, moving time, max/avg speed)
* Built-in "how this works" explainers

The dashboard is available in wp-admin and, optionally, on the front end via the
`[satgps_dashboard]` shortcode.

== Installation ==

1. Upload the `satellite-gps-tracker` folder to `/wp-content/plugins/` (or install
   the ZIP via Plugins > Add New > Upload).
2. Activate the plugin.
3. Go to **Satellite GPS > Settings**. Copy the **Ingest endpoint** URL and the
   **API key** into your ESP32 `include/secrets.h`:
       #define SATGPS_ENDPOINT "https://your-site/wp-json/satgps/v1/ingest"
       #define SATGPS_API_KEY  "the-key-shown-on-the-settings-page"
4. Flash the device. Telemetry will appear on **Satellite GPS > Dashboard**.
5. (Optional) Tick "Public dashboard" and put `[satgps_dashboard]` on any page to
   show it to visitors.

Shortcode options:
* `device` - show only one device id (default: all).
* `height` - map height in pixels (default: 480).
* `hide_map` - set to "1" to hide the map + coordinates and show a message
  instead (e.g. to publish the dashboard without revealing exact location).
* `hide_map_message` - custom text for the hidden-map message.
  Example: `[satgps_dashboard hide_map="1" hide_map_message="Location shown to family only"]`

== Frequently Asked Questions ==

= Does the map/charts need internet? =
The viewer's browser loads map tiles (OpenStreetMap) and the Leaflet/Chart.js
libraries from a CDN, so the browser needs internet. The ingest endpoint itself
has no external dependencies.

= Is the endpoint secure? =
Ingest requires the secret API key in the `X-API-Key` header (constant-time
compared). Read endpoints are admin-only unless you enable "Public dashboard".

== Changelog ==

= 1.4.7 =
* Clearer "Receiver health & integrity" section. Every tile now has honest
  labels, a plain-English "?" explainer, real good/watch/problem thresholds and
  a distinct grey "not measured" state, so a blank reading no longer looks like a
  fault. A colour key and an at-a-glance integrity summary chip were added, the
  antenna tile now reads "Not sensed" on boards without a supervisor circuit
  (instead of a misleading "Init"), and time-to-first-fix is framed as a startup
  benchmark rather than a broken live gauge.
* New receiver telemetry surfaced from the NEO-M9N: a broadband **Noise floor**
  tile (MON-RF noisePerMS) to sit alongside the narrowband CW indicator, and an
  on-chip **Odometer** (UBX NAV-ODO) tile showing hardware-filtered distance
  since power-on plus lifetime total — accurate at low speed and immune to the
  GPS jitter that can inflate a track-integrated distance. Requires device
  firmware v1.08+ (the ESP32 side that sends these fields).
* The ingest endpoint now stores the new `noise`, `odo_m` and `odo_total_m`
  fields (schema bumped, migrated automatically on upgrade) and returns them from
  the `latest` endpoint, so the new tiles populate. (Supersedes the incomplete
  1.4.6 release, which added the tiles but not their server-side storage.)

= 1.4.5 =
* New shortcode option: `[satgps_dashboard hide_map="1"]` hides the map and
  coordinates and shows a message in their place — handy for embedding the
  dashboard publicly without revealing exact location. Customise the text with
  `hide_map_message="…"`.

= 1.4.4 =
* New: CSV and GPX export buttons on the dashboard toolbar — download the track
  for the selected device and time range straight to your computer (GPX opens in
  Google Earth, mapping and GIS tools). Generated in the browser from the stored
  data; no extra server load.

= 1.4.3 =
* Fix: the "?" help pop-ups were unreadable — the modal renders outside the
  dashboard's styling scope, so its text/background CSS variables didn't resolve
  (transparent box, grey-on-dark text). The palette is now re-declared on the
  modal so help text is legible.

= 1.4.2 =
* Clearer status: the toolbar now shows two separate indicators — "Receiver"
  (is the ESP32 reaching the server?) and "GPS" (does it have a fix?) — so an
  offline state is never ambiguous between "no data from the device" and "no
  GPS signal".

= 1.4.1 =
* Fix: the Device/Range dropdowns' text disappeared on hover in some browsers
  (native control repaint). The dropdowns now use custom-rendered styling with a
  chevron, so the label stays visible on hover and focus.

= 1.4.0 =
* Automatic updates from GitHub Releases via the bundled Plugin Update Checker
  library. Updates now show on the Plugins screen with one-click update and the
  per-plugin auto-update toggle — no more manual ZIP uploads.

= 1.3.x =
* Dashboard fixes: chart height/stretch, modal click-blocking, and versioned
  cache-busting of CSS/JS.

= 1.1.0 - 1.2.0 =
* UBX-derived telemetry (accuracy in metres, DOP, receiver health), position
  error ellipse, and history charts.

= 1.0.0 =
* Initial release.
