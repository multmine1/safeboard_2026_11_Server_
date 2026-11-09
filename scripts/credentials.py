import os

from _common import ANSIBLE, run

if __name__ == "__main__":
    command = ["ansible-playbook", "playbooks/credentials.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, cwd=ANSIBLE)
