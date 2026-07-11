#!/usr/bin/env python3
"""
Simulate the ESP32 satellite receiver by POSTing realistic telemetry to the
plugin's ingest endpoint. Use it to exercise the WordPress dashboard before the
hardware is ready, or to demo the plugin.

Examples
--------
Dry run (print one packet, send nothing):
    python simulate_device.py --dry-run

Send a moving track to your site:
    python simulate_device.py \\
        --url https://your-site/wp-json/satgps/v1/ingest \\
        --key YOUR_API_KEY --count 60 --interval 2

The JSON produced here mirrors src/wifi_uplink.cpp exactly.
"""
import argparse
import json
import math
import random
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime, timezone

CONSTELLATIONS = ['P', 'P', 'P', 'P', 'L', 'L', 'L', 'A', 'A', 'B', 'B', 'Q']


def make_sats(n):
    sats = []
    used = set()
    for _ in range(n):
        c = random.choice(CONSTELLATIONS)
        prn = random.randint(1, 32)
        key = (c, prn)
        if key in used:
            continue
        used.add(key)
        el = random.randint(5, 85)
        # Higher elevation satellites tend to have stronger signal.
        base = 22 + int(el * 0.28)
        snr = max(0, min(52, base + random.randint(-6, 8)))
        sats.append({
            'c': c,
            'prn': prn,
            'el': el,
            'az': random.randint(0, 359),
            'snr': snr,
            'u': 1 if snr > 28 else 0,  # roughly, strong sats are used in the fix
        })
    return sats


def build_packet(state, device):
    now = datetime.now(timezone.utc)
    sats = make_sats(random.randint(9, 18))
    used = sum(1 for s in sats if s['u'])
    hdop = state['hdop']
    geoid = 48.0  # typical geoid separation in the UK
    return {
        'device': device,
        'fw': 'sim-1.1',
        'ts': now.strftime('%Y-%m-%dT%H:%M:%SZ'),
        'date': now.strftime('%d/%m/%Y'),
        'time': now.strftime('%H:%M:%S'),
        'fix': state['fix'],
        'fix_type': 3 if state['fix'] else 0,
        'sats_used': used if state['fix'] else 0,
        'sats_in_view': len(sats),
        'lat': round(state['lat'], 6),
        'lon': round(state['lon'], 6),
        'alt_m': round(state['alt'], 1),
        'hae_m': round(state['alt'] + geoid, 1),
        'geoid_m': geoid,
        'speed_kmh': round(state['speed'], 2),
        'vspeed_ms': round(random.uniform(-1.5, 1.5), 2),
        'heading_deg': round(state['heading'], 1),
        'hdop': round(hdop, 2),
        'vdop': round(hdop * 1.4, 2),
        'pdop': round(hdop * 1.7, 2),
        'acc_h_m': round(hdop * 1.6 + random.uniform(0, 1.0), 2),
        'acc_v_m': round(hdop * 2.3 + random.uniform(0, 1.5), 2),
        'err_major_m': round(hdop * 1.5 + random.uniform(0, 0.8), 2),
        'err_minor_m': round(hdop * 1.0 + random.uniform(0, 0.5), 2),
        'err_orient_deg': round(random.uniform(0, 180), 1),
        'speed_acc_ms': round(random.uniform(0.1, 0.6), 2),
        'head_acc_deg': round(random.uniform(1, 20), 1),
        'sbas': random.random() > 0.4,
        'jam_state': 1 if random.random() > 0.12 else 2,
        'jam_ind': random.randint(0, 40),
        'agc_pct': random.randint(40, 75),
        'ant_status': 2,  # OK
        'spoof_state': 1 if random.random() > 0.05 else 2,
        'ttff_ms': state['ttff'],
        'uptime_ms': int((time.time() - state['boot']) * 1000),
        'sats': sats,
    }


def step(state):
    """Advance the simulated position along its heading."""
    # Occasionally change speed / heading like a real journey.
    state['speed'] = max(0.0, min(90.0, state['speed'] + random.uniform(-6, 6)))
    state['heading'] = (state['heading'] + random.uniform(-15, 15)) % 360
    state['alt'] = max(0.0, state['alt'] + random.uniform(-2, 2))
    state['hdop'] = max(0.5, min(4.0, state['hdop'] + random.uniform(-0.2, 0.2)))
    state['fix'] = random.random() > 0.03  # occasional dropout

    # Move: distance (km) this interval = speed(km/h) * dt(h)
    dt_h = state['interval'] / 3600.0
    dist_km = state['speed'] * dt_h
    dlat = (dist_km / 111.32) * math.cos(math.radians(state['heading']))
    dlon = (dist_km / (111.32 * math.cos(math.radians(state['lat'])))) * math.sin(math.radians(state['heading']))
    state['lat'] += dlat
    state['lon'] += dlon


def post(url, key, packet, timeout=8):
    data = json.dumps(packet).encode('utf-8')
    req = urllib.request.Request(url, data=data, method='POST')
    req.add_header('Content-Type', 'application/json')
    req.add_header('X-API-Key', key)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read().decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser(description='Simulate the satellite receiver.')
    ap.add_argument('--url', help='Ingest endpoint URL')
    ap.add_argument('--key', default='', help='API key (X-API-Key header)')
    ap.add_argument('--device', default='sim-01', help='Device id')
    ap.add_argument('--count', type=int, default=20, help='Packets to send (0 = forever)')
    ap.add_argument('--interval', type=float, default=2.0, help='Seconds between packets')
    ap.add_argument('--lat', type=float, default=51.5074, help='Start latitude')
    ap.add_argument('--lon', type=float, default=-0.1278, help='Start longitude')
    ap.add_argument('--dry-run', action='store_true', help='Print one packet and exit')
    args = ap.parse_args()

    state = {
        'lat': args.lat, 'lon': args.lon, 'alt': 35.0, 'speed': 15.0,
        'heading': random.uniform(0, 360), 'hdop': 1.2, 'fix': True,
        'interval': args.interval, 'boot': time.time(),
        'ttff': random.randint(2000, 30000),  # time-to-first-fix, stable per run
    }

    if args.dry_run:
        print(json.dumps(build_packet(state, args.device), indent=2))
        return

    if not args.url:
        ap.error('--url is required (or use --dry-run)')

    sent = 0
    ok = 0
    try:
        while args.count == 0 or sent < args.count:
            packet = build_packet(state, args.device)
            try:
                status, body = post(args.url, args.key, packet)
                ok += 1 if status < 300 else 0
                print(f"[{sent+1}] {status} lat={packet['lat']:.5f} lon={packet['lon']:.5f} "
                      f"spd={packet['speed_kmh']:.1f} sats={packet['sats_in_view']} -> {body[:80]}")
            except urllib.error.HTTPError as e:
                print(f"[{sent+1}] HTTP {e.code}: {e.read().decode('utf-8', 'replace')[:120]}")
            except urllib.error.URLError as e:
                print(f"[{sent+1}] connection error: {e.reason}")
            sent += 1
            step(state)
            if args.count == 0 or sent < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        pass

    print(f"\nSent {sent} packets, {ok} accepted.")
    sys.exit(0 if ok == sent and sent > 0 else 1)


if __name__ == '__main__':
    main()
