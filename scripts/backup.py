import sys
import json
import os

from _common import ANSIBLE, GENERATED, run
from _maintenance import maintenance_lock

if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in {"init", "run", "snapshots", "check", "retention-plan", "retention", "restore"}:
        raise SystemExit("Usage: python scripts/backup.py init|run|snapshots|check|retention-plan|retention|restore")
    command = ["ansible-playbook", "playbooks/backup.yml"]
    command += ["--extra-vars", f"backup_action={sys.argv[1]}"]
    if not os.environ.get("ANSIBLE_VAULT_PASSWORD_FILE"):
        command.append("--ask-vault-pass")
    backup = json.loads((GENERATED / "ansible/backup.json").read_text())["backup"]
    with maintenance_lock(backup):
        run(*command, cwd=ANSIBLE)
