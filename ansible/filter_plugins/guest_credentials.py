"""Generate guest access material in memory before Ansible Vault encryption."""

import secrets
import subprocess

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import (
    BestAvailableEncryption, Encoding, PrivateFormat, PublicFormat,
)


def new_guest_credentials(vm_id):
    key = Ed25519PrivateKey.generate()
    passphrase = secrets.token_urlsafe(32)
    password = secrets.token_urlsafe(32)
    password_hash = subprocess.check_output(
        ["openssl", "passwd", "-6", "-stdin"], input=password.encode(),
    ).decode().strip()
    return {"guest_admin": {
        "id": vm_id,
        "private_key": key.private_bytes(
            Encoding.PEM, PrivateFormat.OpenSSH,
            BestAvailableEncryption(passphrase.encode()),
        ).decode(),
        "public_key": key.public_key().public_bytes(
            Encoding.OpenSSH, PublicFormat.OpenSSH,
        ).decode(),
        "key_passphrase": passphrase,
        "sudo_password": password,
        "password_hash": password_hash,
    }}


class FilterModule:
    def filters(self):
        return {"new_guest_credentials": new_guest_credentials}
