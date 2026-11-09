import sys
from scripts._common import ROOT, CPP, BUILD, GENERATED, run

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python run.py SOURCE_DIRECTORY")
    run("cmake", "--preset", "default", cwd=CPP)
    run("cmake", "--build", BUILD)
    run(BUILD / "config-generator", sys.argv[1], GENERATED)
    for stage in ("hypervisor", "image"):
        run(sys.executable, ROOT / "scripts" / f"{stage}.py")
    run(sys.executable, ROOT / "scripts/plan.py")
