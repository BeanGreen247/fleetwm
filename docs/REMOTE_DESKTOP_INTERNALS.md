# Remote desktop: design and internals

How `fleetwm-remote` works, why it is built the way it is, what was measured, and what is still open. For how to use it see
[REMOTE_DESKTOP.md](REMOTE_DESKTOP.md). Source: `src/fleetremote/` (about 3,800 lines of C++20), settings page
`apps/settings/remote_tab.hpp`, configuration `src/common/remote_config.*`. Raw measurements: `perflog/2026-10-26/`.

## 1. What it is, and why it is custom code

A VNC (RFB 3.8) and an RDP server that run inside the user's own session as an ordinary Wayland client of the compositor. There is no
xrdp, x11vnc, wayvnc, libvncserver or FreeRDP behind it; both protocols are implemented from their specifications. The reasons:

- **The compositor is the source of truth.** Fleetwm already knows which part of the screen changed (damage). A bolt-on server would
  re-derive that by diffing the whole framebuffer. Here the damage rectangles from the compositor drive everything.
- **Nothing runs when nobody is connected.** The daemon exits when both servers are off. With no viewer it does not capture, does not
  wake up, and holds about 4 MB; with a viewer it asks the compositor for frames only while someone is looking.
- **No second desktop.** xrdp starts a separate X session; this serves the session the user is already in (the point of a "remote
  desktop" for a laptop that is logged in).
- **Footprint.** One process, two or three threads plus one per viewer, no dependency beyond zlib, libjpeg, OpenSSL (TLS and the VNC
  DES login) and xkbcommon.

The cost of that choice is that every codec must be written and verified here, which is why sections 5 and 9 spend their length on how it
was checked.

## 2. Architecture

```
 compositor (fleetwm)
   |  wlr-screencopy (damage, with cursor)        ^ wlr-virtual-pointer, virtual-keyboard
   v                                              |
 +---------------------- fleetwm-remote ------------------------------------------+
 | Wayland thread:  Capture ----publish()----> Hub  <----push_input()----+        |
 |   (poll loop, config watch, accept)          |  frame = XRGB, 64x64 tiles      |
 |                                              |  per-session dirty bitmap       |
 |                    take_dirty() <------------+                                 |
 |   one thread per viewer:  Session -> Throttle -> Shadow -> encoder -> socket   |
 |        VNC: Raw / ZRLE / Tight(+JPEG) / CopyRect / DesktopSize                  |
 |        RDP: bitmap updates (raw 32 bit, or 16 bit run-length), TLS              |
 +--------------------------------------------------------------------------------+
        ^ remote.toml (inotify)           status file / usage tally -> bar, Settings
```

- `Capture` (`capture.cpp`) binds `wlr-screencopy`, `wlr-virtual-pointer` and `virtual-keyboard`. It requests one frame at a time with
  `copy_with_damage`, so the compositor only answers when something changed and says which rectangles. The picture is copied once into
  the Hub; the next request is paced to the fastest connected viewer's frame rate (section 7) so a 24 fps viewer on a 60 Hz screen does not
  make the compositor copy and this process publish 60 frames a second.
- `Hub` (`hub.cpp`) holds the one shared frame (`0x00RRGGBB`) and, per session, a bitmap of 64x64 tiles that changed since that session
  last took them. `publish()` copies only the damaged rows and sets bits; `take_dirty()` hands a session its tiles (optionally of a
  box-filtered smaller picture, `scale.hpp`). A quiet desktop is a cleared bitmap and an idle thread.
- A session thread owns its viewer: socket (`conn.hpp`, plain or TLS), the pixel format the viewer asked for, a `Shadow`, a `Throttle`, and
  the encoder state. Nothing in a session is shared with another viewer, so one slow viewer cannot hold up another, at the price that each
  viewer is encoded separately (open item, section 10).
- Input goes the other way: pointer and key events are queued in the Hub (bounded at 4096) and played back on the Wayland thread through the
  virtual pointer and keyboard. X11 keysyms are mapped through an xkb keymap; pointer coordinates are scaled back when the viewer's picture was shrunk.
- `main.cpp` watches `~/.config/fleetwm/remote.toml` with inotify. Quality, frame rate, bandwidth limit, resolution and colour depth are
  read live by running sessions; port, password, encryption switch changes disconnect the viewers of that server.

## 3. Data model: tiles, the shadow, scroll detection

The frame is cut into 64x64 tiles (`kTile`). That size is a compromise: small enough that a cursor blink or a clock marks little, large
enough that per-tile overhead (palette, header) stays small, and the unit the encoders classify.

`Shadow` (`shadow.hpp`) is the session's copy of what the viewer currently shows. It does two things:

1. **Drops tiles that came out identical.** The compositor's damage is coarse (a blinking cursor marks the tiles around it); comparing
   against the shadow before encoding removes them.
2. **Finds scrolls.** If at least six tiles changed, it tests whether the new picture equals the old one moved by some lines; if so
   the viewer is told `CopyRect` (8 bytes) and only what still differs is sent. A page scroll costs a few hundred bytes plus the newly exposed lines.

## 4. VNC

RFB 3.8 with two logins: the classic VNC challenge (DES, first 8 password characters, a protocol limit) and VeNCrypt (TLS with a
self-signed certificate, then the VNC password). Five failed logins in a minute lock logins out for the rest of the minute
(`Hub::auth_failed`). Changing the password or switching the server off disconnects its viewers.

Encodings offered, in the order the code prefers them: **Tight with JPEG** (when the viewer sent a JPEG quality-level pseudo-encoding and the
pixel format has 16 or 32 bits), **ZRLE**, **Raw**; plus **CopyRect** and **DesktopSize**. A framebuffer update may mix encodings per
rectangle, which is how a future per-region choice could work.

A framebuffer update is built as: take the tiles, convert to the viewer's depth (`quantize_to_depth`), drop identical tiles, detect a scroll, sort the
tiles, join each row of neighbouring tiles into a run, encode each run, write once.

## 5. The picture codec (Tight with JPEG)

`vnc_codec.cpp`. ZRLE was the first codec: per tile solid, packed palette of up to 16 colours or raw pixels, then one deflate stream. It is
excellent on flat UI and poor on anything photographic: a gradient wallpaper or video tile has too many colours for a palette, so it goes out as
raw pixels through deflate. Measured on 1280x800 test pictures (`perflog/2026-10-26/codec-bench.txt`): 211 KB for a wallpaper, 2.2 MB for
a video-like picture.

### 5.1 Choosing a format: what was measured

The question was which image format gives the smallest picture at the same quality, fast. `perflog/2026-10-26/format-study.txt`
sweeps JPEG variants, WebP, PNG and plain zlib to an equal luma SSIM of 0.985 on four pictures (the project's UI screenshot, its wallpaper,
a rendered text page, a gradient-with-noise "video" picture). Findings:

| Content | Best at equal quality | Notes |
|---|---|---|
| Text, flat UI | **lossless**: WebP lossless 12.9 KB (text page), 73 KB (UI) against zlib 237 and 216 KB | JPEG is the worst choice here: 482-557 KB for the text page at the quality that reaches the target, and visibly smeared |
| Smooth wallpaper | WebP lossy 4.1-5.4 KB, **JPEG with optimized Huffman tables 8.2 KB**, plain JPEG 18.1 KB, JPEG 4:4:4 29.9 KB | optimized tables halve the size for a few ms |
| Video-like | **JPEG 4:2:0** 130-148 KB, lossless 1.3-2.5 MB | WebP lossy could not reach the target on this picture |
| Progressive JPEG | 2-4% smaller than optimized baseline, 3-5x the time | not worth it |
| 4:4:4 chroma | 30-60% larger than 4:2:0 | not worth it for photographic content |

So the ideal would be WebP (lossless for UI, lossy for photographs). **It cannot be used on the wire**: no VNC or RDP client decodes WebP or
JPEG XL. Apache Guacamole's VNC side is libvncclient, which decodes Raw, RRE, CoRRE, Hextile, Zlib, ZRLE, Ultra and Tight (with JPEG). H.264
is defined for RFB but is not in libvncclient. A "new compression algorithm" is therefore only possible inside
the standard encodings, and the best the standard set offers is Tight: lossless palette and zlib for text and flat colour, JPEG for photographs.
JPEG (libjpeg-turbo, 4:2:0, optimized Huffman tables, integer DCT) is the format used.

### 5.2 How a row of tiles is encoded

For a run of adjacent tiles, `TightEncoder::encode_run`:

1. **Count colours per tile** (open-addressing hash, early exit above 256).
2. **Group** neighbouring tiles of the same class into one rectangle (at most 2048 wide, a Tight rule):
   *flat* (one colour, the same across tiles) becomes a **Fill** rectangle of 4 bytes; *palette* tiles (at most 256 colours across the group)
   share one palette; *photographic* tiles (more than 256 colours) are their own group.
3. **Decide lossy or lossless.** Up to 48 colours is text or UI and stays lossless without asking. Richer groups are judged on two sample tiles, the
   richest and the median: each is encoded both ways (lossless on a scratch zlib stream, so the persistent streams stay in step with the viewer;
   JPEG with the header cost subtracted) and JPEG is used only if *both* samples say lossless would be more than `1.1 + 0.9 x quality/100` times larger.
   The median sample is what keeps a text window with one gradient tile from turning into a blur; the richest sample is what lets a photograph inside a
   window of text still go to JPEG. A photograph at a quality below 75 skips the trial and goes straight to JPEG.
4. **Emit.** Palette: Tight palette filter, 1 bit per pixel for two colours else 8, zlib streams 1 and 2. Photograph kept lossless: the Tight
   **gradient filter** (predict from left + up - upleft, per channel) on stream 3, which is what makes smooth photographic data compress. JPEG: quality from
   the server's Quality slider (`10 + 0.85 x slider`, 100 -> 95), optimized tables.

Colour-bit dropping (the old quality mechanism) is no longer applied for Tight: text and flat colour are exact at every quality and JPEG takes
the loss. ZRLE viewers still get it.

Cost control: the trial runs on two 64x64 tiles per group, not on the group; the first version tried both formats on whole groups and cost 3x
ZRLE on UI frames. Palette lookups skip runs of one colour. Buffers are members so a busy desktop does not allocate per update.

### 5.3 Results

Whole frame / 640x400 window update, ms and KB per update, `perflog/2026-10-26/codec-bench.txt` (JPEG quality 95 / 77 / 47):

| Content | ZRLE | Tight |
|---|---|---|
| UI screenshot | 147 KB, 16 ms / 60 KB, 6 ms | 166 / 161 / 148 KB, 27-33 ms / 66 / 62 / 55 KB, 9-12 ms |
| Text page | 158 KB, 20 ms / 54 KB, 7 ms | **82 KB, 16 ms / 27 KB, 6 ms** |
| Wallpaper | 211 KB, 23 ms / 72 KB, 7 ms | **80 / 32 / 26 KB, 21-26 ms / 30 / 12 / 10 KB, 7-9 ms** |
| Video-like | 2,271 KB, 105 ms / 697 KB, 32 ms | **145 / 36 / 23 KB, 7-19 ms / 45 / 12 / 8 KB, 2-8 ms** |

Tight is 2x smaller on text, 3-8x on gradients and 15-100x smaller and 5-15x faster on video-like content. **On a dense UI screenshot it is not
smaller than ZRLE (up to 12% larger at the highest quality, equal at low quality) and about 1.7x slower**: the debug-overlay screenshot is
mostly glass gradients and anti-aliased text where deflate over palette indices is already close to what the standard encodings allow. At 16 bits per
pixel Tight is 25% larger than ZRLE on that picture (131 against 105 KB) and smaller on the others. Choosing ZRLE or Tight per rectangle by the
smaller result is possible (an update may mix them) and is the next step if UI frames matter more than photographic ones.

### 5.4 Verification

- `tests/test_vnc_codec.cpp`: a reference Tight decoder (written separately from the encoder, zlib inflate and libjpeg) checks fill, two-colour and
  many-colour palettes, gradient filter, JPEG fidelity (PSNR above 28 dB at slider 40), persistent zlib streams across updates and the
  no-JPEG fallback being exactly lossless.
- `perflog/2026-10-26/vnc-roundtrip.cpp`: the real `serve_vnc` on a local port, synthetic frames, decoded by **libvncclient 0.9.15**,
  the decoder Apache Guacamole uses (`vnc-roundtrip.txt`). 32-bit: UI and text bit-exact at slider 90 and 100, text exact at every quality, photographs
  39-48 dB. 16-bit: Tight and ZRLE give identical PSNR on the flat pictures (the depth is the limit). This is what proves the wire format
  matches a third party's reading of the specification, not just ours.
- Not yet done: a live Guacamole session on the new encoder; other clients (TigerVNC, noVNC, Remmina).

## 6. RDP

RDP (`rdp.cpp`) implements the connection sequence over TLS (security "TLS" or standard RDP security without encryption), no NLA, slow-path bitmap
updates, user name = login name and a 16-character password. A picture is either **32-bit raw** or **16-bit with RDP's interleaved run-length coding**
(bottom-up strips, colour runs and colour images only), whichever is smaller per strip. It shares everything above the codec with VNC: Hub, Shadow, Throttle,
scaling, input, and so the frame-rate setting, the bandwidth floor and the capture pacing apply to RDP unchanged.

The picture quality tweak of this round (Tight/JPEG) does not exist for RDP, deliberately:

- RDP's lossy options are RemoteFX (wavelet, a large codec), the H.264 (AVC) graphics pipeline, or NSCodec. RDP 6.0 "planar" is lossless, a per-plane run-length coding that
  would compress 32-bit UI better than today's 16-bit RLE but not photographs.
- FreeRDP is not installed on the development machine, so a new RDP codec could not be decoded by a real client here. A wrong RDP bitmap stream
  breaks the connection outright; shipping one unverified is worse than not shipping it. Planar is the candidate; the test is `xfreerdp3 /cert:ignore /sec:tls`
  against the server with a screenshot comparison, as was done for the existing coding.

## 7. Throttle: frame rate, quality, bandwidth

`throttle.hpp`, per viewer.

- **Frame rate** (Settings: 5, 10, 15, 20, 24, 30, 45, 60; default 30): the shortest time between two updates is `1 / fps`. Before this round the
  interval came from the quality slider (quality 30-49 meant 200 ms, i.e. 5 updates a second): the lag reported on a 1 Gbit LAN with 0.7 ms
  round trip and the server under 10% of a core, was the setting, not the network. Quality now only decides how much colour detail goes.
  Frame rates are offered because they are the values the ecosystem uses: TigerVNC's server sends at most 60 updates a second by default (`Xvnc -FrameRate`,
  its manual: "Default is 60"), 24 is film rate and the stated target here, 30 is the usual remote-desktop rate, 5-15 are for slow links.
  (The Citrix and Microsoft defaults of 30 are from memory; the two pages could not be fetched.)
