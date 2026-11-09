import json
import os
import subprocess

from _common import ANSIBLE, GENERATED, run

if __name__ == "__main__":
    os.environ.setdefault("ANSIBLE_SSH_AGENT", os.environ.get("SSH_AUTH_SOCK", "auto"))
    infrastructure = json.loads(subprocess.check_output(
        ["tofu", "output", "-json", "infrastructure"], cwd=GENERATED / "tofu",
    ))
    (GENERATED / "ansible/applied.json").write_text(
        json.dumps({"infrastructure": infrastructure}, indent=2) + "\n",
    )
    command = ["ansible-playbook", "playbooks/guests.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    run(*command, cwd=ANSIBLE)
