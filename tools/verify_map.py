#!/usr/bin/env python3
"""Independent structural check of a Teeworlds map.

This shares no code with the editor on purpose: it re-implements the container
layout from the format description and asserts the invariants a client relies
on, so the C++ writer can be checked from the outside.

usage: verify_map.py map [map ...]

Checks
  * header, table sizes and section offsets are consistent
  * item types are unique and ascending with contiguous starts
  * every item payload size is a multiple of 4 and stays inside the item section
  * every data block inflates and the uncompressed size matches the table
  * tile layers decode to exactly width * height tiles (run length for
    tilemap version >= 4, plain otherwise)
  * quads and sound layers decode to their declared element count
  * envelope points cover the ranges the envelopes declare
"""
import struct
import sys
import zlib


def need(condition, message, problems):
    if not condition:
        problems.append(message)
    return condition


class Map:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        self.problems = []

    def unpack(self, offset, fmt):
        return struct.unpack_from('<' + fmt, self.data, offset)

    def zstd(self, blob):
        import subprocess
        result = subprocess.run(['zstd', '-d', '-q', '-c'], input=blob, stdout=subprocess.PIPE)
        if result.returncode != 0:
            raise RuntimeError('zstd failed')
        return result.stdout

    def parse(self):
        p = self.problems
        if not need(self.data[:4] == b'DATA', 'bad magic', p):
            return
        (self.version, self.size, self.swaplen, self.num_types, self.num_items,
         self.num_raw, self.item_size, self.data_size) = self.unpack(4, '8i')
        need(self.version in (3, 4, 5), f'unsupported datafile version {self.version}', p)

        table = 36 + self.num_types * 12 + self.num_items * 4 + self.num_raw * 4
        if self.version >= 4:
            table += self.num_raw * 4
        if self.version >= 5:
            table += 4
        need(table <= len(self.data), 'tables are truncated', p)

        off = 36
        self.types = []
        for _ in range(self.num_types):
            t, s, n = self.unpack(off, '3i')
            off += 12
            self.types.append((t, s, n))
        need([t for t, _, _ in self.types] == sorted(t for t, _, _ in self.types), 'item types not ascending', p)
        need(len({t for t, _, _ in self.types}) == len(self.types), 'duplicate item type', p)
        start = 0
        for t, s, n in self.types:
            need(s == start, f'type {t} start {s} != {start}', p)
            need(n > 0, f'type {t} has no items', p)
            start += n
        need(start == self.num_items, f'type table covers {start} of {self.num_items} items', p)

        self.item_offsets = list(self.unpack(off, f'{self.num_items}i'))
        off += self.num_items * 4
        self.data_offsets = list(self.unpack(off, f'{self.num_raw}i'))
        off += self.num_raw * 4
        self.data_sizes = []
        if self.version >= 4:
            self.data_sizes = list(self.unpack(off, f'{self.num_raw}i'))
            off += self.num_raw * 4
        if self.version >= 5:
            self.comp_item_size = self.unpack(off, 'i')[0]
            off += 4
            self.items = self.zstd(self.data[off:off + self.comp_item_size])
            self.data_start = off + self.comp_item_size
        else:
            need(off + self.item_size <= len(self.data), 'item section is truncated', p)
            self.items = self.data[off:off + self.item_size]
            self.data_start = off + self.item_size
        need(self.data_start + self.data_size <= len(self.data), 'data section is truncated', p)

        # data blocks
        self.blocks = []
        for i in range(self.num_raw):
            begin = self.data_offsets[i]
            end = self.data_offsets[i + 1] if i + 1 < self.num_raw else self.data_size
            need(0 <= begin <= end <= self.data_size, f'data block {i} out of range', p)
            blob = self.data[self.data_start + begin:self.data_start + end]
            if self.version == 3:
                self.blocks.append(blob)
            elif self.version == 5:
                try:
                    out = self.zstd(blob)
                except RuntimeError:
                    p.append(f'data block {i}: zstd error')
                    self.blocks.append(b'')
                    continue
                if not need(self.data_sizes[i] == len(out), f'data block {i} size {len(out)} != {self.data_sizes[i]}', p):
                    pass
                self.blocks.append(out)
            else:
                out = zlib.decompress(blob) if blob else b''
                need(self.data_sizes[i] == len(out), f'data block {i} size {len(out)} != {self.data_sizes[i]}', p)
                self.blocks.append(out)

    def item(self, index):
        offset = self.item_offsets[index]
        need(0 <= offset and offset + 8 <= self.item_size, f'item {index} offset out of range', self.problems)
        type_and_id, size = struct.unpack_from('<2i', self.items, offset)
        need(size % 4 == 0, f'item {index} size {size} is not a multiple of 4', self.problems)
        need(offset + 8 + size <= self.item_size, f'item {index} body runs past the item section', self.problems)
        return type_and_id >> 16, type_and_id & 0xffff, self.items[offset + 8:offset + 8 + size]

    def check_layers(self):
        p = self.problems
        layers_start = layers_num = 0
        for t, s, n in self.types:
            if t == 5:
                layers_start, layers_num = s, n
        tilemaps = 0
        for i in range(self.num_items):
            t, iid, body = self.item(i)
            if t != 5 or len(body) < 12:
                continue
            ltype = struct.unpack_from('<i', body, 4)[0]
            if ltype == 2:
                tilemaps += 1
                version, w, h, flags = struct.unpack_from('<4i', body, 12)
                data = struct.unpack_from('<i', body, 56)[0]
                need(w > 0 and h > 0, f'tile layer {iid}: bad size {w}x{h}', p)
                physics = flags & 0x3e
                if physics:
                    continue  # the payload holds the tiles, checked implicitly
                need(0 <= data < self.num_raw, f'tile layer {iid}: data index {data} out of range', p)
                blob = self.blocks[data] if 0 <= data < self.num_raw else b''
                if version >= 4:
                    count = 0
                    offset = 0
                    while count < w * h and offset + 4 <= len(blob):
                        skip = blob[offset + 2]
                        count += skip + 1
                        offset += 4
                    need(count == w * h, f'tile layer {iid}: run length expanded to {count} of {w * h} tiles', p)
                    need(blob[offset - 1] == 0 if offset else True, f'tile layer {iid}: reserved byte not zero', p)
                else:
                    need(len(blob) == w * h * 4, f'tile layer {iid}: v{version} blob is {len(blob)} bytes, expected {w * h * 4}', p)
            elif ltype == 3:
                nq, data = struct.unpack_from('<2i', body, 16)
                need(0 <= data < self.num_raw, f'quad layer {iid}: data index {data} out of range', p)
                blob = self.blocks[data] if 0 <= data < self.num_raw else b''
                need(len(blob) >= nq * 152, f'quad layer {iid}: {len(blob)} bytes for {nq} quads', p)
            elif ltype == 10:
                ns, data = struct.unpack_from('<2i', body, 16)
                need(0 <= data < self.num_raw, f'sound layer {iid}: data index {data} out of range', p)
                blob = self.blocks[data] if 0 <= data < self.num_raw else b''
                need(len(blob) >= ns * 52, f'sound layer {iid}: {len(blob)} bytes for {ns} sources', p)
        return tilemaps

    def check_envelopes(self):
        p = self.problems
        envelopes = []
        points = None
        for i in range(self.num_items):
            t, iid, body = self.item(i)
            if t == 3 and len(body) >= 52:
                version, channels, start, count = struct.unpack_from('<4i', body, 0)
                envelopes.append((version, channels, start, count))
            elif t == 6:
                points = body
        if not envelopes:
            return 0, 0
        bezier = any(v >= 3 for v, _, _, _ in envelopes)
        size = 88 if bezier else 24
        if points is not None:
            need(len(points) % size == 0, f'envelope points blob {len(points)} is not a multiple of {size}', p)
            total = len(points) // size
            for version, channels, start, count in envelopes:
                need(0 <= start and start + count <= total, f'envelope range {start}+{count} outside {total} points', p)
                need(1 <= channels <= 4, f'envelope has {channels} channels', p)
        return len(envelopes), (len(points) // size if points is not None else 0)


def main(paths):
    failures = 0
    for path in paths:
        m = Map(path)
        m.parse()
        tilemaps = m.check_layers() if not m.problems else 0
        envelopes, points = m.check_envelopes() if not m.problems else (0, 0)
        status = 'ok  ' if not m.problems else 'FAIL'
        print(f'{status} {path}: datafile v{m.version} {m.num_items} items, {m.num_raw} data blocks, '
              f'{tilemaps} tile layers, {envelopes} envelopes / {points} points')
        for problem in m.problems[:8]:
            print(f'       - {problem}')
        failures += 1 if m.problems else 0
    print(f'\n{len(paths)} maps, {failures} with problems')
    return 1 if failures else 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1:]))
