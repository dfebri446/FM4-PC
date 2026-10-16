"""Material atlas: which game material each translated shader belongs to.

Matches the ucode of a dump_shaders run (shader_<hash>.ucode.bin.frag/.vert) with the shader
containers of the user's game: the car and effect shaders in Media\\Shaders (.fxobj), the track
material shaders inside Media\\tracks\\<track>\\bin.zip (XMemCompress LZX, zip method 21,
decompressed with the runtime's LZX decoder in <BuildRoot>\\game-build\\rexruntime.dll) and the
engine shaders in default.xex (<BuildRoot>\\loaded-image.bin). Each match gives the material
(the track shader file name, or for car shaders a class from the names in the constant table),
the constant and sampler names with their registers (c#, and s# = texture fetch constant), and
with REX_GPU_INVENTORY files the draws, primitives, render targets and partner shaders.

  material_atlas.py --build-root <BuildRoot> --dump <dump_shaders dir> [--inventory inv.json ...]
      [--json <out.json>] [--top N]

The output names the game's materials and constants: keep it in the BuildRoot.
"""
import argparse, collections, ctypes, json, os, re, struct, zipfile, zlib, hashlib
from pathlib import Path

REGISTER_SETS = ('b', 'i', 'c', 's')
PRIMITIVES = {1: 'point', 2: 'line', 3: 'linestrip', 4: 'tri', 5: 'fan', 6: 'strip', 8: 'rect',
              12: 'lineloop', 13: 'quad', 14: 'quadstrip'}
# Track shader file names start with their material: ROAD_BLND_REFL_NORM_4, TREECARD_..., ...
TRACK_CLASSES = [('ROAD', 'road'), ('TERR_ROAD', 'road'), ('RDEDG', 'road edge'), ('RDLINE', 'road line'),
                 ('RDDET', 'road detail'), ('SHLDR', 'shoulder'), ('TERR', 'terrain'),
                 ('TREECARD', 'tree card'), ('TREE', 'tree'), ('BUSH', 'bush'), ('GRASS', 'grass'),
                 ('HS_SKY', 'sky'), ('SKY', 'sky'), ('UI_SKY', 'sky'), ('SIGN', 'sign'),
                 ('BARR', 'barrier'), ('CHAIN', 'fence'), ('LAKE', 'water'), ('RIVER', 'water'),
                 ('OCEAN', 'water'), ('ANIM', 'animated object'), ('FLARE', 'flare'),
                 ('SUN', 'sun'), ('HS_', 'homespace'), ('UI_', 'ui')]
# Car shader classes from constant and sampler names, first match wins.
CAR_CLASSES = [('Ghost', 'ghost'), ('GlassColor', 'glass'), ('DamageGlass', 'glass'),
               ('Cockpit', 'cockpit'), ('CarbonFiber', 'carbon fiber'), ('Chrome', 'chrome'),
               ('tread', 'tire'), ('sidewall', 'tire'), ('blurTire', 'tire'), ('blurRim', 'rim'),
               ('Brake', 'brake'), ('Rotor', 'brake'), ('Headlight', 'lights'), ('LightsSampler', 'lights'),
               ('Gauge', 'gauge'), ('CarDriver', 'driver'), ('Undercarriage', 'undercarriage'),
               ('PaintScale', 'paint'), ('PaintColor', 'paint'), ('TwoTone', 'paint'),
               ('TrackGeoShadow', 'car shadow'), ('Metal', 'metal')]


def containers(data):
    """(offset, container) of the shader containers in data (big-endian, as the title loads them)."""
    i = 0
    while True:
        i = data.find(b'\x10\x2a\x11', i)
        if i < 0:
            return
        if i % 4 == 0 and i + 36 <= len(data):
            flags, vsz, psz, _, _, _, _, f1c, f20 = struct.unpack_from('>9I', data, i)
            if (flags & 0xFFFFFF00) == 0x102A1100 and f1c == 0 and f20 == 0 and vsz + psz <= len(data) - i:
                yield i, data[i:i + vsz + psz]
                i += vsz + psz
                continue
        i += 4


def microcode(c):
    _, vsz, _, _, _, _, so = struct.unpack_from('>7I', c)
    start = vsz + struct.unpack_from('>I', c, so)[0]
    return c[start:start + struct.unpack_from('>I', c, so + 4)[0]]


