import json
import os

from _common import ANSIBLE, GENERATED, run
from _maintenance import maintenance_lock

if __name__ == "__main__":
    command = ["ansible-playbook", "playbooks/reset.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    reset = json.loads((GENERATED / "ansible/reset.json").read_text())["reset"]
    with maintenance_lock(reset):
        run(*command, cwd=ANSIBLE)
