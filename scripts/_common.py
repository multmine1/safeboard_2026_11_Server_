from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "cpp"
BUILD = ROOT / "environment/.tools/build/cpp"
ANSIBLE = ROOT / "ansible"
GENERATED = ROOT / "configs/generated"


def run(*command, cwd=ROOT):
    code = subprocess.call(command, cwd=cwd)
    if code:
        raise SystemExit(code)