def constant_table(c):
    """(name, register set, index, count) of the container's constant table."""
    cto = struct.unpack_from('>I', c, 16)[0]
    t = cto + 4
    try:
        _, _, _, n, info, _, _ = struct.unpack_from('>7I', c, t)
        table = []
        for k in range(min(n, 512)):
            name, regset, index, count = struct.unpack_from('>IHHH', c, t + info + k * 20)
            text = c[t + name:t + name + 96].split(b'\0')[0].decode('latin1')
            table.append((text, REGISTER_SETS[regset] if regset < 4 else str(regset), index, count))
        return table
    except (struct.error, IndexError):
        return []


class Lzx:
    """XMemCompress (LZX, 128 KB window) through the runtime's lzx_decompress."""

    def __init__(self, build_root):
        directory = Path(build_root) / 'game-build'
        os.add_dll_directory(str(directory))
        dll = ctypes.CDLL(str(directory / 'rexruntime.dll'))
        self.decompress = getattr(dll, '?lzx_decompress@@YAHPEBX_KPEAX1I21@Z')
        self.decompress.restype = ctypes.c_int
        self.decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t]

    def inflate(self, data, size):
        # Frames: [0xFF, uncompressed size (short frames only)], compressed size, LZX data.
        stream, i = bytearray(), 0
        while i + 2 <= len(data):
            if data[i] == 0xFF:
                i += 3
            length = struct.unpack_from('>H', data, i)[0]
            i += 2
            if not length:
                break
            stream += data[i:i + length]
            i += length
        out = ctypes.create_string_buffer(size)
        if self.decompress(bytes(stream), len(stream), out, size, 0x20000, None, 0):
            return None
        return out.raw


def zip_entries(path, suffix, lzx):
    """(name, data) of the entries of a title archive ending with suffix."""
    archive = zipfile.ZipFile(path)
    with open(path, 'rb') as f:
        for info in archive.infolist():
            if not info.filename.lower().endswith(suffix):
                continue
            f.seek(info.header_offset + 26)
            name_length, extra_length = struct.unpack('<HH', f.read(4))
            f.seek(info.header_offset + 30 + name_length + extra_length)
            raw = f.read(info.compress_size)
            if info.compress_type == 0:
                data = raw
            elif info.compress_type == 8:
                data = zlib.decompress(raw, -15)
            elif info.compress_type == 21:
                data = lzx.inflate(raw, info.file_size)
            else:
                data = None
            if data is not None and zlib.crc32(data) & 0xFFFFFFFF == info.CRC:
                yield info.filename, data


