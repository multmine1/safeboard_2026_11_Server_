import os
import json
import sys

from _common import ANSIBLE, GENERATED, run

if __name__ == "__main__":
    os.environ.setdefault("ANSIBLE_SSH_AGENT", os.environ.get("SSH_AUTH_SOCK", "auto"))
    command = ["ansible-playbook", "playbooks/applications.yml"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    if sys.argv[1:]:
        applications = json.loads((GENERATED / "k0s/applications/deployment.json").read_text())["applications"]
        selected = set()
        for name in sys.argv[1:]:
            if name not in applications or not applications[name]["enabled"]:
                raise SystemExit(f"Application is unknown or disabled: {name}")
            selected.update(applications[name]["order"])
        command += ["-e", json.dumps({"selected_applications": sorted(selected)})]
    run(*command, cwd=ANSIBLE)