- **Capture follows the viewers.** `Hub::max_viewer_fps()` paces `Capture::request_frame`, so the compositor and the Hub stop working above what any viewer can use.
- **Bandwidth limit** paces sends so a viewer never averages more than the limit; above 95% use the colour depth drops one more bit (up to three), and returns below 40%.
  The link's own speed is learned from how long sends block (EMA) so a viewer on a slow link is not flooded even with no limit.
- **Bandwidth floor.** A limit under what the frame rate needs makes the picture lag however well it is coded. The floor is
  `fps x (0.25 x width x height) x bytes_per_changed_pixel`, with 0.25 of the screen being what a drag or scroll changes and bytes per changed pixel
  0.10 up to quality 90, rising to 0.15 at 100. The 0.10 comes from the measured window updates above (UI 0.21, text 0.10, wallpaper 0.04, video 0.03 at the lowest
  JPEG quality, mean about 0.10). Example: 1920x1080 at 24 fps, quality 40: about 1.2 MB/s (10 Mbit/s) while the picture moves fully. Settings shows the number,
  the slider skips steps below it, and the server raises a lower hand-edited limit to it. It is an estimate for sustained motion: an idle or mostly idle
  desktop uses far less (measured 150 KB/s while a viewer was active).

## 8. Passwords

By default a random password is made when a server is switched on and cleared when it is switched off. **Keep this password** (per server) leaves it
in place across off/on and restarts, so a saved Guacamole connection keeps working; **Set your own** takes a password (VNC 6-8 characters, RDP 12-64) and turns keeping on.
`remote.toml` is mode 0600 and holds the password as plain text, because the VNC login needs the secret itself (a hash cannot answer a DES challenge) and the RDP
login compares it. Anyone who can read your files can read it; anyone who has it has your session, so for anything beyond a trusted LAN use the TLS switch and an SSH tunnel or VPN.

