/* Satellite GPS Tracker - dashboard controller. */
(function () {
	'use strict';

	if (typeof window.satgpsData === 'undefined') { return; }
	var CFG = window.satgpsData;

	// ---- small helpers ----------------------------------------------------
	function $(sel, root) { return (root || document).querySelector(sel); }
	function field(name) { return document.querySelector('[data-field="' + name + '"]'); }
	function setField(name, value) { var el = field(name); if (el) el.textContent = value; }
	function setHealth(name, text, state) {
		var el = field(name);
		if (!el) { return; }
		el.textContent = text;
		// 'muted' = not measured / not reported yet (grey, informational). Anything
		// with no explicit state also falls back to muted so a blank reading never
		// masquerades as a bright "real" value.
		var cls = { ok: ' is-ok', warn: ' is-warn', bad: ' is-bad', muted: ' is-muted' }[state] || ' is-muted';
		el.className = 'satgps-tile-value' + cls;
	}
	function esc(s) {
		return String(s).replace(/[&<>"']/g, function (c) {
			return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
		});
	}
	// UBX NAV-SBAS "sys" field -> human name. -1/unknown -> '' (caller decides).
	function sbasSysName(sys) {
		return { '0': 'WAAS', '1': 'EGNOS', '2': 'MSAS', '3': 'GAGAN', '16': 'GPS' }[String(sys)] || '';
	}
	// UBX MON-VER GNSS token -> full constellation name.
	function gnssName(tok) {
		return { GPS: 'GPS', GLO: 'GLONASS', GAL: 'Galileo', BDS: 'BeiDou',
			QZSS: 'QZSS', SBAS: 'SBAS', IMES: 'IMES', NAVIC: 'NavIC', IRNSS: 'NavIC' }[tok] || tok;
	}

	function api(path, params) {
		var url = CFG.restUrl + path;
		if (params) {
			var q = Object.keys(params)
				.filter(function (k) { return params[k] !== '' && params[k] !== undefined && params[k] !== null; })
				.map(function (k) { return encodeURIComponent(k) + '=' + encodeURIComponent(params[k]); });
			if (q.length) { url += '?' + q.join('&'); }
		}
		return fetch(url, { headers: { 'X-WP-Nonce': CFG.nonce }, credentials: 'same-origin' })
			.then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); });
	}

	var useMph = CFG.units === 'mph';
	// Last-seen cumulative UART overrun count, to detect *new* drops between refreshes
	// (uart_ovf is a lifetime counter, so only a positive delta is a live problem).
	var prevUartOvf = null;
	function spd(kmh) { return useMph ? kmh * 0.6213711922 : kmh; }
	function dist(km) { return useMph ? km * 0.6213711922 : km; }
	var spdUnit = useMph ? 'mph' : 'km/h';
	var distUnit = useMph ? 'mi' : 'km';

	function cardinal(deg) {
		var dirs = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
		return dirs[Math.round((deg % 360) / 45) % 8];
	}
	function hdopDesc(h) {
		if (h == null || isNaN(h) || h <= 0) return '';
		if (h < 1) return 'Ideal';
		if (h < 2) return 'Excellent';
		if (h < 5) return 'Good';
		if (h < 10) return 'Moderate';
		return 'Poor';
	}
	function fmtDuration(sec) {
		sec = Math.max(0, Math.floor(sec));
		var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
		return h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
	}
	function tsToLocal(ts) {
		// Stored ts is UTC ('YYYY-MM-DD HH:MM:SS'); make it a real Date.
		var d = new Date((ts || '').replace(' ', 'T') + 'Z');
		return isNaN(d.getTime()) ? null : d;
	}
	function fmtTime(ts) { var d = tsToLocal(ts); return d ? d.toLocaleTimeString() : '—'; }

	// ---- state ------------------------------------------------------------
	var state = {
		device: (document.querySelector('.satgps-frontend') || {}).dataset
			? (document.querySelector('.satgps-frontend').dataset.device || '') : '',
		range: '24h',
		live: true,
		map: null, marker: null, accCircle: null, accEllipse: null, track: null, trackLayers: [],
		charts: {}
	};

	var frontend = document.querySelector('.satgps-frontend');
	if (frontend && frontend.dataset.mapHeight) {
		document.documentElement.style.setProperty('--satgps-map-height', parseInt(frontend.dataset.mapHeight, 10) + 'px');
	}

	// ---- map --------------------------------------------------------------
	function initMap() {
		var el = $('#satgps-map');
		if (!el || typeof L === 'undefined') { return; }
		state.map = L.map(el, { zoomControl: true, attributionControl: true }).setView([20, 0], 2);
		L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
			maxZoom: 19,
			attribution: '&copy; OpenStreetMap contributors'
		}).addTo(state.map);
	}

	function speedColor(frac) {
		var hue = 210 - 210 * Math.max(0, Math.min(1, frac)); // blue -> red
		return 'hsl(' + hue + ',85%,55%)';
	}

	function renderTrack(points) {
		if (!state.map) { return; }
		state.trackLayers.forEach(function (l) { state.map.removeLayer(l); });
		state.trackLayers = [];
		if (state.track) { state.map.removeLayer(state.track); state.track = null; }
		if (!points.length) { return; }

		var maxSpeed = points.reduce(function (m, p) { return Math.max(m, p.speed_kmh); }, 1);
		var latlngs = points.map(function (p) { return [p.lat, p.lon]; });

		if (points.length <= 800) {
			for (var i = 1; i < points.length; i++) {
				var seg = L.polyline([latlngs[i - 1], latlngs[i]], {
					color: speedColor(points[i].speed_kmh / maxSpeed), weight: 4, opacity: 0.9
				}).addTo(state.map);
				state.trackLayers.push(seg);
			}
		} else {
			state.track = L.polyline(latlngs, { color: '#4c8bf5', weight: 3, opacity: 0.85 }).addTo(state.map);
		}

		try { state.map.fitBounds(L.latLngBounds(latlngs).pad(0.15)); } catch (e) {}
	}

	function clearLayer(key) {
		if (state[key]) { state.map.removeLayer(state[key]); state[key] = null; }
	}

	// Build a lat/lon polygon approximating an ellipse of the given metric axes,
	// centred at (lat,lon) with the major axis at `bearingDeg` from North.
	function ellipsePolygon(lat, lon, majorM, minorM, bearingDeg) {
		var pts = [];
		var br = bearingDeg * Math.PI / 180;
		var cosB = Math.cos(br), sinB = Math.sin(br);
		var mLat = 111320;
		var mLon = 111320 * Math.cos(lat * Math.PI / 180) || 1;
		for (var i = 0; i <= 48; i++) {
			var t = (i / 48) * 2 * Math.PI;
			var u = majorM * Math.cos(t), v = minorM * Math.sin(t);
			var north = u * cosB - v * sinB;
			var east = u * sinB + v * cosB;
			pts.push([lat + north / mLat, lon + east / mLon]);
		}
		return pts;
	}

	function updateAccuracyOverlay(latest, ll) {
		if (!state.map) { return; }
		var style = { color: '#4c8bf5', weight: 1, fillColor: '#4c8bf5', fillOpacity: 0.12 };
		if (latest.err_major_m > 0) {
			clearLayer('accCircle');
			var poly = ellipsePolygon(latest.lat, latest.lon, latest.err_major_m,
				latest.err_minor_m || latest.err_major_m, latest.err_orient_deg || 0);
			if (!state.accEllipse) {
				state.accEllipse = L.polygon(poly, style).addTo(state.map);
			} else {
				state.accEllipse.setLatLngs(poly);
			}
		} else if (latest.acc_h_m > 0) {
			clearLayer('accEllipse');
			if (!state.accCircle) {
				state.accCircle = L.circle(ll, { radius: latest.acc_h_m, color: style.color,
					weight: 1, fillColor: style.fillColor, fillOpacity: 0.12 }).addTo(state.map);
			} else {
				state.accCircle.setLatLng(ll);
				state.accCircle.setRadius(latest.acc_h_m);
			}
		} else {
			clearLayer('accCircle');
			clearLayer('accEllipse');
		}
	}

	function updateMarker(latest) {
		if (!state.map || !latest.found || !latest.fix) { return; }
		var ll = [latest.lat, latest.lon];
		var html = '<div class="satgps-arrow" style="transform:rotate(' + latest.heading_deg + 'deg)">&#10148;</div>';
		var icon = L.divIcon({ className: 'satgps-marker', html: html, iconSize: [24, 24], iconAnchor: [12, 12] });
		if (!state.marker) {
			state.marker = L.marker(ll, { icon: icon, zIndexOffset: 1000 }).addTo(state.map);
		} else {
			state.marker.setLatLng(ll); state.marker.setIcon(icon);
		}

		// Accuracy overlay: prefer the oriented error ellipse (NAV-COV), else a circle (hAcc).
		updateAccuracyOverlay(latest, ll);

		state.marker.bindPopup(
			'Lat ' + latest.lat.toFixed(6) + '<br>Lon ' + latest.lon.toFixed(6) +
			'<br>' + spd(latest.speed_kmh).toFixed(1) + ' ' + spdUnit +
			(latest.acc_h_m > 0 ? '<br>±' + latest.acc_h_m.toFixed(1) + ' m' : '')
		);
	}

	// ---- charts -----------------------------------------------------------
	function makeLineChart(canvasId, label, color) {
		var el = document.getElementById(canvasId);
		if (!el || typeof Chart === 'undefined') { return null; }
		return new Chart(el.getContext('2d'), {
			type: 'line',
			data: { labels: [], datasets: [{ label: label, data: [], borderColor: color,
				backgroundColor: color.replace(/^hsl\((.*)\)$/, 'hsla($1,0.15)'),
				fill: true, tension: 0.25, pointRadius: 0, borderWidth: 2 }] },
			options: {
				responsive: true, maintainAspectRatio: false, animation: false,
				plugins: { legend: { display: false } },
				scales: {
					x: { ticks: { color: '#97a3b6', maxTicksLimit: 8 }, grid: { color: 'rgba(150,163,182,0.12)' } },
					y: { ticks: { color: '#97a3b6' }, grid: { color: 'rgba(150,163,182,0.12)' } }
				}
			}
		});
	}

	function initCharts() {
		if (typeof Chart === 'undefined') { return; }
		Chart.defaults.color = '#97a3b6';
		state.charts.speed = makeLineChart('satgps-speed-chart', 'Speed', 'hsl(150,70%,55%)');
		state.charts.alt = makeLineChart('satgps-alt-chart', 'Altitude', 'hsl(35,90%,55%)');
		state.charts.vspeed = makeLineChart('satgps-vspeed-chart', 'Vertical speed', 'hsl(280,60%,60%)');
		state.charts.acc = makeLineChart('satgps-acc-chart', 'Accuracy', 'hsl(0,70%,55%)');

		var snrEl = document.getElementById('satgps-snr');
		if (snrEl) {
			state.charts.snr = new Chart(snrEl.getContext('2d'), {
				type: 'bar',
				data: { labels: [], datasets: [{ label: 'C/N0', data: [], backgroundColor: [] }] },
				options: {
					responsive: true, maintainAspectRatio: false, animation: false,
					plugins: { legend: { display: false }, tooltip: { callbacks: {
						label: function (c) { return c.parsed.y + ' dB-Hz'; } } } },
					scales: {
						x: { ticks: { color: '#97a3b6', maxRotation: 90, minRotation: 45 }, grid: { display: false } },
						y: { beginAtZero: true, suggestedMax: 50, ticks: { color: '#97a3b6' }, grid: { color: 'rgba(150,163,182,0.12)' } }
					}
				}
			});
		}
	}

	function downsample(points, max) {
		if (points.length <= max) { return points; }
		var step = Math.ceil(points.length / max), out = [];
		for (var i = 0; i < points.length; i += step) { out.push(points[i]); }
		return out;
	}

	function updateHistoryCharts(points) {
		var ds = downsample(points, 400);
		var labels = ds.map(function (p) { return fmtTime(p.ts); });
		if (state.charts.speed) {
			state.charts.speed.data.labels = labels;
			state.charts.speed.data.datasets[0].data = ds.map(function (p) { return +spd(p.speed_kmh || 0).toFixed(1); });
			state.charts.speed.data.datasets[0].label = 'Speed (' + spdUnit + ')';
			state.charts.speed.update();
		}
		if (state.charts.alt) {
			state.charts.alt.data.labels = labels;
			state.charts.alt.data.datasets[0].data = ds.map(function (p) { return +(p.alt_m || 0).toFixed(1); });
			state.charts.alt.update();
		}
		if (state.charts.vspeed) {
			state.charts.vspeed.data.labels = labels;
			state.charts.vspeed.data.datasets[0].data = ds.map(function (p) { return +(p.vspeed_ms || 0).toFixed(2); });
			state.charts.vspeed.data.datasets[0].label = 'Vertical speed (m/s)';
			state.charts.vspeed.update();
		}
		if (state.charts.acc) {
			state.charts.acc.data.labels = labels;
			state.charts.acc.data.datasets[0].data = ds.map(function (p) { return +(p.acc_h_m || 0).toFixed(2); });
			state.charts.acc.data.datasets[0].label = 'Accuracy (± m)';
			state.charts.acc.update();
		}
	}

	function updateSnrChart(sats) {
		if (!state.charts.snr) { return; }
		var withSnr = (sats || []).filter(function (s) { return s.snr >= 0; })
			.sort(function (a, b) { return b.snr - a.snr; });
		state.charts.snr.data.labels = withSnr.map(function (s) {
			return SatGPSSkyplot.constInfo(s.c).code + s.prn;
		});
		state.charts.snr.data.datasets[0].data = withSnr.map(function (s) { return s.snr; });
		state.charts.snr.data.datasets[0].backgroundColor = withSnr.map(function (s) { return SatGPSSkyplot.snrColor(s.snr); });
		state.charts.snr.update();
	}

	function updateConstellation(sats) {
		var host = $('#satgps-const');
		if (!host) { return; }
		var counts = {};
		(sats || []).forEach(function (s) {
			var info = SatGPSSkyplot.constInfo(s.c);
			counts[info.name] = counts[info.name] || { n: 0, color: info.color };
			counts[info.name].n++;
		});
		var names = Object.keys(counts);
		var max = names.reduce(function (m, n) { return Math.max(m, counts[n].n); }, 1);
		if (!names.length) { host.innerHTML = '<p class="satgps-muted">No satellites in view.</p>'; return; }
		host.innerHTML = names.map(function (n) {
			var c = counts[n];
			var pct = Math.round(100 * c.n / max);
			return '<div class="satgps-const-row"><span class="satgps-const-name">' + n + '</span>' +
				'<span class="satgps-bar"><span style="width:' + pct + '%;background:' + c.color + '"></span></span>' +
				'<span class="satgps-const-count">' + c.n + '</span></div>';
		}).join('');
	}

	function updateSkyLegend() {
		var host = $('#satgps-sky-legend');
		if (!host) { return; }
		host.innerHTML =
			'<span><i style="background:#22c55e"></i>Strong &ge;35</span>' +
			'<span><i style="background:#f59e0b"></i>Medium 20-35</span>' +
			'<span><i style="background:#ef4444"></i>Weak &lt;20</span>' +
			'<span><i style="background:#9ca3af"></i>No signal</span>' +
			'<span><i style="background:transparent;border:2px solid #fff"></i>Used in fix</span>';
	}

	// ---- live status ------------------------------------------------------
	// Two independent indicators so "offline" is never ambiguous:
	//   1. Receiver link  — is the ESP32 actually reaching the server?
	//   2. GPS            — does the receiver have a satellite fix right now?
	function updateStatus(latest) {
		var linkDot = $('#satgps-status-dot'), linkText = $('#satgps-status-text');
		var gpsDot = $('#satgps-gps-dot'), gpsText = $('#satgps-gps-text');
		function set(dotEl, textEl, cls, msg) {
			if (dotEl) dotEl.className = 'satgps-status-dot' + (cls ? ' ' + cls : '');
			if (textEl) textEl.textContent = msg;
		}

		// Nothing has ever been received from the device.
		if (!latest.found) {
			set(linkDot, linkText, 'is-offline', 'No data from receiver');
			set(gpsDot, gpsText, '', 'GPS —');
			return;
		}

		var age = latest.age_seconds || 0;
		var ago = age < 60 ? age + 's ago'
			: age < 3600 ? Math.round(age / 60) + 'm ago'
			: Math.round(age / 3600) + 'h ago';

		// 1) Receiver link — based purely on how long since the ESP32 last posted.
		if (age > 120) {
			set(linkDot, linkText, 'is-offline', 'Receiver offline — no data · ' + ago);
		} else if (age > 30) {
			set(linkDot, linkText, 'is-stale', 'Receiver online (delayed) · ' + ago);
		} else {
			set(linkDot, linkText, 'is-online', 'Receiver online · ' + ago);
		}

		// 2) GPS fix — only meaningful when data is fresh. If the uplink is dead we
		//    can't know the live GPS state, so show a neutral dash.
		if (age > 120) {
			set(gpsDot, gpsText, '', 'GPS — (no recent data)');
		} else if (latest.fix || latest.fix_type >= 2) {
			set(gpsDot, gpsText, 'is-online', 'GPS fix · ' + (latest.sats_used || 0) + ' sats');
		} else if ((latest.sats_in_view || 0) > 0) {
			set(gpsDot, gpsText, 'is-stale', 'No GPS fix — searching (' + latest.sats_in_view + ' in view)');
		} else {
			set(gpsDot, gpsText, 'is-stale', 'No GPS fix — no satellites');
		}
	}

	function updateTiles(latest) {
		var fixEl = field('fix');
		if (fixEl) {
			fixEl.textContent = latest.found && latest.fix ? 'YES' : 'NO';
			fixEl.className = 'satgps-tile-value ' + (latest.found && latest.fix ? 'is-ok' : 'is-bad');
		}
		if (!latest.found) { return; }
		var ft = { 2: '2D fix', 3: '3D fix', 5: 'time only' }[latest.fix_type] || 'fix';
		var fixSbas = latest.sbas ? (' · ' + (sbasSysName(latest.sbas_sys) || 'SBAS')) : '';
		setField('fix_sub', latest.fix ? (ft + fixSbas) : 'searching…');
		setField('sats_used', latest.sats_used);
		setField('sats_in_view', latest.sats_in_view);
		setField('speed', spd(latest.speed_kmh).toFixed(1));
		setField('speed_unit', spdUnit);
		setField('alt', latest.alt_m.toFixed(1));
		var vs = latest.vspeed_ms || 0;
		var arrow = vs > 0.2 ? '▲ ' : (vs < -0.2 ? '▼ ' : '');
		setField('alt_sub', 'MSL · ' + arrow + Math.abs(vs).toFixed(1) + ' m/s');
		if (latest.acc_h_m > 0) {
			setField('acc', '±' + latest.acc_h_m.toFixed(1) + ' m');
			setField('acc_sub', 'HDOP ' + (latest.hdop || 0).toFixed(1) + ' · ' + hdopDesc(latest.hdop));
		} else {
			setField('acc', latest.hdop ? latest.hdop.toFixed(1) : '—');
			setField('acc_sub', 'HDOP · ' + hdopDesc(latest.hdop));
		}
		setField('heading', Math.round(latest.heading_deg) + '°');
		setField('heading_cardinal', cardinal(latest.heading_deg));
		setField('latlon', latest.fix ? (latest.lat.toFixed(6) + ', ' + latest.lon.toFixed(6)) : 'No fix');
		setField('last_ts', latest.ts ? 'as of ' + fmtTime(latest.ts) + ' local' : '');
		setField('device_footer', latest.device || '');

		// Receiver health / integrity. Decoded from UBX MON-RF + NAV-STATUS, so the
		// whole section is blank/grey in plain-NMEA mode. Colour key: grey = not
		// measured, green = healthy, amber = watch, red = a real problem. "Not
		// measured" is deliberately distinct from "measured and fine".
		var worst = 0; // 0 none, 1 ok, 2 watch, 3 problem — for the roll-up chip.
		function note(state) { var r = { ok: 1, warn: 2, bad: 3 }[state] || 0; if (r > worst) worst = r; }

		// Interference — the receiver's own jamming verdict (ITFM). jam_ind is the
		// narrowband CW indicator (0-255) shown as a %.
		var js = latest.jam_state;
		if (js == null) { setHealth('jam', '—', 'muted'); setField('jam_sub', 'RF jamming monitor'); }
		else {
			var js_state = js === 1 ? 'ok' : (js === 2 ? 'warn' : (js === 3 ? 'bad' : 'muted'));
			setHealth('jam', ['Monitor off', 'Clear', 'Elevated', 'Jammed'][js] || 'Monitor off', js_state);
			var cw = Math.round((latest.jam_ind || 0) * 100 / 255);
			setField('jam_sub', 'CW ' + cw + '%');
			note(js_state);
		}

		// RF gain (AGC) — automatic front-end gain. Absolute value is only weakly
		// diagnostic (a sudden swing is the real signal), so it is informational
		// across the normal band and only amber at the extremes; never red.
		if (latest.agc_pct == null) { setHealth('agc', '—', 'muted'); }
		else {
			var agc = latest.agc_pct;
			var agc_state = (agc < 20 || agc > 95) ? 'warn' : '';
			setHealth('agc', agc + '%', agc_state || 'ok'); // green = in the healthy band
			note(agc_state);
		}
		setField('agc_sub', 'front-end gain');

		// Noise floor (broadband) — MON-RF noisePerMS. Counterpart to the CW
		// indicator: a rising broadband floor is the fingerprint of a wideband
		// jammer. Absolute scale is receiver-relative, so shown informational.
		if (latest.noise == null) { setHealth('noise', '—', 'muted'); setField('noise_sub', 'broadband level'); }
		else { setHealth('noise', String(latest.noise), 'ok'); setField('noise_sub', 'broadband level'); }

		// Antenna feed — needs an antenna-supervisor circuit (bias-tee + current
		// sense) that this board does not have, so it honestly reads "Not sensed"
		// (grey) rather than pretending. OK/SHORT/OPEN only on boards wired for it.
		var as = latest.ant_status;
		if (as == null || as <= 1) { setHealth('ant', 'Not sensed', 'muted'); }
		else {
			var ant_state = as === 2 ? 'ok' : 'bad';
			setHealth('ant', ['', '', 'OK', 'SHORT', 'OPEN'][as] || '—', ant_state);
			note(ant_state);
		}

		// Spoofing — counterfeit-signal check (basic on the M9N: "None" is
		// reassuring, not a cryptographic guarantee).
		var ss = latest.spoof_state;
		if (ss == null || ss === 0) { setHealth('spoof', 'Not checked', 'muted'); }
		else {
			var sp_state = ss === 1 ? 'ok' : (ss === 2 ? 'warn' : 'bad');
			setHealth('spoof', ['Not checked', 'None', 'Suspected', 'Detected'][ss] || 'Not checked', sp_state);
			note(sp_state);
		}

		// Time to first fix — one-off startup benchmark, not a live gauge. A cold
		// start legitimately takes ~30 s; only long times are worth flagging.
		if (!(latest.ttff_ms > 0)) { setHealth('ttff', '—', 'muted'); }
		else {
			var t = latest.ttff_ms;
			var t_state = t <= 35000 ? 'ok' : (t <= 90000 ? 'warn' : 'bad');
			setHealth('ttff', (t / 1000).toFixed(1) + ' s', t_state);
			// TTFF is a startup score, not a live problem, so keep it out of the roll-up.
		}

		// UART link load (MON-COMMS) — how full the receiver's UART transmit buffer
		// to the ESP32 gets. A live plumbing gauge: high = the host isn't draining
		// the data stream fast enough, overruns = bytes actually dropped. -1/missing
		// = not reported yet (plain-NMEA mode or before the first MON-COMMS).
		var ul = latest.uart_tx_pct;
		var ubar = field('uart_bar');
		if (ul == null || ul < 0) {
			setHealth('uart', '—', 'muted');
			setField('uart_sub', 'receiver → host buffer');
			if (ubar) { ubar.style.width = '0%'; ubar.style.background = 'var(--satgps-muted)'; }
		} else {
			var ovf = latest.uart_ovf || 0;
			var upeak = (latest.uart_tx_peak != null && latest.uart_tx_peak >= 0) ? latest.uart_tx_peak : ul;
			// Live state uses the current-period load (uart_tx_pct). The all-time peak
			// and lifetime overrun total are cumulative, so they inform but must NOT
			// latch the live chip — only a *fresh* overrun since the last refresh
			// (positive delta; a reboot resets the counter, so guard against that) is
			// treated as a live "dropping bytes now" problem.
			var newDrop = (prevUartOvf !== null && ovf > prevUartOvf);
			prevUartOvf = ovf;
			var u_state = (ul >= 90 || newDrop) ? 'bad' : (ul >= 70 ? 'warn' : 'ok');
			setHealth('uart', ul + '%', u_state);
			if (ubar) {
				ubar.style.width = Math.max(0, Math.min(100, ul)) + '%';
				ubar.style.background = { ok: 'var(--satgps-ok)', warn: 'var(--satgps-warn)', bad: 'var(--satgps-bad)' }[u_state];
			}
			var ovfStr = ovf > 0 ? ' · ' + ovf + (ovf === 1 ? ' overrun' : ' overruns') : '';
			setField('uart_sub', 'peak ' + upeak + '%' + ovfStr);
			note(u_state);
		}

		// Roll-up chip: worst live integrity verdict across jam/agc/antenna/spoof
		// and UART link load (live load + fresh drops only — not its historical peak).
		var chip = field('integrity');
		if (chip) {
			var label = ['Not measured', 'All clear', 'Watch', 'Problem'][worst];
			chip.textContent = label;
			chip.className = 'satgps-tile-value satgps-health-chip ' +
				['is-muted', 'is-ok', 'is-warn', 'is-bad'][worst];
		}

		// On-chip odometer (NAV-ODO): hardware-filtered ground distance since the
		// receiver last powered up. Accurate at low speed and immune to GPS jitter,
		// unlike the server-side track integration used for "Distance" above.
		if (latest.odo_m != null && latest.odo_m >= 0) {
			var odoU = dist(latest.odo_m / 1000);
			setField('odo', odoU < 10 ? odoU.toFixed(2) : odoU.toFixed(1));
			var totalStr = (latest.odo_total_m != null)
				? ' · ' + dist(latest.odo_total_m / 1000).toFixed(0) + ' ' + distUnit + ' total'
				: '';
			setField('odo_sub', distUnit + ' · since power-on' + totalStr);
		}

		// Receiver identity (UBX MON-VER, read off the chip once at boot). Static,
		// so it stays blank in plain-NMEA mode until the device reports it.
		setField('rx_module', latest.rx_module || '—');
		setField('rx_fw', latest.rx_fw || '—');
		setField('rx_proto', latest.rx_proto || '—');
		if (latest.rx_gnss) {
			setField('rx_gnss', String(latest.rx_gnss).split(';')
				.map(function (g) { return gnssName(g.trim()); })
				.filter(Boolean).join(' · '));
		} else {
			setField('rx_gnss', '—');
		}

		// SBAS augmentation status (UBX NAV-SBAS): the real system in use (EGNOS over
		// the UK), the GEO satellite PRN, and how many SVs carry corrections — not
		// just the on/off flag from the fix.
		var sysName = sbasSysName(latest.sbas_sys);
		if (latest.sbas && (sysName || latest.sbas_prn > 0)) {
			setHealth('sbas_status', sysName || 'Active', 'ok');
			var prnStr = latest.sbas_prn > 0 ? 'GEO ' + latest.sbas_prn : 'active';
			var cnt = latest.sbas_cnt || 0;
			var cntStr = cnt > 0 ? ' · ' + cnt + ' sat' + (cnt === 1 ? '' : 's') : '';
			setField('sbas_sub', prnStr + cntStr);
		} else if (latest.sbas) {
			setHealth('sbas_status', 'Active', 'ok');
			setField('sbas_sub', 'differential corrections');
		} else if (latest.fix_type != null && latest.rx_module) {
			setHealth('sbas_status', 'Not in use', 'muted');
			setField('sbas_sub', sysName ? sysName + ' available' : 'no corrections yet');
		} else {
			setHealth('sbas_status', '—', 'muted');
			setField('sbas_sub', 'differential corrections');
		}
	}

	// ---- data flow --------------------------------------------------------
	function rangeToFrom(range) {
		var now = Date.now(), map = { '1h': 3600, '6h': 21600, '24h': 86400, '7d': 604800, '30d': 2592000 };
		var secs = map[range] || 86400;
		return new Date(now - secs * 1000).toISOString();
	}

	function refreshLatest() {
		return api('latest', { device: state.device }).then(function (latest) {
			updateStatus(latest);
			updateTiles(latest);
			if (latest.found) {
				updateMarker(latest);
				SatGPSSkyplot.draw(document.getElementById('satgps-skyplot'), latest.sats);
				updateSnrChart(latest.sats);
				updateConstellation(latest.sats);
			}
		}).catch(function () {
			var d = $('#satgps-status-dot'), t = $('#satgps-status-text');
			var gd = $('#satgps-gps-dot'), gt = $('#satgps-gps-text');
			if (d) d.className = 'satgps-status-dot is-offline';
			if (t) t.textContent = 'Dashboard can’t reach the server';
			if (gd) gd.className = 'satgps-status-dot';
			if (gt) gt.textContent = 'GPS —';
		});
	}

	function refreshHistory() {
		var rangeLabels = { '1h': 'last hour', '6h': 'last 6 hours', '24h': 'last 24 hours', '7d': 'last 7 days', '30d': 'last 30 days' };
		setField('range_label', '· ' + (rangeLabels[state.range] || ''));
		var params = { device: state.device, from: rangeToFrom(state.range), limit: 5000 };
		var p1 = api('track', params).then(function (res) {
			renderTrack(res.points || []);
			updateHistoryCharts(res.points || []);
		});
		var p2 = api('stats', { device: state.device, from: params.from }).then(function (s) {
			setField('distance', dist(s.distance_km).toFixed(2));
			setField('distance_unit', distUnit);
			setField('max_speed', spd(s.max_speed).toFixed(1));
			setField('max_speed_unit', spdUnit);
			setField('avg_speed', spd(s.avg_speed).toFixed(1));
			setField('avg_speed_unit', spdUnit);
			setField('moving_time', fmtDuration(s.moving_s));
			setField('max_alt', s.max_alt.toFixed(0));
			setField('points', s.points);
		});
		return Promise.all([p1, p2]).catch(function () {});
	}

	function loadDevices() {
		return api('devices').then(function (list) {
			var sel = $('#satgps-device');
			if (!sel) { return; }
			var opts = '<option value="">' + 'Any device' + '</option>';
			(list || []).forEach(function (d) {
				var dev = esc(d.device);
				opts += '<option value="' + dev + '">' + dev + '</option>';
			});
			sel.innerHTML = opts;
			if (state.device) { sel.value = state.device; }
		}).catch(function () {});
	}

	// ---- export (CSV / GPX) ----------------------------------------------
	// Everything the device sends is stored server-side, so the dashboard can
	// build a downloadable file from the selected range entirely in the browser.
	function isoZ(ts) { return String(ts || '').replace(' ', 'T') + 'Z'; }
	function triggerDownload(filename, mime, text) {
		var blob = new Blob([text], { type: mime });
		var url = URL.createObjectURL(blob);
		var a = document.createElement('a');
		a.href = url; a.download = filename;
		document.body.appendChild(a); a.click(); a.remove();
		setTimeout(function () { URL.revokeObjectURL(url); }, 1500);
	}
	function toCsv(points) {
		var cols = ['ts_utc', 'lat', 'lon', 'alt_m', 'speed_kmh', 'vspeed_ms', 'heading_deg', 'hdop', 'acc_h_m', 'sats_used'];
		var lines = [cols.join(',')];
		points.forEach(function (p) {
			lines.push([isoZ(p.ts), p.lat, p.lon, p.alt_m, p.speed_kmh, p.vspeed_ms, p.heading_deg, p.hdop, p.acc_h_m, p.sats_used].join(','));
		});
		return lines.join('\r\n') + '\r\n';
	}
	function toGpx(points, device) {
		var out = '<?xml version="1.0" encoding="UTF-8"?>\n' +
			'<gpx version="1.1" creator="Satellite GPS Tracker" xmlns="http://www.topografix.com/GPX/1/1">\n' +
			'  <trk><name>' + esc(device || 'track') + '</name><trkseg>\n';
		points.forEach(function (p) {
			out += '    <trkpt lat="' + p.lat + '" lon="' + p.lon + '"><ele>' + p.alt_m + '</ele><time>' + isoZ(p.ts) + '</time></trkpt>\n';
		});
		return out + '  </trkseg></trk>\n</gpx>\n';
	}
	function fileStamp() {
		var d = new Date(); function z(n) { return (n < 10 ? '0' : '') + n; }
		return d.getFullYear() + z(d.getMonth() + 1) + z(d.getDate()) + '-' + z(d.getHours()) + z(d.getMinutes());
	}
	function exportTrack(format, btn) {
		var label = btn ? btn.textContent : '';
		if (btn) { btn.disabled = true; btn.textContent = '…'; }
		function done() { if (btn) { btn.disabled = false; btn.textContent = label; } }
		api('track', { device: state.device, from: rangeToFrom(state.range), limit: 10000 }).then(function (res) {
			var pts = (res && res.points) || [];
			if (!pts.length) { alert('No fixes in the selected range to export.'); done(); return; }
			var dev = state.device || 'all';
			var base = 'satgps-' + dev.replace(/[^A-Za-z0-9_-]/g, '') + '-' + fileStamp();
			if (format === 'gpx') { triggerDownload(base + '.gpx', 'application/gpx+xml', toGpx(pts, dev)); }
			else { triggerDownload(base + '.csv', 'text/csv;charset=utf-8', toCsv(pts)); }
			done();
		}).catch(function () { alert('Export failed — could not fetch the track from the server.'); done(); });
	}

	// ---- help modal -------------------------------------------------------
	var HELP = {
		skyplot: '<h3>Sky plot</h3><p>Each dot is a satellite the receiver can hear right now. The centre of the circle is straight up (the zenith); the outer edge is the horizon. The direction around the circle is the compass bearing (N at the top). Colour shows signal strength. Satellites spread across the whole sky give the best position accuracy.</p>',
		snr: '<h3>Signal strength (C/N&#8320;)</h3><p>Carrier-to-noise density, in dB-Hz, is how cleanly the receiver hears each satellite. Above ~40 is excellent, 30-40 is usable, below 25 is marginal. Buildings, trees and windows lower it.</p>',
		rf: '<h3>Receiver health &amp; integrity</h3><p>These tiles are the receiver grading its own radio health and how far it trusts the fix — a separate question from "do we have a position?". Read the colours as <b>grey = not measured</b>, <b>green = healthy</b>, <b>amber = worth watching</b>, <b>red = a real problem</b>. Everything here is decoded from the u-blox UBX <i>MON-RF</i> and <i>NAV-STATUS</i> messages, so the whole section is blank in plain-NMEA mode. Tap any tile\'s <b>?</b> for detail.</p>',
		jam: '<h3>Interference</h3><p>Jamming is any radio signal loud enough to bury the faint GNSS signals (satellite power at the antenna is around a millionth of a billionth of a watt). The receiver watches its own front-end and rates the situation <b>Clear</b>, <b>Elevated</b> or <b>Jammed</b>. The small <b>CW</b> figure is its narrowband (continuous-wave) indicator — a nearby oscillator or a cheap "GPS blocker" pushes it up. Grey "Monitor off" means the interference monitor hasn\'t reported yet.</p>',
		agc: '<h3>RF gain (AGC)</h3><p>Automatic Gain Control is the receiver\'s automatic volume knob: it rides high and steady when the air is clean, and drops fast when something loud (a jammer) appears. So a steady reading is healthy and a <i>sudden lurch</i> is the interesting event — the exact percentage on its own does not mean good or bad. It is only flagged amber at the extremes (very low = front-end backing right off; very high = starved input, e.g. a disconnected antenna).</p>',
		noise: '<h3>Noise floor</h3><p>The broadband background noise level the receiver sees, straight from MON-RF. It is the counterpart to the narrowband CW indicator: a rising <i>noise floor</i> is the fingerprint of a wideband jammer, whereas a rising <i>CW</i> figure is a single narrowband carrier. The number is on the receiver\'s own internal scale, so watch it for changes rather than reading an absolute threshold.</p>',
		ant: '<h3>Antenna feed</h3><p>On boards wired for it, this catches a broken or shorted antenna cable. Doing so needs a small supervisor circuit that senses the current flowing up the coax to a <i>powered</i> (active) antenna. This tracker doesn\'t include that circuit, so the receiver can\'t tell and reports <b>Not sensed</b> — which is why it stays grey rather than green. That is expected here, not a fault.</p>',
		spoof: '<h3>Spoofing</h3><p>Where jamming shouts over the satellites, spoofing impersonates them — broadcasting counterfeit signals to trick the receiver about where or when it is. The M9N runs simple consistency checks and reports <b>None</b>, <b>Suspected</b> or <b>Detected</b>. It is a lightweight check, so a green "None" is good news but not absolute proof.</p>',
		ttff: '<h3>Time to first fix</h3><p>A stopwatch on the receiver\'s last boot: the time from power-on to its first position fix. Starting cold — no memory of the time, almanac or where it was — takes about half a minute; a warm restart is only seconds. A long time usually just means it booted somewhere with a poor view of the sky. It stays fixed after that first fix, so treat it as a startup score, not a live gauge.</p>',
		odo: '<h3>Odometer</h3><p>The receiver\'s own trip counter: cumulative ground distance travelled since it last powered up, computed on-chip. Because it is hardware-filtered it stays accurate at walking pace and shrugs off the GPS jitter that can inflate a distance worked out by joining up track points. The "total" figure is the module\'s lifetime distance across all trips.</p>',
		receiver: '<h3>Receiver</h3><p>The GNSS module\'s own identity, read straight off the chip at boot with the u-blox <i>MON-VER</i> message. <b>Module</b> is the silicon (a u-blox NEO-M9N here); <b>Firmware</b> is u-blox\'s on-chip software (their "SPG" standard-precision GNSS build) — separate from the tracker\'s own sketch version; <b>Protocol</b> is the UBX binary version the module speaks; and <b>Constellations</b> lists which satellite systems it is set to use.</p>',
		sbas: '<h3>SBAS augmentation</h3><p>SBAS (Satellite-Based Augmentation System) is a free correction service broadcast from geostationary satellites — <b>EGNOS</b> over Europe, WAAS over North America, MSAS over Japan, GAGAN over India. It sends small corrections that sharpen the fix and flag any satellite it judges unsafe to use. This tile (from UBX <i>NAV-SBAS</i>) names the system in use, the <b>GEO</b> satellite\'s PRN number the receiver is listening to, and how many satellites are currently being corrected. "Not in use" simply means no SBAS satellite is being tracked right now — often just a low southern-sky view.</p>',
		uart: '<h3>UART link load</h3><p>The GNSS module streams its data to the ESP32 over a serial (UART) link. This gauge, from UBX <i>MON-COMMS</i>, shows how full the receiver\'s transmit buffer for that link is — essentially the backlog of data waiting to be sent. Low and steady is healthy; a persistently <b>high</b> reading (amber) means the host isn\'t reading the stream fast enough, and an <b>overrun</b> (red) means the buffer filled completely and bytes were dropped. The "peak" figure is the highest level seen in the last monitoring window. It is a plumbing check on the link itself, separate from satellite signal quality.</p>'
	};
	function initHelp() {
		var modal = $('#satgps-help-modal'), content = $('#satgps-help-content');
		document.querySelectorAll('.satgps-help').forEach(function (btn) {
			btn.addEventListener('click', function () {
				content.innerHTML = HELP[btn.dataset.help] || '';
				modal.hidden = false;
			});
		});
		if (modal) {
			modal.addEventListener('click', function (e) {
				if (e.target === modal || e.target.classList.contains('satgps-modal-close')) { modal.hidden = true; }
			});
		}
	}

	// ---- boot -------------------------------------------------------------
	var liveTimer = null;
	function startLive() { stopLive(); if (state.live) { liveTimer = setInterval(refreshLatest, 5000); } }
	function stopLive() { if (liveTimer) { clearInterval(liveTimer); liveTimer = null; } }

	function bindControls() {
		var dev = $('#satgps-device');
		if (dev) dev.addEventListener('change', function () { state.device = dev.value; refreshLatest(); refreshHistory(); });
		var rng = $('#satgps-range');
		if (rng) rng.addEventListener('change', function () { state.range = rng.value; refreshHistory(); });
		var live = $('#satgps-live');
		if (live) live.addEventListener('change', function () { state.live = live.checked; startLive(); });
		var csvBtn = $('#satgps-export-csv');
		if (csvBtn) csvBtn.addEventListener('click', function () { exportTrack('csv', csvBtn); });
		var gpxBtn = $('#satgps-export-gpx');
		if (gpxBtn) gpxBtn.addEventListener('click', function () { exportTrack('gpx', gpxBtn); });
	}

	function init() {
		initMap();
		initCharts();
		updateSkyLegend();
		initHelp();
		bindControls();
		loadDevices().then(function () {
			return Promise.all([refreshLatest(), refreshHistory()]);
		});
		startLive();
	}

	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', init);
	} else {
		init();
	}
})();
