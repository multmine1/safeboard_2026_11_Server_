from _common import ANSIBLE, run

if __name__ == "__main__":
    run("ansible-playbook", "tests/monitoring.yml", cwd=ANSIBLE)
