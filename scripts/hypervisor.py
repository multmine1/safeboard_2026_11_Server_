import os
import sys

from _common import ANSIBLE, run

if __name__ == "__main__":
    run("ansible-galaxy", "collection", "install", "-r", "requirements/collections.yml", cwd=ANSIBLE)
    command = ["ansible-playbook", "playbooks/hypervisor.yml", "playbooks/fabric.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, *sys.argv[1:], cwd=ANSIBLE)
