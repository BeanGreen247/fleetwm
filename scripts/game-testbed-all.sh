#!/usr/bin/env bash
# Runs the Steam test beds one after the other, 6 minutes each (scripts/game-testbed.py), then prints the verdict lines. Start it when the machine is free: every game opens on
# the real screen and is closed again at the end. Reports: perflog/testbed/<date>/. Usage: scripts/game-testbed-all.sh [minutes]
set -u
M=${1:-6}
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
while IFS='|' read -r name appid; do
  python3 scripts/game-testbed.py --name "$name" --steam "$appid" --window-match "steam_app_$appid" --minutes "$M" > /dev/null 2>&1
  tuxthrottlectl gamemode off >/dev/null 2>&1   # never leave Game Mode on between games
  sleep 20
done <<'LIST'
GTA III DE|1546970
LEGO Star Wars Skywalker Saga|920210
South Park Stick of Truth|213670
GTA V Enhanced|3240220
Need for Speed Heat|1222680
Serious Sam 4|257420
Mafia|40990
LEGO Batman The Videogame|21000
LIST
grep -h "^RESULT\|^# " perflog/testbed/"$(date +%F)"/*.txt
