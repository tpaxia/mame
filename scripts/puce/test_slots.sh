#!/bin/sh
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
set -eu
rompath=${1:?ROM directory required}
results=${2:?Results directory required}
for config in default removed smaller console; do
    case "$config" in
        default) set -- ;;
        removed) set -- -bus:ram3 "" ;;
        smaller) set -- -bus:ram3 ram8 ;;
        console) set -- -bus:console "" ;;
    esac
    ./p6066 p6066 "$@" -bus:floppy "" -rompath "$rompath" -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run 10 -autoboot_delay 7 -autoboot_script scripts/puce/test_slots_mame.lua -snapshot_directory "$results" -snapname "slots-$config" > "$results/slots-$config.log" 2>&1
    rg '^PASS:' "$results/slots-$config.log"
    if rg -i 'LUA ERROR|Fatal error|Ignoring MAME exception' "$results/slots-$config.log"; then exit 1; fi
done
