#!/usr/bin/env python3
"""Alternate two host benchmark binaries; reject changed output checksums."""
import argparse
import csv
import io
import statistics
import subprocess
p=argparse.ArgumentParser()
p.add_argument('before')
p.add_argument('after')
p.add_argument('--runs',type=int,default=10)
p.add_argument('--lit',action='store_true')
a=p.parse_args()
if a.runs<3:p.error('--runs must be at least 3')
data=[{},{}]
checks={}
for run in range(a.runs+2):
    for k in ([0,1] if run%2==0 else [1,0]):
        cmd=[[a.before,a.after][k]]
        # Empty filename disables image output while selecting the lit workload.
        if a.lit:cmd+=['','lit']
        rows=csv.DictReader(io.StringIO(subprocess.check_output(cmd,text=True)))
        for row in rows:
            preset=int(row['preset'])
            previous=checks.setdefault(preset,row['checksum'])
            if previous!=row['checksum']:raise SystemExit(f'image checksum changed for preset {preset}')
            if run>=2:data[k].setdefault(preset,[]).append(float(row['ms']))
if data[0].keys()!=data[1].keys():raise SystemExit('preset sets differ')
print('preset,before_ms,after_ms,time_reduction_percent')
for preset in data[0]:
    before,after=[statistics.median(d[preset]) for d in data]
    print(f'{preset},{before:.3f},{after:.3f},{100*(1-after/before):.1f}')
