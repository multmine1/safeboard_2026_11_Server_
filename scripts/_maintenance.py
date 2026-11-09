from contextlib import contextmanager
import os
import shlex
import subprocess


@contextmanager
def maintenance_lock(settings):
    command = "umask 077; exec " + shlex.join([
        "flock", "-n", settings["operation_lock_file"], "tee", "/dev/null",
    ])
    transport = shlex.split(os.environ.get("ANSIBLE_SSH_ARGS", ""))
    with subprocess.Popen(
        ["ssh", *transport, "-T", settings["operation_lock_alias"], command],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
    ) as lock:
        try:
            lock.stdin.write(b"locked\n")
            if lock.stdout.readline() != b"locked\n":
                reason = lock.stderr.read().decode().strip()
                raise SystemExit(reason or "The hypervisor maintenance lock is unavailable.")
            yield
        except BrokenPipeError:
            raise SystemExit("The hypervisor maintenance lock is unavailable.") from None
        finally:
            lock.stdin.close()
            lock.wait()
