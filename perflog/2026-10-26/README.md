# Remote desktop picture encoders, 2026-10-26

Raw outputs of the measurements in `docs/REMOTE_DESKTOP_INTERNALS.md`. Test pictures are the repository's own
(`docs/images/debug-overlay.png`, `perflog/2026-10-20/vm-scenes/wp7.png`) plus a generated text page and a generated
gradient-with-noise "video" picture, all 1280x800; nobody's screen is captured.

| File | What |
|---|---|
| `format-study.py` / `format-study.txt` | JPEG variants, WebP, PNG, zlib at equal quality (SSIM 0.985), size and encode time |
| `codec-bench.cpp` / `codec-bench.txt` | ZRLE against the new Tight encoder (JPEG qualities 95, 77, 47), whole frame and a 640x400 window, ms and KB |
| `vnc-roundtrip.cpp` / `vnc-roundtrip.txt` | the real server (`serve_vnc`) on a local port decoded by libvncclient 0.9.15, the decoder Apache Guacamole uses: bytes and PSNR |
| `make-test-frames.py` | writes `<name>.raw` (XRGB) and `<name>.dim` for the two programs above |

Reproduce (from the repository root, scratch directory `$T`):
```
python3 perflog/2026-10-26/make-test-frames.py            # run inside $T; writes ui/wallpaper/text/video .raw and .dim
g++ -O3 -std=c++20 -Isrc/fleetremote -Isrc/common perflog/2026-10-26/codec-bench.cpp src/fleetremote/vnc_codec.cpp -o $T/bench -lz -ljpeg
(cd $T && for n in ui text wallpaper video; do ./bench $n; done)
python3 perflog/2026-10-26/format-study.py
```
The round trip needs libvncclient's header (`apt-get download libvncserver-dev`, unpack with `dpkg -x` into `$T`; the header wants
`sasl/sasl.h`, a three-line stub is enough) and links `src/fleetremote/{hub,vnc,vnc_codec,tls}.cpp`, `src/common/{remote_config,fleettoml,config_paths}.cpp`
with `-l:libvncclient.so.1 -lssl -lcrypto -lz -ljpeg -lpthread`. Arguments: `<name> <encodings> <quality> <jpeg level>`; `BPP16=1` asks for a 16 bit format.
