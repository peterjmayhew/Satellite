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
		var cls = { ok: ' is-ok', warn: ' is-warn', bad: ' is-bad' }[state] || '';
		el.className = 'satgps-tile-value' + cls;
	}
	function esc(s) {
		return String(s).replace(/[&<>"']/g, function (c) {
			return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
		});
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
		setField('fix_sub', latest.fix ? (ft + (latest.sbas ? ' · SBAS' : '')) : 'searching…');
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

		// Receiver health / integrity (UBX only; shows "Unknown" in NMEA mode)
		var js = latest.jam_state || 0;
		setHealth('jam', ['Unknown', 'OK', 'Warning', 'Critical'][js] || 'Unknown',
			js === 1 ? 'ok' : (js === 2 ? 'warn' : (js === 3 ? 'bad' : '')));
		setField('jam_sub', 'CW ind ' + (latest.jam_ind || 0));
		setField('agc', (latest.agc_pct != null ? latest.agc_pct : 0) + '%');
		var as = latest.ant_status || 0;
		setHealth('ant', ['Init', 'Unknown', 'OK', 'SHORT', 'OPEN'][as] || '—',
			as === 2 ? 'ok' : (as >= 3 ? 'bad' : ''));
		var ss = latest.spoof_state || 0;
		setHealth('spoof', ['Unknown', 'None', 'Indicated', 'Multiple'][ss] || 'Unknown',
			ss === 1 ? 'ok' : (ss === 2 ? 'warn' : (ss === 3 ? 'bad' : '')));
		setField('ttff', latest.ttff_ms > 0 ? (latest.ttff_ms / 1000).toFixed(1) + ' s' : '—');
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
		rf: '<h3>Receiver health &amp; integrity</h3><p>The receiver monitors its own radio front-end. <b>Interference / AGC</b> show whether nearby electronics or deliberate jamming are raising the noise floor (the automatic gain control winds up to compensate). <b>Antenna</b> reports an open or short circuit in the antenna feed. <b>Spoofing</b> flags signals that look artificially generated. <b>Time to fix</b> is how long the last cold/warm start took to acquire a position.</p>'
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
