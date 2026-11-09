import os

from _common import ANSIBLE, run

if __name__ == "__main__":
    os.environ["ANSIBLE_SSH_AGENT"] = os.environ["SSH_AUTH_SOCK"]
    command = ["ansible-playbook", "playbooks/runner.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, cwd=ANSIBLE)
