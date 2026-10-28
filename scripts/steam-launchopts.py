#!/usr/bin/env python3
"""Changes Steam launch options of chosen games for a test and puts them back. Steam keeps localconfig.vdf open and rewrites it, so Steam is shut down while the file is edited
and started again afterwards.

  steam-launchopts.py set 220 280 --drop-nvidia          # remove the NVIDIA PRIME offload variables from those games' launch options
  steam-launchopts.py set 220 --prepend SDL_VIDEODRIVER=x11
  steam-launchopts.py restore                            # put the backup (made on the first `set`) back
Backups: localconfig.vdf.fleetwm-test-bak next to the file.
"""
import argparse, glob, os, re, shutil, subprocess, sys, time

NV = re.compile(r"(?:__NV_PRIME_RENDER_OFFLOAD|__VK_LAYER_NV_optimus|__GLX_VENDOR_LIBRARY_NAME)=\S+\s*")

def cfg_path():
    p = glob.glob(os.path.expanduser("~/.local/share/Steam/userdata/*/config/localconfig.vdf"))
    if not p: sys.exit("no localconfig.vdf")
    return p[0]

def steam_stop():
    subprocess.run(["steam", "-shutdown"], capture_output=True, timeout=20)
    for _ in range(40):
        if subprocess.run(["pgrep", "-x", "steam"], capture_output=True).returncode != 0: return
        time.sleep(1)

def steam_start():
    subprocess.Popen(["steam", "-silent"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["set", "restore"])
    ap.add_argument("appids", nargs="*")
    ap.add_argument("--drop-nvidia", action="store_true")
    ap.add_argument("--prepend", default="")
    a = ap.parse_args()
    path = cfg_path()
    bak = path + ".fleetwm-test-bak"
    steam_stop()
    if a.action == "restore":
        if os.path.exists(bak): shutil.copy2(bak, path); print("restored", path)
    else:
        if not os.path.exists(bak): shutil.copy2(path, bak)
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        for appid in a.appids:
            start = text.find('\n\t\t\t\t\t"%s"\n\t\t\t\t\t{' % appid)   # the game's block in Apps (five tabs deep)
            if start == -1:
                print(appid, "has no block in localconfig.vdf"); continue
            end = text.find("\n\t\t\t\t\t}", start)
            blk = text[start:end]
            m = re.search(r'("LaunchOptions"\t+")([^"]*)(")', blk)
            if not m:
                print(appid, "has no launch options"); continue
            new = NV.sub("", m.group(2)) if a.drop_nvidia else m.group(2)
            if a.prepend: new = a.prepend + " " + new
            blk = blk[:m.start(2)] + new + blk[m.end(2):]
            text = text[:start] + blk + text[end:]
            print(appid, "->", new[:110])
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    steam_start()

if __name__ == "__main__":
    main()
