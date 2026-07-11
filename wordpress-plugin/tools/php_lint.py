#!/usr/bin/env python3
"""Syntax-check every PHP file in the plugin using phply."""
import sys
import glob
import os
from phply import phplex
from phply.phpparse import make_parser

root = os.path.join(os.path.dirname(__file__), '..', 'satellite-gps-tracker')
files = sorted(glob.glob(os.path.join(root, '**', '*.php'), recursive=True))

fail = 0
for f in files:
    with open(f, 'r', encoding='utf-8') as fh:
        code = fh.read()
    rel = os.path.relpath(f, root)
    try:
        parser = make_parser()
        lexer = phplex.lexer.clone()
        parser.parse(code, lexer=lexer, tracking=True)
        print(f"OK   {rel}")
    except Exception as e:  # noqa: BLE001
        fail += 1
        print(f"FAIL {rel}: {e}")

print(f"\n{len(files)} files, {fail} failed")
sys.exit(1 if fail else 0)