## 9. Performance: what was measured, found and changed

- *Lag report (2026-10-26):* LAN, 0.7 ms RTT, no send-buffer pressure, server 0-8% of a core, 39 MB. The quality-derived 200 ms interval was the cause. Fixed by the frame-rate setting.
- *Capture rate above the useful rate:* `copy_with_damage` answers at the screen's rate; with a 24 fps viewer most frames were copied, published under the Hub mutex and dropped. Now paced.
- *JPEG decision cost:* trial-encoding whole groups made text frames 3x slower than ZRLE; two sample tiles brought them to 0.8x of ZRLE and a hash palette replaced a linear 256-entry scan (the scan alone was 12 ms on a photographic frame).
- *Allocation:* per-update vectors in the encoder became members.
- *Known and left:* `Hub::publish` copies pixels while holding the global mutex that session threads need to take tiles (a 1080p full-frame publish is a few ms, damage-sized in practice);
  each viewer is encoded separately; `take_dirty` copies tiles into per-session buffers; no profile of the whole daemon under a real load was possible (the kernel forbids `perf` for users on this machine, `perf_event_paranoid` is 3),
  so the encoder was profiled with gprof and the rest by timing and reading.

## 10. Not done, in order of value

1. A live Guacamole session on the Tight encoder (the libvncclient round trip passes; Guacamole adds its own stages).
2. A remote cursor: the pointer is drawn into the picture, so Guacamole's local-cursor mode has nothing to show and every mouse move is a frame. The RichCursor pseudo-encoding needs the cursor image
   and shape from the compositor.
3. Choosing ZRLE or Tight per rectangle by size (UI frames).
4. RDP planar (lossless) coding, verified with FreeRDP; then RemoteFX or H.264 for lossy.
5. One encode shared between viewers; NLA for RDP; clipboard, audio and drive redirection; more than the first monitor.
6. The Settings page grew by about eight rows; it has not been checked at 1024x768 (the minimum supported size).
