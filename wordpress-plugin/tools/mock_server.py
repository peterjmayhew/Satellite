#!/usr/bin/env python3
"""
Offline stand-in for the WordPress plugin's REST endpoints. Mirrors the ingest
auth + field coercion in includes/class-satgps-rest.php so the firmware payload
can be tested end-to-end without a WordPress install.

Run:
    python mock_server.py --port 8787 --key testkey123
Then point simulate_device.py at:
    http://localhost:8787/wp-json/satgps/v1/ingest
"""
import argparse
import json
import sqlite3
import hmac
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

NS = '/wp-json/satgps/v1'
API_KEY = ''
DB = None


def now_utc():
    return datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S')


def init_db():
    db = sqlite3.connect(':memory:', check_same_thread=False)
    db.execute("""CREATE TABLE fixes (
        id INTEGER PRIMARY KEY AUTOINCREMENT, device TEXT, ts TEXT, received_at TEXT,
        fix INTEGER, sats_used INTEGER, sats_in_view INTEGER, lat REAL, lon REAL,
        alt_m REAL, speed_kmh REAL, heading_deg REAL, hdop REAL,
        fix_type INTEGER, acc_h_m REAL, acc_v_m REAL, vspeed_ms REAL, hae_m REAL,
        geoid_m REAL, vdop REAL, pdop REAL, speed_acc_ms REAL, head_acc_deg REAL,
        sbas INTEGER, jam_state INTEGER, jam_ind INTEGER, agc_pct INTEGER,
        ant_status INTEGER, spoof_state INTEGER, ttff_ms INTEGER,
        err_major_m REAL, err_minor_m REAL, err_orient_deg REAL,
        uptime_ms INTEGER, sats_json TEXT)""")
    return db


