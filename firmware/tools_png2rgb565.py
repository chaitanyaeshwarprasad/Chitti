"""Convert a 466x466 PNG into raw RGB565 (big-endian, as the CO5300 panel expects) for EMBED_FILES."""
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
assert im.size == (466, 466), im.size
out = bytearray()
for r, g, b in im.getdata():
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    out += bytes((v >> 8, v & 0xFF))
open(dst, "wb").write(out)
print(dst, len(out), "bytes")
