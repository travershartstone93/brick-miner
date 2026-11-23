"""Stable bytes used by qualification evidence and bundle manifests."""
import hashlib
import json

def sha(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n").encode()


def write(path, value):
    path.write_bytes(encode(value))
