"""Save the latest application GPU capture, with session/timestamp evidence."""
from pathlib import Path
from PIL import Image
import argparse, io, json, time

p = argparse.ArgumentParser()
p.add_argument('--label')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
s = json.loads((root/'outputs/FM4-analysis/interactive-session.json').read_text(encoding='utf-8-sig'))
run = Path(s['run_directory'])
out = Path(__file__).resolve().parent/'user-play-latest.png'
frames = list((run/'frames').glob('*.ppm'))
if not frames:
    out.unlink(missing_ok=True)
    raise SystemExit('No GPU frame in the current session yet; no image saved.')
frame = max(frames, key=lambda p:p.stat().st_mtime)
for attempt in range(20):
    try:
        Image.open(io.BytesIO(frame.read_bytes())).save(out)
        break
    except (OSError, ValueError):
        if attempt == 19: raise
        time.sleep(.05)
info = dict(process_id=s['process_id'], run_directory=str(run), source=str(frame),
            age_seconds=round(time.time()-frame.stat().st_mtime,2), image=str(out))
if a.label:
    assert all(c.isalnum() or c in '-_' for c in a.label)
    evidence = run/'automatic-tests'
    evidence.mkdir(exist_ok=True)
    (evidence/(a.label+'.png')).write_bytes(out.read_bytes())
    (evidence/(a.label+'.json')).write_text(json.dumps(info,indent=2))
print(json.dumps(info))
