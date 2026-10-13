"""Write an application-local controller state atomically, with sharing retries."""
from pathlib import Path
import argparse
import os
import time

p = argparse.ArgumentParser()
p.add_argument('buttons', type=int)
p.add_argument('--lt', type=int, default=0)
p.add_argument('--rt', type=int, default=0)
p.add_argument('--lx', type=int, default=0)
p.add_argument('--ly', type=int, default=0)
p.add_argument('--rx', type=int, default=0)
p.add_argument('--ry', type=int, default=0)
p.add_argument('--seconds', type=float, default=0)
a = p.parse_args()
assert 0 <= a.buttons <= 65535
assert all(0 <= x <= 255 for x in (a.lt, a.rt))
assert all(-32768 <= x <= 32767 for x in (a.lx, a.ly, a.rx, a.ry))
assert 0 <= a.seconds <= 30
target = Path(__file__).resolve().parent / 'test-input.txt'

def write(values):
    temporary = target.with_name(f'test-input-{os.getpid()}.tmp')
    temporary.write_text(' '.join(map(str, values)) + '\n', encoding='ascii')
    try:
        for attempt in range(100):
            try:
                temporary.replace(target)
                return
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.01)
    finally:
        temporary.unlink(missing_ok=True)

try:
    write((a.buttons, a.lt, a.rt, a.lx, a.ly, a.rx, a.ry))
    if a.seconds:
        time.sleep(a.seconds)
finally:
    if a.seconds:
        write((0, 0, 0, 0, 0, 0, 0))
