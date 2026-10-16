"""Run an e2e gate on a snapshot of the game build and summarize it against a baseline.

The snapshot (<BuildRoot>/snapshots/<name>) is the build that build-result.json describes, so
Build.ps1 -Action build can rebuild game-build while the gate runs. Cells run one at a time
(performance numbers need the machine to themselves) into <BuildRoot>/e2e/<name>-<hz>-<scenario>.

Tiers (README.txt, "Gates"):
  smoke  one run per scenario at 240 Hz: does it still work (about 10 minutes)
  perf   three runs of photo at 240 Hz: its race phase (12 cars, 30 s) and photo mode
  full   three runs of every scenario at 60 and 240 Hz: before committing a stage (about 1 hour)
An AI driver drives the player's car in every race (FM4_GAME_CMDLINE=engageaionstart; --human
for the harness throttle).

  python outputs\\FM4-build\\e2e\\matrix.py --build-root D:\\FM4-build --name vb --tier smoke --baseline reg
"""
import argparse, json, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import pipeline  # noqa: E402

TIERS = {
    'smoke': (('photo', 'photo_view', 'career'), (240,), 1),
    'perf': (('photo',), (240,), 3),
    'full': (('photo', 'photo_view', 'career'), (60, 240), 3),
}


def snapshot(root, source, name):
    """Copy the game build to <root>/snapshots/<name> after checking it against build-result.json."""
    report = json.loads((root / 'build-result.json').read_text())
    for artifact, expected in report['artifacts'].items():
        if pipeline.digest(root / source / artifact) != expected:
            raise SystemExit(f'{source}/{artifact} differs from build-result.json; finish the build first')
    target = root / 'snapshots' / name
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for item in (root / source).iterdir():
        if item.is_file() and item.suffix.lower() in ('.exe', '.dll', '.pdb', '.map'):
            shutil.copy2(item, target / item.name)
    return target.relative_to(root)


def cell_args(root, name, hz, scenario, repeat, build, extra):
    args = [sys.executable, HERE / 'e2e.py', '--build-root', root, '--game-build', build,
            '--out', root / 'e2e' / f'{name}-{hz}-{scenario}', '--scenario', scenario,
            '--repeat', str(repeat)]
    if hz != 60:
        args += [f'--arg=--video_mode_refresh_rate={hz}', '--env', f'FM4_FRAME_RATE={hz}']
    if scenario == 'quickrace':
        args += ['--tail', '40', '--measure', '30']
    if scenario in ('photo', 'career'):
        args += ['--profile', 'copy']
    if scenario == 'photo_view':
        # The photo saved by the photo cell of the same gate.
        args += ['--profile', root / 'e2e' / f'{name}-{hz}-photo' / ('run1' if repeat > 1 else '') / 'user-data']
    return args + extra


def describe(out):
    """One line per cell: runs that reached their goal, overall and phase fps / p99."""
    runs = sorted(out.glob('run[0-9]*')) or [out]
    passed, parts = 0, []
    for run in runs:
        path = run / 'summary.json'
        if not path.exists():
            continue
        s = json.loads(path.read_text())
        passed += not s.get('crashed') and s.get('goal_reached') is not False
    path = out / 'summary.json'
    if path.exists():
        s = json.loads(path.read_text())
        parts.append(f"fps {s.get('fps_mean')} p99 {s.get('frame_ms_p99')}")
        for phase, stats in (s.get('phases') or {}).items():
            parts.append(f"{phase} {stats.get('fps_mean')} fps p99 {stats.get('frame_ms_p99')}")
        if s.get('failed_steps'):
            parts.append(f"failed: {s['failed_steps']}")
    return f'{passed}/{len(runs)} ' + ', '.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--name', required=True, help='cells go to <BuildRoot>/e2e/<name>-<hz>-<scenario>')
    parser.add_argument('--tier', choices=sorted(TIERS), default='smoke')
    parser.add_argument('--scenarios', help='comma list instead of the tier scenarios')
    parser.add_argument('--hz', help='comma list of refresh rates instead of the tier ones')
    parser.add_argument('--repeat', type=int, help='runs per cell instead of the tier count')
    parser.add_argument('--baseline', help='compare each cell with <BuildRoot>/e2e/<baseline>-<hz>-<scenario>')
    parser.add_argument('--game-build', default='game-build', help='build directory to snapshot')
    parser.add_argument('--no-snapshot', action='store_true', help='run the build directory itself')
    parser.add_argument('--human', action='store_true',
                        help='drive with the harness input instead of the AI driver (engageaionstart)')
    parser.add_argument('--env', action='append', default=[], help='extra NAME=value for every cell')
    parser.add_argument('--arg', action='append', default=[], help='extra --cvar=value for every cell')
    options = parser.parse_args()
    root = options.build_root.resolve()
    scenarios, rates, repeat = TIERS[options.tier]
    scenarios = options.scenarios.split(',') if options.scenarios else scenarios
    rates = [int(r) for r in options.hz.split(',')] if options.hz else rates
    repeat = options.repeat or repeat
    if 'photo_view' in scenarios and 'photo' not in scenarios:
        parser.error('photo_view loads the photo of the photo cell; run photo too')
    scenarios = sorted(scenarios, key=lambda s: s != 'photo')
    build = Path(options.game_build) if options.no_snapshot else snapshot(root, options.game_build, options.name)
    # An AI driver drives the player's car, so every race follows the track and the load per
    # frame no longer depends on where a car held at full throttle ends up.
    env = options.env if options.human or any(e.startswith('FM4_GAME_CMDLINE=') for e in options.env) \
        else options.env + ['FM4_GAME_CMDLINE=engageaionstart']
    extra = [f'--env={e}' for e in env] + [f'--arg={a}' for a in options.arg]
    log = root / 'e2e' / f'{options.name}.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    started, results = time.time(), []
    for hz in rates:
        for scenario in scenarios:
            cell = f'{options.name}-{hz}-{scenario}'
            out = root / 'e2e' / cell
            t = time.time()
            with open(root / 'e2e' / f'{cell}.log', 'w', encoding='utf-8', errors='replace') as output:
                code = subprocess.call([str(a) for a in cell_args(root, options.name, hz, scenario, repeat, build, extra)],
                                       stdout=output, stderr=subprocess.STDOUT)
            verdict = ''
            if options.baseline:
                base = root / 'e2e' / f'{options.baseline}-{hz}-{scenario}'
                if (base / 'summary.json').exists() and (out / 'summary.json').exists():
                    report = subprocess.run([sys.executable, HERE / 'compare.py', base, out, '--json'],
                                            capture_output=True, text=True)
                    try:
                        verdict = ' ' + json.loads(report.stdout)['verdict'] + f' vs {base.name}'
                    except (ValueError, KeyError):
                        verdict = ' compare failed'
                else:
                    verdict = f' (no baseline {base.name})'
            line = f'{cell}: exit {code}, {(time.time() - t) / 60:.1f} min, {describe(out)}{verdict}'
            results.append(code)
            print(line, flush=True)
            with open(log, 'a', encoding='utf-8') as f:
                f.write(line + '\n')
    print(f'{options.tier} gate {options.name}: {sum(c == 0 for c in results)}/{len(results)} cells passed '
          f'in {(time.time() - started) / 60:.0f} min')
    sys.exit(1 if any(results) else 0)


if __name__ == '__main__':
    main()
