#!/usr/bin/env python3
"""Manual KDE desktop check; never contacts an API or changes application settings.

Requires a built privacy_probe, Spectacle, Pillow and numpy. Covers only the
KWin screenshot path, not a browser/PipeWire recording. A single screen is
required so the crop cannot accidentally include another monitor's contents.
Only a synthetic 200x160 corner is retained; every full screenshot is deleted
immediately after the crop is copied, including on failure.
"""

import argparse
import json
from pathlib import Path
import selectors
import subprocess
import tempfile
import time

import numpy as np
from PIL import Image


def start_probe(executable, mode):
    process = subprocess.Popen(
        [str(executable), mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + 10
    try:
        while time.monotonic() < deadline:
            if not selector.select(timeout=0.2):
                continue
            line = process.stdout.readline().strip()
            if line:
                print(line, flush=True)
            if line.startswith("READY "):
                return process, json.loads(line.removeprefix("READY "))
            if line.startswith("FAIL ") or process.poll() is not None:
                raise RuntimeError(f"{mode} did not become ready")
        raise RuntimeError(f"{mode} timed out")
    except BaseException:
        stop_probe(process)
        raise
    finally:
        selector.close()


def stop_probe(process):
    if process.poll() is None:
        try:
            process.stdin.write("quit\n")
            process.stdin.flush()
            process.wait(timeout=3)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            process.terminate()
            process.wait(timeout=3)
    process.stdin.close()
    process.stdout.close()


def capture_corner(info):
    if info["screen_count"] != 1:
        raise RuntimeError("Automatic crop verification requires exactly one monitor")
    with tempfile.TemporaryDirectory(prefix="minianswer-private-capture-") as directory:
        full = Path(directory) / "capture.png"
        try:
            subprocess.run(
                ["spectacle", "--new-instance", "--background", "--nonotify", "--fullscreen",
                 "--output", str(full)],
                check=True, timeout=12, stdout=subprocess.DEVNULL,
            )
            with Image.open(full) as image:
                scale_x = image.width / info["screen_width"]
                scale_y = image.height / info["screen_height"]
                if abs(scale_x - scale_y) > 0.01:
                    raise RuntimeError("Screenshot dimensions do not match the probe screen")
                width = round(info["capture_width"] * scale_x)
                height = round(info["capture_height"] * scale_y)
                crop = image.crop((0, 0, width, height)).convert("RGB")
                crop.load()
        finally:
            full.unlink(missing_ok=True)
        return crop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output_dir = args.output_dir or Path(tempfile.mkdtemp(prefix="minianswer-synthetic-crops-"))
    output_dir.mkdir(parents=True, exist_ok=True)
    background, info = start_probe(executable, "--background")
    try:
        baseline = capture_corner(info)
        baseline_pixels = np.asarray(baseline, dtype=np.int16)
        # Before persisting anything, verify the whole crop is the synthetic
        # background. Refuse to retain a crop containing unrelated screen data.
        if np.max(np.ptp(baseline_pixels, axis=(0, 1))) > 2:
            raise RuntimeError("Baseline is not a solid synthetic rectangle; no crops saved")
        baseline.save(output_dir / "baseline.png")

        unprotected, _ = start_probe(executable, "--unprotected")
        try:
            control = capture_corner(info)
        finally:
            stop_probe(unprotected)
        difference = np.any(np.abs(np.asarray(control, dtype=np.int16) - baseline_pixels) > 3, axis=2)
        changed = int(np.count_nonzero(difference))
        if changed < 100:
            raise RuntimeError(f"Visibility control failed: only {changed} changed pixels")
        control.save(output_dir / "unprotected.png")

        protected, protected_info = start_probe(executable, "--protected")
        try:
            hidden = capture_corner(info)
        finally:
            stop_probe(protected)
        hidden.save(output_dir / "protected.png")
        hidden_pixels = np.asarray(hidden, dtype=np.int16)
        leaked = int(np.count_nonzero(np.any(np.abs(hidden_pixels - baseline_pixels) > 3, axis=2)))
        if leaked:
            raise RuntimeError(f"Capture exclusion failed: {leaked} pixels differ from background")
        print(json.dumps({
            "result": "PASS", "capture_path": "Spectacle/KWin screenshot",
            "unprotected_changed_pixels": changed, "protected_changed_pixels": leaked,
            "protected_window_visible": protected_info["visible"],
            "protected_window_exposed": protected_info["exposed"],
            "artifacts": str(output_dir.resolve()),
            "browser_pipewire_verified": False,
        }, ensure_ascii=False), flush=True)
    finally:
        stop_probe(background)


if __name__ == "__main__":
    main()
