#!/usr/bin/env python3
"""
Verify the ESP32 firmware and the WordPress plugin agree on the telemetry JSON.

Reads the *actual* source files:
  * firmware  : src/wifi_uplink.cpp  -> keys the device SENDS
  * plugin    : includes/class-satgps-rest.php -> keys the server READS

Fails if the server reads a key the device never sends.
"""
import os
import re
import sys

HERE = os.path.dirname(__file__)
FW = os.path.join(HERE, '..', '..', 'src', 'wifi_uplink.cpp')
PHP = os.path.join(HERE, '..', 'satellite-gps-tracker', 'includes', 'class-satgps-rest.php')


def read(path):
    with open(path, 'r', encoding='utf-8') as fh:
        return fh.read()


def esp_keys(src):
    """Top-level keys emitted in buildJson (pattern: \"key\": )."""
    top = set(re.findall(r'\\"([a-z_]+)\\":', src))
    # Keys inside the per-satellite objects use the same pattern; separate them
    # by looking only at the sats loop body.
    sat_block = src[src.find('"\\"sats\\":['):] if '\\"sats\\":[' in src else ''
    sat = set(re.findall(r'\\"([a-z_]+)\\":', sat_block)) - {'sats'}
    return top - sat, sat


def php_keys(src):
    body = set(re.findall(r"\$body\[\s*'([a-z_]+)'\s*\]", src))
    sat = set(re.findall(r"\$s\[\s*'([a-z_]+)'\s*\]", src))
    return body, sat


def main():
    fw = read(FW)
    php = read(PHP)

    esp_top, esp_sat = esp_keys(fw)
    php_top, php_sat = php_keys(php)

    print("Device sends (top-level):", ", ".join(sorted(esp_top)))
    print("Server reads (top-level):", ", ".join(sorted(php_top)))
    print("Device sends (per-sat)  :", ", ".join(sorted(esp_sat)))
    print("Server reads (per-sat)  :", ", ".join(sorted(php_sat)))
    print()

    problems = []

    missing_top = php_top - esp_top
    if missing_top:
        problems.append("Server reads top-level keys the device never sends: " + ", ".join(sorted(missing_top)))

    missing_sat = php_sat - esp_sat
    if missing_sat:
        problems.append("Server reads per-sat keys the device never sends: " + ", ".join(sorted(missing_sat)))

    extra_top = esp_top - php_top
    if extra_top:
        print("Note: device sends extra keys the server ignores (OK):", ", ".join(sorted(extra_top)))

    if problems:
        print("\nCONTRACT MISMATCH:")
        for p in problems:
            print("  - " + p)
        sys.exit(1)

    print("\nCONTRACT OK - device and server agree on every consumed field.")


if __name__ == '__main__':
    main()
