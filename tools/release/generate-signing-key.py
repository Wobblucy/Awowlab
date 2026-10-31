#!/usr/bin/env python3
"""
Create the ECDSA P-256 key pair used to sign release manifests.

Run this once. It writes a password-protected private key PEM, prints the
matching public key as 128 hex characters, and offers to write it into
signing_public_key in awowlab.config.json, where the build picks it up.

The private key must stay OUTSIDE both repositories. Back it up somewhere
safe (password manager attachment, encrypted drive). If the key is lost,
auto-update keeps working for already-published releases, but new releases
can't be signed until users manually install a build with a new public key.

Usage:
    python tools/release/generate-signing-key.py [--out C:\\path\\to\\awowlab-signing-key.pem]
                                                 [--write-config | --no-write-config]

Requires: pip install cryptography
"""

import argparse
import getpass
import sys
from pathlib import Path

try:
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives import serialization
except ImportError:
    print("The 'cryptography' package is required: pip install cryptography")
    sys.exit(1)

import awowlab_config

DEFAULT_KEY_PATH = Path.home() / "awowlab-signing" / "awowlab-signing-key.pem"


def main():
    parser = argparse.ArgumentParser(description="Generate the AwowLab release signing key pair.")
    parser.add_argument("--out", type=Path, default=DEFAULT_KEY_PATH,
                        help=f"Where to write the private key PEM (default: {DEFAULT_KEY_PATH})")
    parser.add_argument("--config", default=awowlab_config.CONFIG_PATH,
                        help=f"Config file to put the public key in (default: {awowlab_config.CONFIG_PATH})")
    parser.add_argument("--write-config", dest="write_config", action="store_true", default=None,
                        help="Write the public key into the config file without asking")
    parser.add_argument("--no-write-config", dest="write_config", action="store_false",
                        help="Only print the public key")
    args = parser.parse_args()

    key_path: Path = args.out
    if key_path.exists():
        print(f"Refusing to overwrite existing key at {key_path}")
        print("Move it first if you really want a new key pair.")
        sys.exit(1)

    password = getpass.getpass("Password to protect the private key: ")
    confirm = getpass.getpass("Confirm password: ")
    if password != confirm:
        print("Passwords do not match.")
        sys.exit(1)
    if len(password) < 8:
        print("Use at least 8 characters.")
        sys.exit(1)

    private_key = ec.generate_private_key(ec.SECP256R1())

    key_path.parent.mkdir(parents=True, exist_ok=True)
    key_path.write_bytes(private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.BestAvailableEncryption(password.encode()),
    ))
    print(f"\nPrivate key written to: {key_path}")
    print("Back this file up now. Do not commit it to any repository.\n")

    numbers = private_key.public_key().public_numbers()
    x = numbers.x.to_bytes(32, "big")
    y = numbers.y.to_bytes(32, "big")

    public_hex = (x + y).hex()
    print("Public key (signing_public_key in awowlab.config.json):\n")
    print(public_hex)
    print()

    write = args.write_config
    if write is None:
        answer = input(f"Write it into {args.config}? [y/N] ")
        write = answer.strip().lower() in ("y", "yes")
    if write:
        awowlab_config.write_public_key(public_hex, args.config)
        print(f"Updated {args.config}. Re-run CMake and rebuild the updater.")
    else:
        print("Put it in signing_public_key in awowlab.config.json, then re-run CMake")
        print("and rebuild the updater.")


if __name__ == "__main__":
    main()
