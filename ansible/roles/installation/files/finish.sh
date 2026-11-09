#!/bin/sh
set -eu
ram=$1
payload=$2
user=$3
user_home=$4
umask 077
test -f "$ram/complete"

while read -r mode path file; do
    mkdir -p "/target$(dirname "$path")"
    cp "$payload/$file" "/target$path"
    chmod "$mode" "/target$path"
done < "$payload/files"

while read -r mode path; do
    mkdir -p "/target$(dirname "$path")"
    cp "$ram/keys$path" "/target$path"
    chmod "$mode" "/target$path"
done < "$payload/hostkeys"

chmod 700 "/target$user_home/.ssh"
in-target chown -R "$user:$user" "$user_home/.ssh"
while read -r action unit <&9; do
    in-target systemctl "$action" "$unit"
done 9< "$payload/services"
