"""Summarize REX_GPU_INVENTORY files (Stage 3.1): shader pairs, render target,
texture, primitive and depth state the title uses, per scenario and combined.

usage: inventory.py --build-root <BuildRoot> <inventory.json> [...] [--json out.json]
Enum names are read from <BuildRoot>\\runtime-source\\include\\rex\\graphics\\xenos.h.
"""
import argparse, json, re
from collections import Counter
from pathlib import Path


def enum(text, name):
    body = re.search(r'enum class ' + name + r'\b[^{]*\{(.*?)\};', text, re.S).group(1)
    return {int(m.group(2), 0): m.group(1) for m in re.finditer(r'\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', body)}


def summarize(pairs, names):
    colors, textures, primitives, msaa, depth, modes = (Counter() for _ in range(6))
    mrt, dims, stages = Counter(), Counter(), Counter()
    for p in pairs:
        for r in p['render_targets']:
            written = (r >> 16) & 15
            fmt = '+'.join(names['color'].get((r >> (4 * i)) & 15, '?') for i in range(4) if written >> i & 1)
            colors[fmt or 'none'] += 1
            mrt[bin(written).count('1')] += 1
            if (r >> 21) & 1:
                depth[names['depth'].get((r >> 20) & 1, '?')] += 1
            msaa[('1x', '2x', '4x', '?')[(r >> 22) & 3]] += 1
        for t in p['textures']:
            textures[names['texture'].get(t & 63, str(t & 63))] += 1
            dims[('1D', '2D', '3D', 'cube')[(t >> 6) & 3]] += 1
            stages['pixel' if (t >> 11) & 1 else 'vertex'] += 1
        for x in p['primitives']:
            primitives[names['primitive'].get(x, str(x))] += 1
        for m in p['edram_modes']:
            modes[names['edram'].get(m, str(m))] += 1
    total = sum(p['draws'] for p in pairs)
    share = lambda c: dict(c.most_common())
    return {
        'shader_pairs': len(pairs), 'vertex_shaders': len({p['vs'] for p in pairs}),
        'pixel_shaders': len({p['ps'] for p in pairs if int(p['ps'], 16)}), 'draws': total,
        'memexport_pairs': sum(p['memexport'] for p in pairs),
        'blend_states': len({b for p in pairs for b in p['blend']}),
        'depth_states': len({d for p in pairs for d in p['depth']}),
        # Counts of (shader pair, state) combinations; a pair can use several states.
        'color_targets': share(colors), 'color_target_count': share(mrt), 'depth_formats': share(depth),
        'msaa': share(msaa), 'edram_modes': share(modes), 'primitives': share(primitives),
        'texture_formats': share(textures), 'texture_dimensions': dict(dims), 'texture_stages': dict(stages),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--json')
    parser.add_argument('inventories', nargs='+')
    options = parser.parse_args()
    xenos = (Path(options.build_root) / 'runtime-source/include/rex/graphics/xenos.h').read_text(errors='replace')
    names = {'color': enum(xenos, 'ColorRenderTargetFormat'), 'depth': enum(xenos, 'DepthRenderTargetFormat'),
             'texture': enum(xenos, 'TextureFormat'), 'primitive': enum(xenos, 'PrimitiveType'),
             'edram': enum(xenos, 'EdramMode')}
    result, combined = {}, {}
    for path in options.inventories:
        pairs = json.loads(Path(path).read_text())['pairs']
        result[Path(path).stem] = summarize(pairs, names)
        for p in pairs:
            key = (p['vs'], p['ps'])
            if key not in combined:
                combined[key] = {**p, 'render_targets': set(), 'textures': set(), 'primitives': set(),
                                 'edram_modes': set(), 'blend': set(), 'depth': set(), 'draws': 0}
            c = combined[key]
            c['draws'] += p['draws']
            c['memexport'] = c['memexport'] or p['memexport']
            for field in ('render_targets', 'textures', 'primitives', 'edram_modes', 'blend', 'depth'):
                c[field].update(p[field])
    if len(options.inventories) > 1:
        result['combined'] = summarize(list(combined.values()), names)
    for name, summary in result.items():
        print(f'== {name}')
        for key, value in summary.items():
            if isinstance(value, dict):
                value = ', '.join(f'{k} {v:.1%}' if isinstance(v, float) else f'{k} {v}' for k, v in list(value.items())[:12])
            print(f'  {key}: {value}')
    if options.json:
        Path(options.json).write_text(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
