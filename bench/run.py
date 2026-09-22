#!/usr/bin/env python3
"""Reproduce offline artifacts; never opens an audio device."""
import argparse
import csv
import datetime
import json
import hashlib
import os
from pathlib import Path
import platform
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build-bench')
parser.add_argument('--output', default=None)
parser.add_argument('--loopback-device', type=int, help='explicitly opt in to a wired/virtual full-duplex measurement')
parser.add_argument('--count', type=int, default=10000)
parser.add_argument('--chain', choices=['none','full'], default='none')
parser.add_argument('--skip-build', action='store_true', help='reuse an already validated build')
parser.add_argument('--offline-deps', action='store_true', help='use dependencies already fetched in build directory')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = (root / args.build).resolve()
out = Path(args.output).resolve() if args.output else build / 'results'
out.mkdir(parents=True, exist_ok=True)
configure = ['cmake', '-S', str(root), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release', '-DTALOSDSP_BUILD_BENCHMARKS=ON']
if args.offline_deps:
    configure += ['-DFETCHCONTENT_FULLY_DISCONNECTED=ON']
if not args.skip_build:
    subprocess.run(configure, check=True)
    subprocess.run(['cmake', '--build', str(build), '-j', '4'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'check'], check=True)
    subprocess.run([str(build / 'loopback_latency'), '--self-test'], check=True)
metadata = dict(timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                platform=platform.platform(), machine=platform.machine(), cpu=platform.processor(),
                logical_cpus=os.cpu_count(), revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
                backend='offline; no audio device', compiler=subprocess.check_output(['c++','--version'],text=True).splitlines()[0])
if platform.system() == 'Darwin':
    cpu = subprocess.run(['sysctl','-n','machdep.cpu.brand_string'],capture_output=True,text=True)
    metadata['cpu'] = cpu.stdout.strip() if cpu.returncode == 0 else 'unavailable (host restricts sysctl)'
    metadata['cpu_query_error'] = cpu.stderr.strip()
metadata['source_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for folder in ['src','bench','tests'] for p in sorted((root/folder).rglob('*'))
    if p.is_file() and p.suffix in ['.cpp','.h','.py'] and 'results' not in p.parts}
metadata['stage_sample_rate_hz'] = 48000
metadata['stage_channels'] = 1
metadata['stage_block_frames'] = [64, 256]
metadata['measurement'] = 'unpaced offline callback workload; queue saturation is expected; not I/O latency'
(out / 'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
clips = out / 'clips'
clips.mkdir(exist_ok=True)
for mode in ['stage', 'watchdog', 'classifier', 'features']:
    command = [str(build / 'talos_bench'), mode]
    if mode == 'classifier': command.append(str(clips))
    with (out / (mode+'.csv')).open('w') as file:
        subprocess.run(command, stdout=file, check=True)
rows = list(csv.DictReader((out / 'classifier.csv').open()))
matrices = {}
for row in rows:
    matrix = matrices.setdefault(row['rate'], [[0]*3 for _ in range(3)])
    matrix[int(row['label'])][int(row['predicted'])] += 1
summary = {'synthetic_only': True, 'class_order': ['percussive','tonal','ambient'],
           'confusion_rows_true_columns_predicted': matrices, 'stage_cost': []}
stages = list(csv.DictReader((out/'stage.csv').open()))
for combs in range(1,5):
    for enabled in range(2):
        for block in [64,256]:
            selected = [r for r in stages if (int(r['comb_count']),int(r['classifier_enabled']),int(r['block']))==(combs,enabled,block)]
            values = sorted(float(r['cost_us']) for r in selected)
            summary['stage_cost'].append(dict(combs=combs,classifier=bool(enabled),block=block,
                median_us=values[len(values)//2], p99_us=values[(len(values)*99+99)//100-1],
                maximum_us=values[-1], audio_drops=int(selected[0]['audio_drops'])))
(out / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
if args.loopback_device is not None:
    command = [str(build/'loopback_latency'), '--device', str(args.loopback_device), '--count', str(args.count), '--chain', args.chain]
    with (out/'loopback.csv').open('w') as raw, (out/'loopback-device.txt').open('w') as device:
        result = subprocess.run(command, stdout=raw, stderr=device)
    rows = list(csv.DictReader((out/'loopback.csv').open()))
    values = sorted(float(r['roundtrip_ms']) for r in rows if int(r['roundtrip_frames']) >= 0)
    if values:
        import math
        summary['loopback'] = dict(valid=len(values), missing=len(rows)-len(values), exit_code=result.returncode,
            min_ms=values[0], median_ms=values[math.ceil(.5*len(values))-1],
            p95_ms=values[math.ceil(.95*len(values))-1], p99_ms=values[math.ceil(.99*len(values))-1],
            p999_ms=values[math.ceil(.999*len(values))-1], max_ms=values[-1])
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    if result.returncode: raise SystemExit('Loopback failed or had missing returns/xruns; inspect saved raw results.')
print('Results:', out)
