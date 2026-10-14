"""Static inventory of guest functions that build PM4 type-3 packet headers.

usage: pm4_writers.py <generated dir> <xenos.h> <out.json>
A type-3 header is 0xC0000000 | (count - 1) << 16 | opcode << 8, built by
lis rX,0xC0nn followed by ori rY,rX,opcode<<8. Addresses and opcode names only.
"""
import json, re, sys
from collections import defaultdict
from pathlib import Path

root, xenos_h, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
opcodes = {}
for m in re.finditer(r'(PM4_\w+)\s*=\s*(0x[0-9A-Fa-f]+)', xenos_h.read_text(errors='replace')):
    opcodes.setdefault(int(m.group(2), 16), m.group(1)[4:])

func_re = re.compile(r'^DEFINE_REX_FUNC\((sub_[0-9A-Fa-f]+)\)')
lis_re = re.compile(r'//\s*lis (r\d+),(-?\d+)')
ori_re = re.compile(r'//\s*(?:ori|addi) (r\d+),(r\d+),(\d+)$')
call_re = re.compile(r'//\s*bl (?:0x)?([0-9A-Fa-f]+)')
packets = defaultdict(lambda: defaultdict(set))  # func -> opcode -> counts
calls = defaultdict(set)
for f in sorted(root.glob('*.cpp')):
    func, lis = None, {}
    for line in f.read_text(errors='replace').splitlines():
        m = func_re.search(line)
        if m:
            func, lis = m.group(1), {}
            continue
        if func is None:
            continue
        m = lis_re.search(line)
        if m:
            lis[m.group(1)] = int(m.group(2))
            continue
        m = ori_re.search(line)
        if m and m.group(2) in lis:
            hi, lo = lis[m.group(2)], int(m.group(3))
            if -16384 <= hi <= -16384 + 0x3F and lo & 0xFF == 0 and (lo >> 8) in opcodes:
                packets[func][opcodes[lo >> 8]].add(hi + 16384 + 1)
            continue
        m = call_re.search(line)
        if m:
            calls[func].add('sub_' + m.group(1).upper())

by_opcode = defaultdict(list)
for func, ops in packets.items():
    for op in ops:
        by_opcode[op].append(func)
writers = set(packets)
# Callers of packet writers, one level up: candidates for the D3D API layer.
callers = defaultdict(list)
for func, callees in calls.items():
    for callee in callees & writers:
        callers[callee].append(func)
result = {
    'functions': {f: {op: sorted(c) for op, c in sorted(ops.items())} for f, ops in sorted(packets.items())},
    'by_opcode': {op: sorted(fs) for op, fs in sorted(by_opcode.items())},
    'callers': {f: sorted(c) for f, c in sorted(callers.items())},
}
out.write_text(json.dumps(result, indent=1))
print(f'{len(packets)} functions write type-3 headers; {sum(map(len, callers.values()))} call sites')
for op, fs in sorted(by_opcode.items(), key=lambda x: -len(x[1])):
    print(f'  {op:28s} {len(fs):4d}  {" ".join(sorted(fs)[:6])}{" ..." if len(fs) > 6 else ""}')
