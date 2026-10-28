#!/usr/bin/env bash
# Runs the game test beds one after the other, in this order: native Linux Steam games (a one minute launch check each), then Heroic and Lutris games (2 minutes),
# then the Steam games that run through Proton (2 minutes each). Start it when the machine is free: every game opens on the real screen and is closed again.
# Reports: perflog/testbed/<date>/. Usage: scripts/game-testbed-all.sh [native|launchers|proton|all]   (default all)
set -u
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
WHAT=${1:-all}
done_one() { tuxthrottlectl gamemode off >/dev/null 2>&1; sleep 20; }   # never leave Game Mode on between games
steam_group() {  # steam_group native|proton
  python3 scripts/game-testbed.py --list-steam | while IFS='|' read -r kind appid name; do
    [[ $kind == "$1" ]] || continue
    # online-only or server entries cannot be judged by a launch check
    case "$name" in *"Dedicated Server"*|*Beta*) continue ;; esac
    # a native game's window class is its own (hl2_linux, ...): any new window that is not Steam's counts; a Proton game's is steam_app_<id>
    if [[ $kind == native ]]; then python3 scripts/game-testbed.py --kind native --name "$name" --steam "$appid" > /dev/null 2>&1
    else python3 scripts/game-testbed.py --kind proton --name "$name" --steam "$appid" --window-match "steam_app_$appid" > /dev/null 2>&1; fi
    done_one
  done
}
[[ $WHAT == native || $WHAT == all ]] && steam_group native
if [[ $WHAT == launchers || $WHAT == all ]]; then
  python3 scripts/game-testbed.py --kind native --name "osu!lazer" --cmd "osu-lazer-launcher run" --window-match osu --minutes 2 > /dev/null 2>&1; done_one
  # Heroic and Lutris entries are listed in scripts/game-testbed-launchers.txt as: name|heroic:<runner>/<appname>  or  name|lutris:<slug>  (filled in as the games are identified)
  if [[ -f scripts/game-testbed-launchers.txt ]]; then
    while IFS='|' read -r name spec; do
      [[ -z $name || $name == \#* ]] && continue
      case "$spec" in
        heroic:*) python3 scripts/game-testbed.py --name "$name" --heroic "${spec#heroic:}" > /dev/null 2>&1 ;;
        lutris:*) python3 scripts/game-testbed.py --name "$name" --lutris "${spec#lutris:}" > /dev/null 2>&1 ;;
      esac
      done_one
    done < scripts/game-testbed-launchers.txt
  fi
fi
[[ $WHAT == proton || $WHAT == all ]] && steam_group proton
grep -h "^RESULT\|^# " perflog/testbed/"$(date +%F)"/*.txt
