#!/usr/bin/env python3
"""Audit the Git index for private game inputs, secrets and dependency drift."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from prepare import ROOT

files = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
files = [f for f in files if f]
if not files:
    raise SystemExit("No staged/tracked files; stage source before auditing")
errors = []
batch = subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def indexed(name):
    batch.stdin.write((":" + name + "\n").encode())
    batch.stdin.flush()
    header = batch.stdout.readline().decode().strip().split()
    if len(header) != 3 or header[1] != "blob":
        raise RuntimeError("Not an indexed blob: " + name)
    data = batch.stdout.read(int(header[2]))
    assert batch.stdout.read(1) == b"\n"
    return data
private_extensions = {".xbe", ".iso", ".xiso", ".apk", ".aab", ".keystore", ".jks", ".p12", ".pfx"}
private_dirs = {"assets", "generated", "out", "build", "build-native", "logs", "hdd", "CrashDumps", "sdk"}
secret = re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{60,}|AKIA[A-Z0-9]{16}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)")
total = 0
for name in files:
    path = Path(name)
    if path.suffix.lower() in private_extensions:
        errors.append("private file: " + name)
    if any(p in {".local", ".git", ".aws", ".codex", ".agents"} for p in path.parts):
        errors.append("private metadata: " + name)
    if path.parts[0] in {"dist", "out", "build", "assets", "generated", "hdd", "logs", "CrashDumps", "cap", "captures"}:
        errors.append("build product: " + name)
    if path.parts[0] == "ports" and len(path.parts) > 2 and path.parts[2] in private_dirs:
        errors.append("private port input/output: " + name)
    # Inspect the actual indexed blob, not a possibly different working file.
    data = indexed(name)
    total += len(data)
    if len(data) >= 100 * 1024 * 1024:
        errors.append("GitHub file size limit: " + name)
    if data[:4] == b"XBEH":
        errors.append("retail executable magic: " + name)
    if secret.search(data):
        errors.append("credential pattern: " + name)

manifest = ROOT / "third_party/SOURCES.sha256.json"
expected = json.loads(manifest.read_text())
tracked = set(files)
for name, digest in expected.items():
    if name not in tracked:
        errors.append("dependency source missing from index: " + name)
        continue
    data = indexed(name)
    if hashlib.sha256(data).hexdigest() != digest:
        errors.append("dependency source changed: " + name)
batch.stdin.close()
batch.wait()
print(f"Audited {len(files)} indexed files ({total:,} bytes); {len(expected)} pinned dependency files")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print("PASS: no game executables/images, generated port code, private build data, signing keys or credential patterns")
