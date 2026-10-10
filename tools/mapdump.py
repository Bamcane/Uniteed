#!/usr/bin/env python3
"""Read-only dumper for Teeworlds/DDRaceNet datafile v3/v4 (and v5 item table) maps."""
import struct, sys, zlib, io, subprocess

def zstd_decompress(blob):
    p = subprocess.run(['zstd', '-d', '-q', '-c'], input=blob, stdout=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError('zstd failed')
    return p.stdout

ITEM_TYPES = {0:'VERSION',1:'INFO',2:'IMAGE',3:'ENVELOPE',4:'GROUP',5:'LAYER',6:'ENVPOINTS',7:'SOUND'}
LAYER_TYPES = {0:'INVALID',1:'GAME',2:'TILES',3:'QUADS',10:'SOUNDS'}

def rd(b, off, fmt):
    return struct.unpack_from('<'+fmt, b, off)

class DataFile:
    def __init__(self, data):
        self.b = data
        assert data[:4] == b'DATA', data[:4]
        (self.version, self.size, self.swaplen, self.num_item_types, self.num_items,
         self.num_raw_data, self.item_size, self.data_size) = rd(data, 4, '8i')
        off = 36
        self.types = []
        for i in range(self.num_item_types):
            t, s, n = rd(data, off, '3i'); off += 12
            self.types.append((t, s, n))
        self.item_offsets = list(rd(data, off, '%di' % self.num_items)); off += 4*self.num_items
        self.data_offsets = list(rd(data, off, '%di' % self.num_raw_data)); off += 4*self.num_raw_data
        self.data_sizes = []
        if self.version >= 4:
            self.data_sizes = list(rd(data, off, '%di' % self.num_raw_data)); off += 4*self.num_raw_data
        self.comp_item_size = None
        if self.version >= 5:
            self.comp_item_size = rd(data, off, 'i')[0]; off += 4
        self.off_after_tables = off
        if self.version >= 5:
            prolog = data[off:off+self.comp_item_size]
            self.items_blob = zstd_decompress(prolog)
        else:
            self.items_blob = data[off:off+self.item_size]
            off += self.item_size
        self.data_start = off if self.version < 5 else off + (self.comp_item_size or 0)

    def item(self, idx):
        o = self.item_offsets[idx]
        t_id, size = rd(self.items_blob, o, '2i')
        return t_id >> 16, t_id & 0xffff, self.items_blob[o+8:o+8+size], size

    def raw(self, idx):
        start = self.data_offsets[idx]
        end = self.data_offsets[idx+1] if idx+1 < self.num_raw_data else self.data_size
        blob = self.b[self.data_start+start:self.data_start+end]
        if self.version >= 5:
            return zstd_decompress(blob)
        return zlib.decompress(blob)

def main(path):
    d = DataFile(open(path,'rb').read())
    print(f"== {path}")
    print(f"  datafile v{d.version} items={d.num_items} raw={d.num_raw_data} item_blob={d.item_size} data={d.data_size} types={d.num_item_types}")
    if False:
        print("  (items zstd-compressed, dump limited)")
        return
    for t, s, n in d.types:
        print(f"  type {t} ({ITEM_TYPES.get(t,'?')}) start={s} num={n}")
        for i in range(s, s+n):
            ty, iid, body, size = d.item(i)
            extra = ''
            if ty == 0:
                extra = 'version=%d' % rd(body,0,'i')[0]
            elif ty == 1:
                extra = 'ver=%d author=%d mapver=%d credits=%d license=%d' % rd(body,0,'5i')
            elif ty == 2:
                v,w,h,ext,imgname,imgdata = rd(body,0,'6i')
                extra = f'ver={v} {w}x{h} external={ext} imgdata={imgdata} size={size}'
            elif ty == 4:
                vals = rd(body,0,'%di' % (size//4))
                extra = ' '.join('%d' % v for v in vals)
            elif ty == 5:
                lv, ltype, lflags = rd(body,0,'3i')
                v = rd(body,12,'i')[0]
                if ltype == 2 and size >= 60:
                    w,h,flags = rd(body,16,'3i')
                    # the colour is four ints, not four bytes: the item is 92
                    # bytes with image at 52 and data at 56, and verify_map.py
                    # decodes the tile data through offset 56
                    colour = rd(body,28,'4i')
                    img, data_idx = rd(body,52,'2i')
                    s2 = ''
                    if size >= 80:
                        s2 = ' tele=%d speedup=%d front=%d switch=%d tune=%d' % rd(body,60,'5i')
                    extra = (f'tilemap ver={v} {w}x{h} flags={flags} layerflags={lflags} '
                             f'colour={colour[0]},{colour[1]},{colour[2]},{colour[3]} image={img} data={data_idx} size={size}{s2}')
                elif ltype == 3:
                    nq, di, img = rd(body,12,'3i')
                    extra = f'quads ver={v} numquads={nq} data={di} image={img} size={size}'
                elif ltype == 10:
                    ns, di, snd = rd(body,12,'3i')
                    extra = f'sounds ver={v} numsrc={ns} data={di} sound={snd} size={size}'
                else:
                    extra = f'layer ver={v} type={ltype} flags={lflags} size={size}'
            elif ty == 3:
                v, ch, sp, np = rd(body,0,'4i')
                extra = f'env ver={v} channels={ch} start={sp} npoints={np} size={size}'
            elif ty == 7:
                extra = 'sound ver=%d ext=%d' % rd(body,0,'2i')
            print(f"    item {i:3d} id={iid:3d} size={size:4d} {extra}")

if __name__ == '__main__':
    for p in sys.argv[1:]:
        try: main(p)
        except Exception as e: print(f"!! {p}: {type(e).__name__}: {e}")
