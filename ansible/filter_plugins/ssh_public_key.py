from cryptography.hazmat.primitives.serialization import (
    Encoding, PublicFormat, PrivateFormat, NoEncryption, load_ssh_private_key,
)


def ssh_public_key(private_key, passphrase):
    key = load_ssh_private_key(
        private_key.encode(), passphrase.encode() if passphrase else None,
    )
    return key.public_key().public_bytes(Encoding.OpenSSH, PublicFormat.OpenSSH).decode()


class FilterModule:
    def filters(self):
        return {"ssh_public_key": ssh_public_key, "ssh_runtime_key": ssh_runtime_key,
                "ssh_key_matches": ssh_key_matches}


def ssh_runtime_key(private_key, passphrase):
    key = load_ssh_private_key(
        private_key.encode(), passphrase.encode() if passphrase else None,
    )
    return key.private_bytes(Encoding.PEM, PrivateFormat.OpenSSH, NoEncryption()).decode()


def ssh_key_matches(private_key, public_key):
    try:
        return ssh_public_key(private_key, '') == ' '.join(public_key.split()[:2])
    except (ValueError, TypeError):
        return False
