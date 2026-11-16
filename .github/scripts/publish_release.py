"""Publish the authorized 1.4.0 release from verified native build artifacts."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

TAG = "v1.4.0"
BUILD_RUN = "43854845439"
BUILD_COMMIT = "2775565ea979029af0af3cd7a02e55f5bea7b611"
PINNED = {
    "m1zuki-linux-x86_64.tar.gz": "6840991b0dcbc7f12869fbf27598bcebd902c318ca9178938192d617454ec5b8",
    "m1zuki-macos-arm64+x86_64.zip": "83c60f87cd8fddab3b20d25cc24102aa7b30fc90697ddbe80a776c589071c88a",
}
REPO = os.environ["GH_REPO"]
HEAD = os.environ["GITHUB_SHA"]
OUT = Path("release-assets")


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def api(path):
    return json.loads(run("gh", "api", f"repos/{REPO}/{path}"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_tag(required=False):
    refs = api(f"git/matching-refs/tags/{TAG}")
    ref = next((r for r in refs if r["ref"] == f"refs/tags/{TAG}"), None)
    if ref is None:
        assert not required, "Release tag was not created"
        return
    obj = ref["object"]
    while obj["type"] == "tag":
        obj = api(f"git/tags/{obj['sha']}")["object"]
    assert obj["type"] == "commit" and obj["sha"] == HEAD, "Release tag points to another commit"


def main():
    assert REPO == "mmionya/m1zuki", "Unexpected target repository"
    assert api("git/ref/heads/main")["object"]["sha"] == HEAD, "Publish the main commit only"
    verify_tag()
    build = api(f"actions/runs/{BUILD_RUN}")
    assert build["conclusion"] == "success" and build["head_sha"] == BUILD_COMMIT
    # Reusing binaries is safe only while application, resources and build inputs match.
    run("git", "diff", "--exit-code", BUILD_COMMIT, HEAD, "--", "CMakeLists.txt", "src", "assets", "packaging")
    OUT.mkdir(exist_ok=True)
    for name in ("m1zuki-linux-x86_64", "m1zuki-macos-universal"):
        run("gh", "run", "download", BUILD_RUN, "--name", name, "--dir", str(OUT))
    run("gh", "run", "download", os.environ["GITHUB_RUN_ID"], "--name", "m1zuki-windows-release", "--dir", str(OUT))
    for name, digest in PINNED.items():
        assert sha(OUT / name) == digest, f"Changed native artifact: {name}"
    source = OUT / "m1zuki-1.4.0-source.zip"
    run("git", "archive", "--format=zip", "--prefix=m1zuki/", "--output", str(source), HEAD)
    archives = [OUT / name for name in PINNED] + [OUT / "m1zuki-windows-1.4.0-x64.zip", source]
    for path in archives:
        assert path.stat().st_size > 0
        if path.suffix == ".zip":
            with zipfile.ZipFile(path) as archive:
                assert archive.testzip() is None, f"Corrupt ZIP: {path}"
    sums = OUT / "SHA256SUMS.txt"
    sums.write_text("".join(f"{sha(p)}  {p.name}\n" for p in archives), encoding="ascii")
    report = OUT / "BUILD-STATUS.md"
    report.write_text(Path("BUILD-STATUS.md").read_text(encoding="utf-8")
                      + f"\nRelease Windows build and publication: https://github.com/{REPO}/actions/runs/{os.environ['GITHUB_RUN_ID']}\n"
                      + f"Release source commit: `{HEAD}`. Windows CI: all 5 CTest groups passed.\n", encoding="utf-8")
    assets = archives + [sums, report]
    releases = api("releases?per_page=100")
    existing = next((r for r in releases if r["tag_name"] == TAG), None)
    if existing:
        assert existing["draft"] and existing["target_commitish"] == HEAD, "Refusing to overwrite a different or published release"
        run("gh", "release", "upload", TAG, *map(str, assets), "--clobber")
    else:
        run("gh", "release", "create", TAG, *map(str, assets), "--draft", "--target", HEAD,
            "--title", "m1zuki 1.4.0", "--notes-file", ".github/release-notes-1.4.0.md")
    release = next(r for r in api("releases?per_page=100") if r["tag_name"] == TAG)
    uploaded = {a["name"]: a for a in api(f"releases/{release['id']}/assets?per_page=100")}
    assert set(uploaded) == {p.name for p in assets}, "Unexpected release assets"
    for path in assets:
        asset = uploaded[path.name]
        assert asset["state"] == "uploaded" and asset["size"] == path.stat().st_size
        assert asset["digest"] == "sha256:" + sha(path), f"Release checksum mismatch: {path}"
    run("gh", "release", "edit", TAG, "--draft=false", "--latest")
    published = api(f"releases/tags/{TAG}")
    assert not published["draft"] and not published["prerelease"]
    verify_tag(required=True)
    print(published["html_url"])


if __name__ == "__main__":
    main()
