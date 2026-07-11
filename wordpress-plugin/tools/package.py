#!/usr/bin/env python3
"""Package the plugin folder into an installable WordPress zip."""
import os
import zipfile

HERE = os.path.dirname(__file__)
BASE = os.path.join(HERE, '..')
ROOT = 'satellite-gps-tracker'
OUT = os.path.join(BASE, 'satellite-gps-tracker.zip')

if os.path.exists(OUT):
    os.remove(OUT)

files = []
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED) as z:
    for dirpath, _dirs, names in os.walk(os.path.join(BASE, ROOT)):
        for n in sorted(names):
            full = os.path.join(dirpath, n)
            arc = os.path.relpath(full, BASE).replace(os.sep, '/')
            z.write(full, arc)
            files.append(arc)

print('Created %s (%.1f KB)' % (OUT, os.path.getsize(OUT) / 1024))
print('\nPlugin contents:')
for f in sorted(files):
    print('   ' + f)
