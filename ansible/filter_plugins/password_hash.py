"""Bind a password to an idempotent native bcrypt verifier."""

import bcrypt


def bcrypt_password_hash(password, previous_hash, rounds):
    encoded = password.encode()
    if previous_hash and bcrypt.checkpw(encoded, previous_hash.encode()) and int(previous_hash.split("$")[2]) == rounds:
        return previous_hash
    return bcrypt.hashpw(encoded, bcrypt.gensalt(rounds=rounds)).decode()


class FilterModule:
    def filters(self):
        return {"bcrypt_password_hash": bcrypt_password_hash}
