#!/usr/bin/env bash
# Boot one firmware+arch combo headlessly and assert the banner shows on serial.
# usage: run-test.sh <name> <qemu> <code.fd> <vars-src.fd> <img>
#        [expected-serial] [extra qemu args...]
set -u

NAME=$1; QEMU=$2; CODE=$3; VARS_SRC=$4; IMG=$5; EXPECT="${6:-UEFI booted}"
shift 6 || true
BUILD=build
LOGDIR="$BUILD/logs"
LOG="$LOGDIR/$NAME.log"
VARS="$LOGDIR/$NAME-vars.fd"

mkdir -p "$LOGDIR"

PF=()
if [ "$CODE" != "none" ]; then
    VARS="$LOGDIR/$NAME-vars.fd"
    cp "$VARS_SRC" "$VARS"
    PF=(-drive if=pflash,format=raw,readonly=on,file="$CODE"
        -drive if=pflash,format=raw,file="$VARS")
fi

timeout 15 "$QEMU" -display none -no-reboot -serial "file:$LOG" \
    "${PF[@]}" \
    -drive format=raw,file="$IMG" \
    "$@" -net none >/dev/null 2>&1 || true

if grep -aqF "$EXPECT" "$LOG"; then
    echo "[$NAME] PASS"
else
    echo "[$NAME] FAIL - see $LOG"
    exit 1
fi
