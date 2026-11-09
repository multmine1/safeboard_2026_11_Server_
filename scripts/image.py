import os
import sys

from _common import ANSIBLE, run

if __name__ == "__main__":
    command = ["ansible-playbook", "playbooks/image.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, *sys.argv[1:], cwd=ANSIBLE)
