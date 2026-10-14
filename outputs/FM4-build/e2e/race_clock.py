"""Race clock rate of an e2e run: the on-screen race timer against the capture times.

A ratio of 1.0 means the game advances in real time; 2.0 means it runs twice as fast
(for example when the frame rate is raised without the game scaling its time step).
Needs the captures kept as PPM (e2e.py --keep-ppm) and capture/captures.txt.

usage: race_clock.py <e2e run directory>
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e import Ocr  # noqa: E402

TIMER = re.compile(r'(\d{1,2})[:.](\d{2})[.,:](\d{3})')


def race_time(lines):
    """The race timer: the topmost m:ss.mmm text that is not the lap timer."""
    found = []
    for line in lines:
        text = line['text'].replace(' ', '')
        match = TIMER.search(text)
        if match and 'LAP' not in text.upper():
            minutes, seconds, millis = (int(g) for g in match.groups())
            found.append((line['y'], minutes * 60 + seconds + millis / 1000))
    return min(found)[1] if found else None


def main():
    run = Path(sys.argv[1])
    times = {}
    for line in (run / 'capture' / 'captures.txt').read_text().splitlines():
        index, ms = line.split()
        times[int(index)] = int(ms) / 1000
    ocr = Ocr()
    samples = []
    try:
        for ppm in sorted((run / 'capture').glob('frame-*.ppm')):
            index = int(ppm.stem.split('-')[1])
            if index not in times:
                continue
            value = race_time(ocr.read(str(ppm)))
            if value is not None:
                samples.append((times[index], value))
    finally:
        ocr.close()
    # Longest run of increasing timer readings (the race itself).
    best, current = [], []
    for sample in samples:
        if current and not (current[-1][1] < sample[1] < current[-1][1] + 10 * (sample[0] - current[-1][0]) + 1):
            if len(current) > len(best):
                best = current
            current = []
        current.append(sample)
    if len(current) > len(best):
        best = current
    if len(best) < 3:
        print(f'not enough race timer readings ({len(samples)} found)')
        return
    n = len(best)
    mean_wall = sum(w for w, _ in best) / n
    mean_race = sum(r for _, r in best) / n
    slope = (sum((w - mean_wall) * (r - mean_race) for w, r in best) /
             sum((w - mean_wall) ** 2 for w, _ in best))
    print(f'race clock / wall clock: {slope:.3f} over {n} captures '
          f'({best[0][1]:.1f} to {best[-1][1]:.1f} s race time)')


if __name__ == '__main__':
    main()