def coerce(body):
    """Same shaping the PHP ingest() applies."""
    device = str(body.get('device', 'unknown'))[:32] or 'unknown'
    ts = now_utc()
    if body.get('ts'):
        try:
            ts = datetime.fromisoformat(str(body['ts']).replace('Z', '+00:00')).strftime('%Y-%m-%d %H:%M:%S')
        except ValueError:
            pass
    lat = float(body.get('lat', 0) or 0)
    lon = float(body.get('lon', 0) or 0)
    if not -90 <= lat <= 90:
        lat = 0.0
    if not -180 <= lon <= 180:
        lon = 0.0
    sats = []
    for s in (body.get('sats') or [])[:64]:
        if not isinstance(s, dict):
            continue
        sats.append({
            'c': (str(s.get('c', '?'))[:1] or '?').upper(),
            'prn': int(s.get('prn', 0)), 'el': int(s.get('el', 0)),
            'az': int(s.get('az', 0)), 'snr': int(s.get('snr', -1)),
            'u': 1 if s.get('u') else 0,
        })

    def f(key):
        return float(body.get(key, 0) or 0)

    return {
        'device': device, 'ts': ts, 'received_at': now_utc(),
        'fix': 1 if body.get('fix') else 0,
        'sats_used': max(0, int(body.get('sats_used', 0) or 0)),
        'sats_in_view': max(0, int(body.get('sats_in_view', 0) or 0)),
        'lat': lat, 'lon': lon,
        'alt_m': f('alt_m'),
        'speed_kmh': max(0.0, f('speed_kmh')),
        'heading_deg': f('heading_deg'),
        'hdop': max(0.0, f('hdop')),
        'fix_type': max(0, int(body.get('fix_type', 0) or 0)),
        'acc_h_m': max(0.0, f('acc_h_m')),
        'acc_v_m': max(0.0, f('acc_v_m')),
        'vspeed_ms': f('vspeed_ms'),
        'hae_m': f('hae_m'),
        'geoid_m': f('geoid_m'),
        'vdop': max(0.0, f('vdop')),
        'pdop': max(0.0, f('pdop')),
        'speed_acc_ms': max(0.0, f('speed_acc_ms')),
        'head_acc_deg': max(0.0, f('head_acc_deg')),
        'sbas': 1 if body.get('sbas') else 0,
        'jam_state': max(0, min(3, int(body.get('jam_state', 0) or 0))),
        'jam_ind': max(0, min(255, int(body.get('jam_ind', 0) or 0))),
        'agc_pct': max(0, min(100, int(body.get('agc_pct', 0) or 0))),
        'ant_status': max(0, min(4, int(body.get('ant_status', 0) or 0))),
        'spoof_state': max(0, min(3, int(body.get('spoof_state', 0) or 0))),
        'ttff_ms': max(0, int(body.get('ttff_ms', 0) or 0)),
        'err_major_m': max(0.0, f('err_major_m')),
        'err_minor_m': max(0.0, f('err_minor_m')),
        'err_orient_deg': f('err_orient_deg'),
        'uptime_ms': max(0, int(body.get('uptime_ms', 0) or 0)),
        'sats_json': json.dumps(sats),
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _cors(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, X-API-Key, X-WP-Nonce')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.send_header('Content-Length', '0')
        self.end_headers()

    def _send(self, code, obj):
        data = json.dumps(obj).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self._cors()
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        path = urlparse(self.path).path
        if path != NS + '/ingest':
            return self._send(404, {'error': 'not found'})
        provided = self.headers.get('X-API-Key', '')
        if not API_KEY or not hmac.compare_digest(API_KEY, provided):
            return self._send(401, {'code': 'satgps_unauthorized', 'message': 'Invalid or missing API key.'})
        try:
            length = int(self.headers.get('Content-Length', 0))
            body = json.loads(self.rfile.read(length) or b'{}')
        except (ValueError, TypeError):
            return self._send(400, {'error': 'bad json'})
        if not isinstance(body, dict):
            return self._send(400, {'error': 'expected object'})
        d = coerce(body)
        cols = list(d.keys())
        placeholders = ",".join("?" * len(cols))
        cur = DB.execute(
            "INSERT INTO fixes (%s) VALUES (%s)" % (",".join(cols), placeholders),
            [d[c] for c in cols])
        DB.commit()
        self._send(200, {'ok': True, 'id': cur.lastrowid, 'received_at': d['received_at']})

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        device = q.get('device', [''])[0]

        if u.path == NS + '/latest':
            where = 'WHERE device=?' if device else ''
            args = (device,) if device else ()
            row = DB.execute(f"SELECT * FROM fixes {where} ORDER BY ts DESC, id DESC LIMIT 1", args).fetchone()
            if not row:
                return self._send(200, {'found': False})
            cols = [c[0] for c in DB.execute("SELECT * FROM fixes LIMIT 0").description]
            r = dict(zip(cols, row))
            return self._send(200, {
                'found': True, 'device': r['device'], 'ts': r['ts'], 'received_at': r['received_at'],
                'age_seconds': 0, 'fix': r['fix'], 'fix_type': r['fix_type'],
                'lat': r['lat'], 'lon': r['lon'], 'alt_m': r['alt_m'],
                'hae_m': r['hae_m'], 'geoid_m': r['geoid_m'],
                'speed_kmh': r['speed_kmh'], 'vspeed_ms': r['vspeed_ms'],
                'heading_deg': r['heading_deg'], 'hdop': r['hdop'], 'vdop': r['vdop'], 'pdop': r['pdop'],
                'acc_h_m': r['acc_h_m'], 'acc_v_m': r['acc_v_m'],
                'speed_acc_ms': r['speed_acc_ms'], 'head_acc_deg': r['head_acc_deg'], 'sbas': r['sbas'],
                'jam_state': r['jam_state'], 'jam_ind': r['jam_ind'], 'agc_pct': r['agc_pct'],
                'ant_status': r['ant_status'], 'spoof_state': r['spoof_state'], 'ttff_ms': r['ttff_ms'],
                'err_major_m': r['err_major_m'], 'err_minor_m': r['err_minor_m'], 'err_orient_deg': r['err_orient_deg'],
                'sats_used': r['sats_used'], 'sats_in_view': r['sats_in_view'],
                'sats': json.loads(r['sats_json'] or '[]'),
            })

        if u.path == NS + '/track':
            where = 'WHERE fix=1' + (' AND device=?' if device else '')
            args = (device,) if device else ()
            cols = ['ts', 'lat', 'lon', 'alt_m', 'speed_kmh', 'vspeed_ms', 'heading_deg', 'hdop', 'acc_h_m', 'sats_used']
            rows = DB.execute(f"SELECT {','.join(cols)} FROM fixes {where} ORDER BY ts ASC", args).fetchall()
            pts = [dict(zip(cols, r)) for r in rows]
            return self._send(200, {'device': device, 'count': len(pts), 'points': pts})

        if u.path == NS + '/stats':
            where = 'WHERE fix=1' + (' AND device=?' if device else '')
            args = (device,) if device else ()
            rows = DB.execute(f"SELECT ts,lat,lon,alt_m,speed_kmh FROM fixes {where} ORDER BY ts ASC", args).fetchall()
            import math
            dist_km = 0.0
            moving_s = 0.0
            speeds = []
            alts = []
            prev = None
            for r in rows:
                ts, lat, lon, alt, spd = r
                speeds.append(spd)
                alts.append(alt)
                if prev:
                    dlat = math.radians(lat - prev[1])
                    dlon = math.radians(lon - prev[2])
                    a = math.sin(dlat / 2) ** 2 + math.cos(math.radians(prev[1])) * math.cos(math.radians(lat)) * math.sin(dlon / 2) ** 2
                    dist_km += 6371.0088 * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
                prev = r
            return self._send(200, {
                'device': device, 'points': len(rows),
                'distance_km': round(dist_km, 3),
                'moving_s': int(len(rows)),  # ~1s cadence approximation for the mock
                'max_speed': round(max(speeds), 1) if speeds else 0,
                'avg_speed': round(sum(speeds) / len(speeds), 1) if speeds else 0,
                'max_alt': round(max(alts), 1) if alts else 0,
                'min_alt': round(min(alts), 1) if alts else 0,
                'max_sats': 0, 'first_ts': rows[0][0] if rows else None, 'last_ts': rows[-1][0] if rows else None,
            })

        if u.path == NS + '/devices':
            rows = DB.execute("SELECT device, COUNT(*), MAX(ts) FROM fixes GROUP BY device").fetchall()
            return self._send(200, [{'device': r[0], 'points': r[1], 'last_ts': r[2], 'last_seen': r[2]} for r in rows])

        return self._send(404, {'error': 'not found'})


def main():
    global API_KEY, DB
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8787)
    ap.add_argument('--key', default='testkey123')
    args = ap.parse_args()
    API_KEY = args.key
    DB = init_db()
    srv = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f"mock server on http://127.0.0.1:{args.port}{NS}/  (key: {API_KEY})")
    srv.serve_forever()


if __name__ == '__main__':
    main()
