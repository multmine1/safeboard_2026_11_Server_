#!/bin/sh
set -eu
ram=$1
previous_root=$2
system_disk=$3
payload=$4
read -r root_filesystem < "$payload/previous_root_filesystem"
umask 077

if [ ! -f "$ram/complete" ]; then
    mkdir -p "$ram"
    if ! grep -q " $ram tmpfs " /proc/mounts; then
        mount -t tmpfs -o mode=0700 tmpfs "$ram"
    fi
    mkdir -p "$ram/old-root" "$ram/keys"
    mount -t "$root_filesystem" -o ro,noload "$previous_root" "$ram/old-root"
    trap 'umount "$ram/old-root"' EXIT
    while read -r mode path; do
        test -f "$ram/old-root$path"
        test ! -L "$ram/old-root$path"
        mkdir -p "$ram/keys$(dirname "$path")"
        cp "$ram/old-root$path" "$ram/keys$path"
        chmod "$mode" "$ram/keys$path"
    done < "$payload/hostkeys"
    umount "$ram/old-root"
    trap - EXIT
    touch "$ram/complete"
fi

debconf-set partman-auto/disk "$(readlink -f "$system_disk")"
