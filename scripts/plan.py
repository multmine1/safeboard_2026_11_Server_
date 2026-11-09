from _common import GENERATED, run

if __name__ == "__main__":
    run("tofu", "init", cwd=GENERATED / "tofu")
    run("tofu", "plan", cwd=GENERATED / "tofu")
