import socket, struct, sys, time, os
# A tiny X11 program: shows a 640x480 window for HOLD seconds, then unmaps it and stays alive (what Steam does when it hides to the tray).
#   xhide.py HOLD [TITLE]    connects to the newest X socket in /tmp/.X11-unix (the test compositor's Xwayland, started on demand)
hold = float(sys.argv[1]); title = sys.argv[2] if len(sys.argv) > 2 else "xtest"
import glob
for _ in range(50):
    socks = sorted(glob.glob("/tmp/.X11-unix/X*"), key=os.path.getmtime)
    if socks: break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX); s.connect(socks[-1])
def rd(n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise SystemExit("closed")
        b += c
    return b
s.send(struct.pack("<BxHHHHxx", 0x6c, 11, 0, 0, 0))
h = rd(8); body = rd(struct.unpack("<H", h[6:8])[0] * 4)
base = struct.unpack("<I", body[4:8])[0]
vlen = struct.unpack("<H", body[16:18])[0]; nfmt = body[21]
off = 32 + ((vlen + 3) & ~3) + nfmt * 8
root = struct.unpack("<I", body[off:off + 4])[0]
def req(op, data=b"", extra=0):
    data += b"\0" * ((4 - len(data) % 4) % 4)
    s.send(struct.pack("<BBH", op, extra, (len(data) + 4) // 4) + data)
wid = base | 1
# CreateWindow: depth 0, wid, parent, x, y, w, h, border, class InputOutput(1), visual CopyFromParent(0), mask BackPixel(2), value
req(1, struct.pack("<IIhhHHHHII", wid, root, 100, 100, 640, 480, 0, 1, 0, 2) + struct.pack("<I", 0x2a6fdb), 0)
# ChangeProperty WM_NAME (atom 39) STRING(31)
t = title.encode()
req(18, struct.pack("<IIIBxxxI", wid, 39, 31, 8, len(t)) + t, 0)
req(8, struct.pack("<I", wid))  # MapWindow
time.sleep(hold)
req(10, struct.pack("<I", wid))  # UnmapWindow: the program hides its window but stays alive
time.sleep(60)
