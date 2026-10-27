"""Offline CLI regression checks; installs only generated packages in temporary roots."""
import io
import os
import shutil
from pathlib import Path
import subprocess
import tarfile
import tempfile

AXON = Path(__file__).resolve().parents[1] / "axon"
with tempfile.TemporaryDirectory(prefix="axon-bootstrap-") as directory:
    base = Path(directory)
    target = base / "root"
    target.mkdir()
    sync = target / "var/lib/axon/sync"
    sync.mkdir(parents=True)
    conf = base / "axon.conf"
    conf.write_text("[fixture]\nServer = file:///nonexistent/fixture.txt\n")
    env = {**os.environ, "AXON_CONF": str(conf), "NO_COLOR": "1"}
    archive = base / "bootstrap-app.tar"
    with tarfile.open(archive, "w") as tar:
        payload = b"fixture payload\n"
        entry = tarfile.TarInfo("usr/share/bootstrap-fixture")
        entry.size = len(payload)
        tar.addfile(entry, io.BytesIO(payload))
    (sync / "fixture.txt").write_text(
        f"bootstrap-app|1.0|{archive.stat().st_size}|{archive.as_uri()}|fixture|ntsync-autoload\n"
    )

    def run(*args, success=True):
        print("Checking:", *args, flush=True)
        result = subprocess.run([str(AXON), *map(str, args)], env=env,
                                cwd=base, text=True, capture_output=True, timeout=15)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    # Invalid/incomplete options never become package targets.
    for args in [("get", "--root"), ("get", "--root", archive),
                 ("get", "--bogus"), ("get", "--needed"),
                 ("get", "--assume-installed"),
                 ("get", "--assume-installed", "foo>=1", "bootstrap-app")]:
        run(*args, success=False)

    # Even a successfully resolved target cannot install if a sibling fails.
    result = run("get", "--root", target, "--noconfirm", "--assume-installed",
                 "ntsync-autoload", "bootstrap-app", "missing-bootstrap-fixture", success=False)
    assert "aborted before downloading" in result.stderr
    assert not (target / "usr/share/bootstrap-fixture").exists()
    assert not (target / "var/cache/axon/pkg").exists()

    # Reuse a cached fixture: exercise extraction and DB writes without networking.
    cache = target / "var/cache/axon/pkg"
    cache.mkdir(parents=True)
    shutil.copy2(archive, cache / archive.name)

    # A dependency exception permits a real offline extraction and DB write.
    run("--root", target, "get", "--needed", "--noconfirm", "--keep-cache",
        "--assume-installed", "ntsync-autoload", "bootstrap-app")
    assert (target / "usr/share/bootstrap-fixture").read_bytes() == payload
    assert (target / "var/lib/pacman/local/bootstrap-app-1.0-1/desc").is_file()
    assert (target / "var/cache/axon/pkg/bootstrap-app.tar").is_file()
    assert not (target / "var/lib/pacman/local/ntsync-autoload-1.0-1").exists()

    # Explicit directories override root defaults in either argument order.
    other = base / "other-root"
    other.mkdir()
    explicit_db = base / "explicit-db"
    run("--db-path", explicit_db, "get", "--root", other,
        "--sync-dir", sync, "--cache-dir", cache, "--no-color", "--noconfirm",
        "--keep-cache", "--assume-installed", "ntsync-autoload",
        "--assume-installed", "unused-exception", "bootstrap-app")
    assert (explicit_db / "local/bootstrap-app-1.0-1/desc").is_file()
    assert (other / "usr/share/bootstrap-fixture").read_bytes() == payload
    assert not (other / "var/lib/pacman").exists()

    # Re-running must not need the repository or archive once installed.
    (sync / "fixture.txt").unlink()
    archive.unlink()
    run("get", "--root", target, "--needed", "--noconfirm", "bootstrap-app")

    # Canonicalizing aliases of / prevents bypassing live-root protections.
    result = run("get", "--root", "/tmp/..", "--noconfirm", "bash", success=False)
    assert "protected system package" in result.stderr

print("Bootstrap CLI checks passed")
