#!/usr/bin/env python3
"""Builds a folder that looks like a console's disk, with made-up trophy data.

    make_fixture.py <root>

Two users. User 10000001 has two trophy sets (one with earned trophies, one untouched),
user 10000002 has one. The files follow the layouts described in docs/FORMATS.md.
Each trophy set carries a real 512 x 512 PNG as its game picture.
"""
import hashlib, json, os, struct, sys, zlib

EPOCH_US = 62135596800 * 1000000  # 0001-01-01 -> 1970-01-01, in microseconds
BLOCK, HASH = 0x420, 0x20


def record(rtype, body):
    return struct.pack('>IIII', rtype, len(body), 0, 0) + body


def t2pd(np, uid, grades, earned):
    """earned: {trophy id: unix seconds}"""
    n = len(grades)
    blocks = [
        (0x001, [bytes(0x30)]),
        (0x002, [struct.pack('>QQQQQI', 0, 1, 2, 3, 0, uid).ljust(0x60, b'\0')]),
        (0x100, [bytes(0x40)]),
        (0x300, [np.encode().ljust(0x480, b'\0')]),
        (0x400, [bytes(0x90)]),
        (0x500, [struct.pack('>II', i, 'PGSB'.index(g) + 1).ljust(0xc0, b'\0') for i, g in enumerate(grades)]),
        (0x600, [bytes(0x1f0)]),
        (0x700, [bytes(0xb0)]),
        (0x800, [(struct.pack('>IIQQ', i, 0x11 if i in earned else 0, 0, earned[i] * 1000000 + EPOCH_US if i in earned else 0)).ljust(0x50, b'\0')
                 for i in range(n)]),
    ]
    head = 0x30 + len(blocks) * 0x20
    table, data, off = b'', b'', head
    for rtype, bodies in blocks:
        size = len(bodies[0])
        table += struct.pack('>IIIIQQ', rtype, size, 1, len(bodies), off, 0)
        for b in bodies:
            data += record(rtype, b)
        off += len(bodies) * (size + 0x10)
    flat = (b'T2TD' + struct.pack('>II', 0x10000, len(blocks))).ljust(0x30, b'\0') + table + data
    part = BLOCK - HASH
    flat = flat.ljust(-(-len(flat) // part) * part, b'\0')
    out = (b'T2PD' + struct.pack('>IQ', 0x10000, BLOCK)).ljust(0x20, b'\0') + struct.pack('>Q', len(flat) // part)
    out = out.ljust(0x40, b'\0')
    for i in range(0, len(flat), part):
        chunk = flat[i:i + part]
        out += chunk + hashlib.sha256(chunk).digest()
    return out


def ucp(files):
    """files: [(name, bytes)]"""
    table_at, row = 0x60, 0x40
    off = table_at + len(files) * row
    off = -(-off // 16) * 16
    table, blob = b'', b''
    for name, data in files:
        table += name.encode().ljust(32, b'\0') + struct.pack('>QQ', off + len(blob), len(data)) + bytes(16)
        blob += data + bytes(-len(data) % 16)
    total = off + len(blob)
    head = struct.pack('>IIQII', 0xb228c60a, 1, total, len(files), row).ljust(table_at, b'\0')
    return (head + table).ljust(off, b'\0') + blob


def png(w, h, seed=0):
    """A real RGB PNG: a colour gradient with a darker frame."""
    rows = b''
    for y in range(h):
        row = bytearray([0])  # filter: none
        for x in range(w):
            edge = x < w // 16 or y < h // 16 or x >= w - w // 16 or y >= h - h // 16
            r, g, b = (x * 255 // max(1, w - 1) + seed) % 256, y * 255 // max(1, h - 1), (seed * 40 + 60) % 256
            row += bytes((r // 4, g // 4, b // 4) if edge else (r, g, b))
        rows += bytes(row)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def trophy_set(np, title, grades, rev, langs=('en-US', 'pt-BR')):
    tid = lambda i: '%04d' % i
    conf = {
        'schemaVersion': '1.00', 'trophyDefinitionRevision': rev, 'platform': ['PS5'],
        'trophyNpCommId': np, 'trophySetVersion': '1.0', 'defaultLanguage': langs[0], 'languages': list(langs),
        'trophies': [{'id': tid(i), 'hidden': i == 2, 'grade': g, 'hasReward': False} for i, g in enumerate(grades)],
    }
    files = [('icon0_%s.png' % langs[0], png(512, 512, len(grades))), ('tropconf.json', json.dumps(conf).encode())]
    for lang in langs:
        meta = {
            'schemaVersion': '0.90', 'trophyDefinitionRevision': rev, 'trophyNpCommId': np,
            'metadata': {
                'titleMetadata': {'name': title if lang == langs[0] else '%s (%s)' % (title, lang)},
                'trophyMetadata': [{'id': tid(i), 'name': 'Tōkon "%s" #%d' % (title, i), 'detail': 'Do thing %d.\nTwice.' % i}
                                   for i in range(len(grades))],
            },
        }
        files.append(('tropmeta_%s.json' % lang, json.dumps(meta, ensure_ascii=False).encode()))
    return ucp(files)


SETS = {
    'NPWR90001_00': ('Phosphor Quest', 'PGSSBBBB', 100, ('en-US', 'pt-BR')),
    'NPWR90002_00': ('Segment Racer', 'PGGSSSBBBBBB', 7, ('ja-JP', 'fr-FR')),  # no English names
    'NPWR90003_00': ('Glass Bezel', 'GSB', 3, ('en-GB',)),
}
USERS = {
    0x10000001: {'NPWR90001_00': {1: 1790000000, 4: 1790000500, 5: 1790001000}, 'NPWR90002_00': {}},
    0x10000002: {'NPWR90003_00': {0: 1791000000}},
}


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def main(root):
    for np, (title, grades, rev, langs) in SETS.items():
        write('%s/user/trophy2/nobackup/conf/%s/TROPHY.UCP' % (root, np), trophy_set(np, title, grades, rev, langs))
    for uid, sets in USERS.items():
        for np, earned in sets.items():
            write('%s/user/home/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT' % (root, uid, np), t2pd(np, uid, SETS[np][1], earned))
    os.makedirs('%s/data' % root, exist_ok=True)
    # loose pictures for the icon tests
    write('%s/pics/square.png' % root, png(512, 512))
    write('%s/pics/wide.png' % root, png(400, 200))
    write('%s/pics/small.png' % root, png(64, 48))


if __name__ == '__main__':
    main(sys.argv[1])
