#!/usr/bin/env python3
"""A pretend wpa_supplicant for the PGO training run and for trying the Wi-Fi page without a radio.

    pgo-fake-wpa.py DIR [SECONDS]

Listens on DIR/wlan0 (a unix datagram socket, like the real control interface) and answers the commands Fleetwm's wpa
backend sends: STATUS, SCAN, SCAN_RESULTS, LIST_NETWORKS, ADD_NETWORK, SET_NETWORK, ENABLE_NETWORK, REMOVE_NETWORK,
DISCONNECT, SAVE_CONFIG. State changes are real enough for the page: adding and enabling a network makes it the
connected one, DISCONNECT clears it. Runs for SECONDS (default 600) or until killed. Needs no privileges.
"""
import os
import socket
import sys
import time

AIRS = [("aa:aa:aa:aa:aa:02", 5180, -52, "[WPA2-PSK-CCMP][ESS]", "Home"),
        ("bb:bb:bb:bb:bb:01", 2437, -66, "[ESS]", "Cafe"),
        ("cc:cc:cc:cc:cc:03", 2412, -78, "[WPA2-PSK-CCMP][ESS]", "Neighbour"),
        ("dd:dd:dd:dd:dd:04", 5745, -61, "[WPA2-PSK-CCMP][WPS][ESS]", "Office"),
        ("ee:ee:ee:ee:ee:05", 2462, -84, "[WPA-PSK-TKIP][ESS]", "Old router")]


def main():
    d = sys.argv[1]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 600
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, "wlan0")
    if os.path.exists(path):
        os.unlink(path)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.bind(path)
    s.settimeout(0.5)
    known = {0: "Home"}   # id -> ssid
    current = 0           # id of the connected network, or None
    next_id = 1
    end = time.time() + secs
    while time.time() < end:
        try:
            data, addr = s.recvfrom(4096)
        except socket.timeout:
            continue
        cmd = data.decode(errors="replace").strip()
        word = cmd.split(" ", 1)[0]
        if word == "STATUS":
            if current is None:
                reply = "wpa_state=DISCONNECTED\n"
            else:
                ssid = known[current]
                reply = "bssid=aa:aa:aa:aa:aa:02\nssid=%s\nid=%d\nwpa_state=COMPLETED\nip_address=192.168.0.9\n" % (ssid, current)
        elif word == "SCAN_RESULTS":
            reply = "bssid / frequency / signal level / flags / ssid\n" + "".join("%s\t%d\t%d\t%s\t%s\n" % a for a in AIRS)
        elif word == "LIST_NETWORKS":
            reply = "network id / ssid / bssid / flags\n" + "".join(
                "%d\t%s\tany\t%s\n" % (i, n, "[CURRENT]" if i == current else "") for i, n in known.items())
        elif word == "ADD_NETWORK":
            known[next_id] = ""
            reply = "%d\n" % next_id
            next_id += 1
        elif word == "SET_NETWORK":
            parts = cmd.split(" ", 3)
            if len(parts) == 4 and parts[2] == "ssid":
                known[int(parts[1])] = parts[3].strip('"')
            reply = "OK\n"
        elif word == "ENABLE_NETWORK":
            nid = int(cmd.split(" ")[1]) if " " in cmd else None
            if nid in known:
                current = nid
            reply = "OK\n"
        elif word == "REMOVE_NETWORK":
            nid = int(cmd.split(" ")[1]) if " " in cmd else None
            known.pop(nid, None)
            if current == nid:
                current = None
            reply = "OK\n"
        elif word == "DISCONNECT":
            current = None
            reply = "OK\n"
        else:  # SCAN, SAVE_CONFIG, SELECT_NETWORK, anything else
            reply = "OK\n"
        if addr:
            try:
                s.sendto(reply.encode(), addr)
            except OSError:
                pass
    os.unlink(path)


main()
