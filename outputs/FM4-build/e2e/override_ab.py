"""Compares the captures of a run with shader_override_ab_ms: in every other period of the
steady clock the runtime draws with the DXBC translations instead of the HLSL overrides, so
in a static scene (photo mode, a paused race) consecutive captures from the two phases show
whether an override renders the same image.

  e2e.py ... --keep-ppm --arg shader_override_path=<dir> --arg shader_override_ab_ms=3000
  override_ab.py <run out> --period-ms 3000 [--diff <png>]

Each capture's phase comes from its time in capture/captures.txt (steady clock, like the
runtime's phase); captures within --margin-ms of a phase change are skipped, since the frame
was rendered before the capture was written. Consecutive pairs are listed: in a static scene
same-phase pairs are identical, and on/off pairs are identical if the overrides are."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from e2e import read_ppm, write_png  # noqa: E402


def phase(milliseconds, period, margin):
    offset = milliseconds % period
    if offset < margin or period - offset < margin:
        return None
    return 'off' if (milliseconds // period) % 2 else 'on'


def difference(a, b):
    """Differing bytes and the mean absolute difference of a sample of them."""
    if a == b:
        return 0, 0.0
    xor = (int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')).to_bytes(len(a), 'little')
    differing = len(xor) - xor.count(0)
    sample = [abs(x - y) for x, y in zip(a[::97], b[::97]) if x != y]
    return differing, (sum(sample) / len(sample)) if sample else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', help='e2e.py --out directory of a --keep-ppm run')
    parser.add_argument('--period-ms', type=int, required=True)
    parser.add_argument('--margin-ms', type=int, default=700)
    parser.add_argument('--diff', help='write |on - off| x16 of the first differing on/off pair')
    options = parser.parse_args()
    capture = pathlib.Path(options.run) / 'capture'
    times = {}
    for line in (capture / 'captures.txt').read_text().split('\n'):
        if line.strip():
            index, milliseconds = map(int, line.split())
            times[index] = milliseconds
    frames = []
    for index in sorted(times):
        path = capture / f'frame-{index:03d}.ppm'
        state = phase(times[index], options.period_ms, options.margin_ms)
        if path.is_file() and state:
            frames.append((index, state, path))

    counts = {}
    diff_written = False
    previous = None
    for index, state, path in frames:
        width, height, rgb = read_ppm(path)
        if previous and previous[0] == index - 1:
            _, previous_state, previous_rgb = previous
            kind = 'same' if previous_state == state else 'on/off'
            differing, mean = difference(previous_rgb, rgb)
            result = 'identical' if not differing else f'{differing} bytes differ, mean {mean:.1f}'
            counts[kind, not differing] = counts.get((kind, not differing), 0) + 1
            print(f'{index - 1:3d} {previous_state:>3} -> {index:3d} {state:>3}  {kind:6} {result}')
            if differing and kind == 'on/off' and options.diff and not diff_written:
                on, off = (previous_rgb, rgb) if previous_state == 'on' else (rgb, previous_rgb)
                amplified = bytes(min(255, abs(x - y) * 16) for x, y in zip(on, off))
                write_png(pathlib.Path(options.diff), width, height, amplified, 2)
                diff_written = True
        previous = (index, state, rgb)
    print('summary:', ', '.join(f'{kind} {"identical" if same else "different"}: {count}'
                                for (kind, same), count in sorted(counts.items())) or 'no pairs')


if __name__ == '__main__':
    main()
