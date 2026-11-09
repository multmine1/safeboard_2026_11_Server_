"""Configure the Mac's selective browser proxy from the generated access plan."""

import sys

from _common import ANSIBLE, run

if __name__ == "__main__":
    run("ansible-playbook", "playbooks/browser-proxy.yml", *sys.argv[1:], cwd=ANSIBLE)
