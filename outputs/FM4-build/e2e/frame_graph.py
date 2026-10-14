"""Print the frame graph of REX_FRAME_GRAPH traces (Stage B1): passes, resolves and which
pass reads which resolve output, with the surfaces each pass renders to, and the GPU time
of each pass and resolve when the trace has it (including render target transfers and
texture loads issued for that pass).

usage: frame_graph.py --build-root <BuildRoot> <trace.jsonl> [--frame N] [--list]
Enum names are read from <BuildRoot>\\runtime-source\\include\\rex\\graphics\\xenos.h.
"""
import argparse, json, re
from pathlib import Path


def enum(text, name):
    body = re.search(r'enum class ' + name + r'\b[^{]*\{(.*?)\};', text, re.S).group(1)
    return {int(m.group(2), 0): m.group(1).removeprefix('k_').removeprefix('k')
            for m in re.finditer(r'\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', body)}


def frames(path):
    """Frames in order; GPU times arrive a few frames later and are attached by frame."""
    result, by_number, current = [], {}, None
    for line in Path(path).read_text().splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue  # The process may be killed mid-line.
        if 'frame' in event:
            current = {'frame': event['frame'], 'events': [], 'gpu': None}
            by_number[event['frame']] = current
            result.append(current)
        elif 'gpu' in event:
            if event['gpu'] in by_number:
                by_number[event['gpu']]['gpu'] = event
        elif current is not None:
            current['events'].append(event)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', required=True)
    parser.add_argument('trace')
    parser.add_argument('--frame', type=int, help='frame number (default: the one with most draws)')
    parser.add_argument('--list', action='store_true', help='list sampled frames')
    options = parser.parse_args()
    xenos = (Path(options.build_root) / 'runtime-source/include/rex/graphics/xenos.h').read_text(errors='replace')
    color = enum(xenos, 'ColorRenderTargetFormat')
    depth = enum(xenos, 'DepthRenderTargetFormat')
    dest = enum(xenos, 'ColorFormat')
    sampled = [f for f in frames(options.trace) if f['events']]
    draws = lambda f: sum(e.get('draws', 0) for e in f['events'])
    if options.list:
        for f in sampled:
            gpu = f" GPU {sum(f['gpu']['events']):.2f} ms" if f['gpu'] else ''
            print(f"frame {f['frame']}: {draws(f)} draws, "
                  f"{sum('resolve' in e for e in f['events'])} resolves{gpu}")
        return
    frame = (next(f for f in sampled if f['frame'] == options.frame) if options.frame
             else max(sampled, key=draws))
    gpu = frame['gpu']
    times = gpu['events'] if gpu else []
    print(f"frame {frame['frame']}: {draws(frame)} draws" +
          (f", GPU {sum(times):.2f} ms in passes and resolves, {gpu['swap']:.2f} ms after the "
           f"swap, {gpu['gaps']:.2f} ms between submissions" if gpu else ''))
    names = {}  # resolve id -> short name used in this frame
    for event in frame['events']:
        index = event.get('event')
        ms = f"{times[index]:6.3f} ms " if index is not None and index < len(times) else ''
        print(ms, end='')
        if 'pass' in event:
            p = event['pass']
            targets = ' '.join(f"rt{i}@{base}:{color.get(fmt, fmt)}" for i, base, fmt in p['color']) or 'no color'
            if 'depth' in p:
                targets += f" depth@{p['depth'][0]}:{depth.get(p['depth'][1], p['depth'][1])}"
            reads = ' '.join(names.get(r, f'r{r}(earlier)') for r in event['reads'])
            extra = ''
            if event.get('transfers'):
                extra += f"  transfers {event['transfers']}"
            ranges = [f"{'depth' if slot == 0 else f'rt{slot - 1}'} {low}-{high}"
                      for slot, low, high in event.get('ranges', []) if low != high]
            if ranges:
                extra += f"  claimed tiles {', '.join(ranges)}"
            print(f"  pass  {event['draws']:5d} draws  pitch {p['pitch']} msaa {1 << p['msaa']}x "
                  f"offset {p['offset']}  {targets}" + (f"  reads {reads}" if reads else '') + extra)
        else:
            name = f"R{len(names)}"
            names[event['resolve']] = name
            select, base, fmt = event['source']
            source = (f"depth@{base}:{depth.get(fmt, fmt)}" if select >= 4
                      else f"rt{select}@{base}:{color.get(fmt, fmt)}")
            address, length, fmt, pitch, height = event['dest']
            clears = '+'.join(n for n, on in zip(('color', 'depth'), event['clear']) if on)
            print(f"  {name:4s}  resolve {source} -> {fmt and dest.get(fmt, fmt)} {pitch}x{height} "
                  f"at {address:08x} ({length} bytes)" + (f"  clear {clears}" if clears else ''))


if __name__ == '__main__':
    main()
