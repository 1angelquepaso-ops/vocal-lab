"""pyrex-engine analyze --stems a.wav b.wav ... [--label 116] [--bpm 116] --out result.json
Prints one line of JSON (house rule). Progress -> <out dir>/progress.txt."""
from __future__ import annotations

import argparse
import json
import os
import sys


def main(argv=None):
    ap = argparse.ArgumentParser(prog='pyrex-engine')
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('analyze')
    a.add_argument('--stems', nargs='+')
    a.add_argument('--folder')
    a.add_argument('--label', type=float)
    a.add_argument('--bpm', type=float)
    a.add_argument('--out', required=True)
    r_ = sub.add_parser('render')
    r_.add_argument('--analysis', required=True, help='result.json from analyze')
    r_.add_argument('--bpm', type=float, required=True, help='FL project BPM')
    r_.add_argument('--sr', type=int, default=44100, help='FL project sample rate')
    r_.add_argument('--out-dir', required=True)
    r_.add_argument('--out', required=True)
    args = ap.parse_args(argv)
    if args.cmd == 'render':
        return render_cmd(args)
    from .analyze import analyze_song, label_bpm, song_title
    paths = args.stems or []
    if args.folder:
        paths += sorted(os.path.join(args.folder, f) for f in os.listdir(args.folder)
                        if f.lower().endswith('.wav') and 'part_' not in f.lower())
    folder = args.folder or os.path.dirname(paths[0])
    label = args.label if args.label else label_bpm(folder)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    prog = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'progress.txt')

    def say(f, m):
        with open(prog, 'w', encoding='utf-8') as fh:
            fh.write(f'{f:.2f}|{m}')
    try:
        r = analyze_song(paths, label=label, bpm_override=args.bpm, progress=say)
        r['title'] = song_title(folder)
    except Exception as e:                                       # noqa: BLE001
        r = dict(ok=False, error=f'{type(e).__name__}: {e}')
    with open(args.out, 'w', encoding='utf-8') as fh:
        json.dump(r, fh, ensure_ascii=False, indent=1)
    summary = {k: r.get(k) for k in ('ok', 'error', 'title', 'questions', 'notes')}
    if r.get('ok'):
        c = r['clock']
        summary.update(bpm=c['bpm'], level=c['level'], key=r['key'].get('key'))
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if r.get('ok') else 1


def render_cmd(args):
    from .check import check_song
    from .render import compensate, render_song
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    prog = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'progress.txt')

    def say(f, m):
        with open(prog, 'w', encoding='utf-8') as fh:
            fh.write(f'{f:.2f}|{m}')
    try:
        with open(args.analysis, encoding='utf-8') as fh:
            an = json.load(fh)
        if not an.get('ok'):
            raise ValueError(an.get('error') or 'analysis failed')
        res = render_song(an, args.bpm, args.out_dir, out_sr=args.sr,
                          progress=lambda f, m: say(0.85 * f, m))
        say(0.88, 'Removing the stretcher delay')
        res = compensate(an, res)
        ck = check_song(an, res, progress=lambda f, m: say(0.9 + 0.09 * f, m))
        r = dict(ok=True, level=ck['level'], problems=ck['problems'], files=res['files'], plan=res['plan'],
                 latency_ms=res['latency_ms'], check=ck, drag_allowed=ck['level'] != 'red')
    except Exception as e:                                       # noqa: BLE001
        r = dict(ok=False, error=f'{type(e).__name__}: {e}')
    with open(args.out, 'w', encoding='utf-8') as fh:
        json.dump(r, fh, ensure_ascii=False, indent=1)
    say(1.0, 'Done')
    print(json.dumps({k: r.get(k) for k in ('ok', 'error', 'level', 'problems', 'drag_allowed')}, ensure_ascii=False))
    return 0 if r.get('ok') else 1


if __name__ == '__main__':
    sys.exit(main())
