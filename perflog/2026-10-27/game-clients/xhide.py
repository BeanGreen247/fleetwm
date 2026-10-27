import socket, struct, sys, time, os
# A tiny X11 program: shows a window (640x480 unless sized) for HOLD seconds, then unmaps it and stays alive (what Steam does when it hides to the tray).
#   xhide.py HOLD [TITLE]    connects to $DISPLAY (the test compositor's Xwayland, started on demand), else to the newest X socket in /tmp/.X11-unix
hold = float(sys.argv[1]); title = sys.argv[2] if len(sys.argv) > 2 else "xtest"
W, H, X, Y = (int(v) for v in sys.argv[3:7]) if len(sys.argv) >= 7 else (640, 480, 100, 100)   # xhide.py HOLD TITLE W H X Y: a borderless-looking window as big as the screen, like a Wine game
stay = hold < 0   # a negative HOLD keeps the window shown
import glob
disp = os.environ.get("DISPLAY", "")
if disp.startswith(":"):
    socks = ["/tmp/.X11-unix/X" + disp[1:].split(".")[0]]   # the test compositor's own Xwayland, announced by the test script
else:
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
req(1, struct.pack("<IIhhHHHHII", wid, root, X, Y, W, H, 0, 1, 0, 2) + struct.pack("<I", 0x2a6fdb), 0)
# ChangeProperty WM_NAME (atom 39) STRING(31)
t = title.encode()
req(18, struct.pack("<IIIBxxxI", wid, 39, 31, 8, len(t)) + t, 0)
req(8, struct.pack("<I", wid))  # MapWindow
time.sleep(abs(hold))
if stay: time.sleep(600)
req(10, struct.pack("<I", wid))  # UnmapWindow: the program hides its window but stays alive
time.sleep(60)
