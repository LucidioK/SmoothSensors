#!/usr/bin/env python3
"""
Sets up passwordless SSH access to the Uno Q board, so scripts/deploy.sh,
scripts/deploy.ps1, and sketch/tests/test_sketch_hardware.py stop prompting
for the board's password on every SSH call.

Reuses ~/.ssh/id_ed25519 if you already have one and it has no passphrase
(the common case); otherwise generates a dedicated
~/.ssh/id_ed25519_smoothsensors03 key and adds a Host entry for the board to
~/.ssh/config. Either way, the actual "ssh-copy-id" step -- appending the
public key to the board's ~/.ssh/authorized_keys -- is done explicitly here,
since Windows has no ssh-copy-id. (The old attempt at this, deploy.ps1's
Copy-SshKeyToBoardIfNeeded, only scp'd the .pub file onto the board as a
loose file -- it never touched authorized_keys, so the key it generated was
never actually authorized. That function has been removed; this script
replaces it.)

You will be prompted for the board's password ONE more time, to authorize
the new key -- this must run in a real interactive terminal (not redirected
output), since ssh reads the password prompt from the terminal directly.

Usage:
    python scripts/setup_ssh_key.py
    python scripts/setup_ssh_key.py arduino@10.0.0.195
    BOARD_HOST=arduino@10.0.0.195 python scripts/setup_ssh_key.py

Safe to re-run: every step here is additive (existing keys, config entries,
and authorized_keys lines are left alone and never duplicated).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

SSH_DIR = Path.home() / ".ssh"
DEFAULT_KEY = SSH_DIR / "id_ed25519"
DEDICATED_KEY = SSH_DIR / "id_ed25519_smoothsensors03"

# Auto-accept a *new* host key (first-ever connection to this board) so non-
# interactive checks below don't hang on a host-key prompt; a *changed* host
# key (the actual MITM-relevant case) is still rejected as normal.
SSH_COMMON_OPTS = ["-o", "StrictHostKeyChecking=accept-new"]


def resolve_board_host(explicit: str | None) -> str:
    """Mirrors the BOARD_HOST/BOARD_IP defaulting logic in scripts/deploy.sh and deploy.ps1."""
    if explicit:
        return explicit
    if os.environ.get("BOARD_HOST"):
        return os.environ["BOARD_HOST"]
    board_ip = os.environ.get("BOARD_IP", "10.0.0.195")
    return f"arduino@{board_ip}"


def require_on_path(binary: str) -> None:
    if shutil.which(binary) is None:
        sys.exit(f"==> ERROR: '{binary}' not found in PATH (install the OpenSSH client)")


def has_no_passphrase(key_path: Path) -> bool:
    """True if `key_path` is a usable private key with no passphrase."""
    result = subprocess.run(
        ["ssh-keygen", "-y", "-P", "", "-f", str(key_path)],
        capture_output=True, text=True,
    )
    return result.returncode == 0


def already_passwordless(board_host: str) -> bool:
    result = subprocess.run(
        ["ssh", *SSH_COMMON_OPTS, "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", board_host, "true"],
        capture_output=True, text=True,
    )
    return result.returncode == 0


def harden_permissions(path: Path) -> None:
    """Best-effort: OpenSSH refuses to use a private key (or config) that's too open."""
    try:
        if os.name == "posix":
            os.chmod(SSH_DIR, 0o700)
            os.chmod(path, 0o600)
        else:
            user = os.environ.get("USERNAME", "")
            subprocess.run(
                ["icacls", str(path), "/inheritance:r", "/grant:r", f"{user}:F"],
                capture_output=True, text=True,
            )
    except OSError as exc:
        print(f"==> WARNING: could not harden permissions on {path}: {exc}")


def generate_key(path: Path) -> None:
    print(f"==> Generating a new ed25519 key at {path}")
    SSH_DIR.mkdir(mode=0o700, exist_ok=True)
    subprocess.run(
        ["ssh-keygen", "-t", "ed25519", "-f", str(path), "-N", "", "-C", "smoothsensors03"],
        check=True,
    )
    harden_permissions(path)


def add_host_config_entry(board_host: str, key_path: Path) -> None:
    host = board_host.split("@", 1)[-1]
    config_path = SSH_DIR / "config"
    marker = f"# smoothsensors03: {host}"
    existing = config_path.read_text() if config_path.exists() else ""
    if marker in existing:
        return
    print(f"==> Adding a Host entry for {host} to {config_path}")
    block = f"\n{marker}\nHost {host}\n    IdentityFile {key_path}\n    IdentitiesOnly yes\n"
    with config_path.open("a") as f:
        f.write(block)
    harden_permissions(config_path)


def authorize_key_on_board(board_host: str, pub_key_text: str) -> None:
    pub_key_text = pub_key_text.strip()
    remote_cmd = (
        "mkdir -p ~/.ssh && chmod 700 ~/.ssh && "
        "touch ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && "
        f"grep -qxF '{pub_key_text}' ~/.ssh/authorized_keys || "
        f"echo '{pub_key_text}' >> ~/.ssh/authorized_keys"
    )
    print(f"==> Authorizing the key on {board_host} (you may be asked for the board's password one more time)")
    result = subprocess.run(
        ["ssh", *SSH_COMMON_OPTS, "-o", "ConnectTimeout=10", board_host, remote_cmd]
    )
    if result.returncode != 0:
        sys.exit(f"==> ERROR: failed to update authorized_keys on {board_host} (exit {result.returncode})")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set up passwordless SSH access to the Uno Q board.",
    )
    parser.add_argument(
        "board_host", nargs="?", default=None,
        help="board-user@host, e.g. arduino@10.0.0.195 (default: $BOARD_HOST, or "
             "arduino@$BOARD_IP, or arduino@10.0.0.195)",
    )
    args = parser.parse_args()

    require_on_path("ssh")
    require_on_path("ssh-keygen")

    board_host = resolve_board_host(args.board_host)
    print(f"==> Target board: {board_host}")

    if already_passwordless(board_host):
        print("==> Passwordless SSH already works for this board -- nothing to do.")
        return 0

    needs_config_entry = False
    if DEFAULT_KEY.exists():
        if has_no_passphrase(DEFAULT_KEY):
            print(f"==> Reusing existing key {DEFAULT_KEY}")
            key_path = DEFAULT_KEY
        else:
            print(f"==> {DEFAULT_KEY} exists but is passphrase-protected; using a dedicated key instead")
            key_path = DEDICATED_KEY
            needs_config_entry = True
            if not key_path.exists():
                generate_key(key_path)
    else:
        key_path = DEFAULT_KEY
        generate_key(key_path)

    if needs_config_entry:
        add_host_config_entry(board_host, key_path)

    pub_key_path = Path(str(key_path) + ".pub")
    authorize_key_on_board(board_host, pub_key_path.read_text())

    if already_passwordless(board_host):
        print(f"==> Success: `ssh {board_host}` (and deploy.sh/deploy.ps1/the sketch tests) "
              "will no longer prompt for a password.")
        return 0

    print("==> WARNING: the key was authorized, but a follow-up passwordless check still failed. "
          "See any errors above, or try `ssh -v " + board_host + "` to diagnose.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
