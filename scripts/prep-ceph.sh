#!/bin/bash

SELF_PATH="$(readlink --canonicalize-existing -- "$0")"
SELF_DIR="${SELF_PATH%/*}"
SELF_BASE="${SELF_PATH##*/}"
BLUESTORE_DIR=$1
BLKDEV=$2

usage ()
{
    echo "Usage: ${SELF_BASE} BLUESTORE_DIR BLOCK_DEV"
    exit 1
}

test -n "$1" || usage
test -n "$2" || usage

if [ ! -d "$BLUESTORE_DIR" ]; then
    mkdir "$BLUESTORE_DIR"
fi
if [ "$(ls -A $BLUESTORE_DIR)" ]; then
    rm -r "$BLUESTORE_DIR"/*
fi
ln -s "$BLKDEV" "$BLUESTORE_DIR/block"