def classify(source_names, table):
    names = ' '.join(t[0] for t in table)
    for source in source_names:
        if source.startswith('track:'):
            material = source.split('/')[-1].upper()
            for prefix, cls in TRACK_CLASSES:
                if material.startswith(prefix):
                    return cls
            return 'track object'
    if any(s.startswith('car:') for s in source_names):
        for keyword, cls in CAR_CLASSES:
            if keyword in names:
                return cls
        return 'car other'
    if any(s.startswith('effect:') for s in source_names):
        return 'particles'
    return 'engine' if source_names else '?'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--dump', required=True, help='dump_shaders directory of a run')
    parser.add_argument('--inventory', action='append', default=[], help='REX_GPU_INVENTORY file')
    parser.add_argument('--json', help='write the atlas here (outside the repository)')
    parser.add_argument('--top', type=int, default=12, help='shaders listed per class')
    options = parser.parse_args()
    root = Path(options.build_root)
    media = Path(json.loads((root / 'local.json').read_text())['disc1']) / 'Media'
    lzx = Lzx(root)

    # md5 of the big-endian microcode -> sources and constant table. The title's Direct3D
    # patches the vertex fetches of a vertex shader for the vertex declaration it is bound with,
    # so vertex shaders are also indexed by microcode length for a closest match.
    known = {}
    vertex_by_length = collections.defaultdict(list)
    def add(data, source):
        for _, c in containers(data):
            stage = 'vs' if struct.unpack_from('>I', c)[0] & 1 else 'ps'
            code = microcode(c)
            key = (stage, hashlib.md5(code).hexdigest())
            entry = known.setdefault(key, {'sources': set(), 'constants': None})
            entry['sources'].add(source)
            if entry['constants'] is None:
                entry['constants'] = constant_table(c)
                if stage == 'vs':
                    vertex_by_length[len(code)].append((code, key))
    def closest_vertex(code):
        best, best_key = 0.0, None
        words = struct.unpack('>%dI' % (len(code) // 4), code)
        for candidate, key in vertex_by_length.get(len(code), ()):
            other = struct.unpack('>%dI' % (len(candidate) // 4), candidate)
            same = sum(a == b for a, b in zip(words, other)) / max(len(words), 1)
            if same > best:
                best, best_key = same, key
        return (best_key, best) if best >= 0.75 else (None, best)
    shaders = media / 'Shaders'
    for fxobj in sorted(shaders.rglob('*.fxobj')):
        kind = 'car' if 'Cars' in fxobj.parts else 'effect' if 'effects' in fxobj.parts else 'shader'
        add(fxobj.read_bytes(), f'{kind}:{fxobj.stem}')
    tracks = 0
    for archive in sorted((media / 'tracks').glob('*/bin.zip')):
        for name, data in zip_entries(archive, '.fxobj', lzx):
            add(data, f'track:{archive.parent.name}/{Path(name).stem}')
            tracks += 1
    add((root / 'loaded-image.bin').read_bytes(), 'default.xex')

    draws = collections.defaultdict(lambda: {'draws': 0, 'primitives': set(), 'partners': collections.Counter(),
                                             'render_targets': set(), 'blended': False})
    for path in options.inventory:
        for pair in json.loads(Path(path).read_text())['pairs']:
            for stage, partner in (('ps', 'vs'), ('vs', 'ps')):
                d = draws[(stage, pair[stage].upper())]
                d['draws'] += pair['draws']
                d['primitives'].update(PRIMITIVES.get(p, str(p)) for p in pair['primitives'])
                d['partners'][pair[partner].upper()] += pair['draws']
                d['render_targets'].update(pair['render_targets'])

    atlas = []
    dump = Path(options.dump)
    for stage, suffix in (('ps', 'frag'), ('vs', 'vert')):
        for ucode in sorted(dump.glob(f'shader_*.ucode.bin.{suffix}')):
            shader = ucode.name[7:23].upper()
            words = struct.unpack('<%dI' % (ucode.stat().st_size // 4), ucode.read_bytes())
            code = struct.pack('>%dI' % len(words), *words)
            entry = known.get((stage, hashlib.md5(code).hexdigest()), {})
            similarity = 1.0 if entry else 0.0
            if not entry and stage == 'vs':
                key, similarity = closest_vertex(code)
                entry = known.get(key, {})
            table = entry.get('constants') or []
            sources = sorted(entry.get('sources', ()))
            d = draws.get((stage, shader))
            atlas.append({
                'stage': stage, 'hash': shader, 'class': classify(sources, table),
                'sources': sources, 'similarity': round(similarity, 3) if sources else 0.0,
                'constants': [f'{n}={r}{i}' + (f'[{c}]' if c > 1 else '') for n, r, i, c in table],
                'draws': d['draws'] if d else 0,
                'primitives': sorted(d['primitives']) if d else [],
                'partners': [h for h, _ in d['partners'].most_common(4)] if d else [],
                'ucode_dwords': len(words),
            })
    # A vertex shader's material is that of the pixel shader it draws with most.
    pixel_classes = {a['hash']: a['class'] for a in atlas if a['stage'] == 'ps'}
    for a in atlas:
        if a['stage'] == 'vs' and a['partners'] and a['partners'][0] in pixel_classes:
            a['class'] = pixel_classes[a['partners'][0]]
    if options.json:
        Path(options.json).write_text(json.dumps(atlas, indent=1))

    for stage in ('ps', 'vs'):
        entries = [a for a in atlas if a['stage'] == stage]
        matched = sum(1 for a in entries if a['sources'])
        print(f'== {stage}: {len(entries)} shaders, {matched} matched ({tracks} track shader files scanned)')
        classes = collections.defaultdict(list)
        for a in entries:
            classes[a['class']].append(a)
        for cls, members in sorted(classes.items(), key=lambda kv: -sum(a['draws'] for a in kv[1])):
            print(f'-- {cls}: {len(members)} shaders, {sum(a["draws"] for a in members)} draws')
            for a in sorted(members, key=lambda a: -a['draws'])[:options.top]:
                names = sorted({s.split('/')[-1].split(':')[-1] for s in a['sources']})
                print(f'   {a["hash"]} {a["draws"]:8d} {"/".join(a["primitives"]):9} '
                      f'{",".join(names)[:48]:48} {" ".join(a["constants"])[:120]}')


if __name__ == '__main__':
    main()
