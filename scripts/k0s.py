import os

from _common import ANSIBLE, run

if __name__ == "__main__":
    os.environ.setdefault("ANSIBLE_SSH_AGENT", os.environ.get("SSH_AUTH_SOCK", "auto"))
    command = ["ansible-playbook", "playbooks/k0s.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, cwd=ANSIBLE)
