"""Open the declared SSH SOCKS5 proxy for private service access."""

import argparse
import json
import os
import shlex

from _common import GENERATED

if __name__ == "__main__":
    access = json.loads((GENERATED / "ansible/local-access.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=access["ssh_host"], help="SSH alias from ~/.ssh/config")
    parser.add_argument("--print-command", action="store_true", help="print the native OpenSSH command")
    args = parser.parse_args()
    command = ["ssh", "-N", "-o", "ClearAllForwardings=no", "-o", "ExitOnForwardFailure=yes",
               "-D", access["socks_address"], args.host]
    print("SOCKS5: " + access["socks_address"], flush=True)
    for name, entry in access["forwards"].items():
        print(f"{name}: {entry['remote_url']}", flush=True)
    if args.print_command:
        print(shlex.join(command))
    else:
        os.execvp(command[0], command)
