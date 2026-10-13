"""Reject silent branch loss in SDK 0.10.0 output; preserve explicit diagnostics."""
from pathlib import Path
import json
import re
import sys

root = Path(sys.argv[1])
is_module = len(sys.argv) > 3 and sys.argv[3] == '--module'
# The two BDZ instructions target a zero word, not a translated function.
if not is_module:
    image = Path(sys.argv[2]).read_bytes()
    assert image[0x827B90BC - 0x82000000:0x827B90C0 - 0x82000000] == bytes(4)
old = 'if (ctx.ctr.u32 == 0) { /* branch to 0x827B90BC outside function */ return; }'
new = 'if (ctx.ctr.u32 == 0) { REX_FATAL("FM4 BDZ reached undecodable word at 0x827B90BC"); }'
guarded = 0
half_sites = 0
vector_sites = {}
errors = []
function_count = 0
half_pattern = re.compile(r'(?m)^(\t// (vpkd3d128|vupkd3d128) v(\d+),v(\d+),([^\n]+)\n)(.*?)(?=^\t// |^loc_|^\})', re.S)
vector_names = r'vmaddfp128|vmaddcfp128|vmaddfp|vnmsubfp128|vnmsubfp|vmsum3fp128|vmsum4fp128'
vector_pattern = re.compile(r'(?m)^(\t// (' + vector_names + r') ([^\n]+)\n)'
                            r'(\tctx\.fpscr\.enableFlushMode(?:Unconditional)?\(\);\n)?'
                            r'\t((?:simde_mm_store_ps|FM4VectorFMA|FM4VectorDot)\([^\n]+\);)\n')

def migrate_vector(match):
    kind = match[2]
    registers = match[3].strip().split(',')
    assert all(re.fullmatch(r'v(?:[0-9]|[1-9][0-9]|1[01][0-9]|12[0-7])', r) for r in registers)
    args = ['ctx.' + r for r in registers]
    load = lambda r: f'simde_mm_load_ps({r}.f32)'
    if kind.startswith('vmsum'):
        assert len(args) == 3
        d, a, b = args
        count = int(kind[5])
        mask = '0xEF' if count == 3 else '0xFF'
        original = f'simde_mm_store_ps({d}.f32, simde_mm_dp_ps({load(a)}, {load(b)}, {mask}));'
        replacement = f'FM4VectorDot({d}, {a}, {b}, {count});'
    else:
        assert len(args) == 4
        d, a, b, c = args
        negative = kind.startswith('vnmsub')
        product = f'simde_mm_mul_ps({load(a)}, {load(b)})'
        if negative:
            expression = f'simde_mm_xor_ps(simde_mm_sub_ps({product}, {load(c)}), simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x80000000))))'
        else:
            expression = f'simde_mm_add_ps({product}, {load(c)})'
        original = f'simde_mm_store_ps({d}.f32, {expression});'
        replacement = f'FM4VectorFMA({d}, {a}, {b}, {c}, {str(negative).lower()});'
    assert match[5] in (original, replacement), f'Unexpected emitter body for {match[0]}'
    vector_sites[kind] = vector_sites.get(kind, 0) + 1
    return match[1] + (match[4] or '') + '\t' + replacement + '\n'
def migrate_half(match):
    global half_sites
    kind, dest, src = match[2], int(match[3]), int(match[4])
    operands = [int(v) for v in match[5].strip().split(',')]
    mode = operands[0] if kind == 'vpkd3d128' else operands[0] >> 2
    if mode not in (3, 5):
        return match[0]
    assert 0 <= dest < 128 and 0 <= src < 128
    count = 2 if mode == 3 else 4
    if kind == 'vpkd3d128':
        assert len(operands) == 3 and 1 <= operands[1] <= 3 and 0 <= operands[2] <= 3
        code = f'FM4PackHalf(ctx.v{dest}, ctx.v{src}, {count}, {operands[1]}, {operands[2]});'
    else:
        code = f'FM4UnpackHalf(ctx.v{dest}, ctx.v{src}, {count});'
    half_sites += 1
    return match[1] + '\t' + code + '\n'
sources = list(root.glob('*recomp*.cpp'))
assert sources, f'No generated source files in {root}'
for path in sources:
    text = path.read_text()
    guarded += text.count(old) + text.count(new)
    updated = text.replace(old, new)
    updated = half_pattern.sub(migrate_half, updated)
    expected_vector_count = len(re.findall(r'(?m)^\t// (' + vector_names + r') ', updated))
    updated, vector_count = vector_pattern.subn(migrate_vector, updated)
    assert vector_count == expected_vector_count, f'{path}: vector migration missed {expected_vector_count - vector_count} sites'
    if vector_count:
        include = '#include "fm4-vector-fixes.h"\n'
        if include not in updated:
            updated = include + updated
    if 'FM4PackHalf(' in updated or 'FM4UnpackHalf(' in updated:
        include = '#include "fm4-instruction-fixes.h"\n'
        if include not in updated:
            updated = include + updated
    if updated != text:
        path.write_text(updated)
    for line in updated.splitlines():
        if 'outside function */ return;' in line or '// FATAL: unresolved' in line:
            errors.append(f'{path.name}: {line.strip()}')
    functions = list(re.finditer(r'(?m)^DEFINE_REX_FUNC\((\w+)\) \{', updated))
    assert functions, f'No function definitions recognized in {path}'
    function_count += len(functions)
    for i, function in enumerate(functions):
        end = functions[i + 1].start() if i + 1 < len(functions) else len(updated)
        body = updated[function.start():end]
        defined = set(re.findall(r'(?m)^(loc_[0-9A-F]+):', body))
        referenced = set(re.findall(r'goto (loc_[0-9A-F]+)', body))
        for missing in referenced - defined:
            errors.append(f'{path.name}: {function[1]} has missing label {missing}')
expected_guards = 0 if is_module else 2
assert guarded == expected_guards, f'Expected {expected_guards} audited BDZ guards, found {guarded}'
report = {'guarded_bdz_sites': guarded, 'float16_instruction_sites': half_sites,
          'vector_instruction_sites': vector_sites,
          'source_files': len(sources), 'function_definitions': function_count,
          'unresolved_generated_diagnostics': errors,
          'guard_target_word': None if is_module else '00000000',
          'target': None if is_module else '0x827B90BC',
          'playability_verified': False}
audit_name = f'{root.name}-generated-guard-audit.json' if is_module else 'generated-guard-audit.json'
(root.parent.parent / audit_name).write_text(json.dumps(report, indent=2))
if errors:
    raise SystemExit('\n'.join(errors))
print(f'{root.name}: {guarded} BDZ guards, {half_sites} float16 sites, {sum(vector_sites.values())} vector arithmetic sites, no unresolved direct-call/label diagnostics.')
