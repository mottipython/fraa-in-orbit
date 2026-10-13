#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

fraa.jpeg is a sheet: six drawings of the fraa in his suit on one flat magenta
background, not touching, read off in rows, left to right, and kept at one scale.
planet.jpeg is Arbre on magenta. The sky, the sphere, the glow and the scroll are
shared with the other screensavers and are already in assets/art. Needs ImageMagick
and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def runs(on, gap):
    """Stretches where `on` is true, joining ones separated by less than `gap`."""
    out, start, last = [], None, None
    for i, v in enumerate(on):
        if not v: continue
        if start is None: start = i
        elif i - last > gap: out.append((start, last + 1)); start = i
        last = i
    if start is not None: out.append((start, last + 1))
    return [r for r in out if r[1] - r[0] > 30]

def pieces(a, rows):
    """The separate drawings on a sheet, in reading order: split into rows by the
    empty bands between them, then each row into drawings by the empty gaps."""
    solid = a[..., 3] > 128
    bands = runs(solid.sum(axis=1) > 2, 40)
    if len(bands) != rows:
        raise SystemExit(f"found {len(bands)} rows of drawings, expected {rows}")
    out = []
    for y0, y1 in bands:
        for x0, x1 in runs(solid[y0:y1].sum(axis=0) > 2, 40):
            out.append(a[y0:y1, x0:x1])
    return out

def trim(a):
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 2)[0]; cols = np.where(solid.sum(axis=0) > 2)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

NAMES = ["f_wave", "f_drift", "f_thrust", "f_puzzled", "f_dismay", "f_glad"]

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    got = [trim(p) for p in pieces(key(load(f"{SRC}/fraa.jpeg")), 2)]
    if len(got) != len(NAMES):
        raise SystemExit(f"fraa: found {len(got)} drawings, expected {len(NAMES)}")
    f = 420 / max(p.shape[0] for p in got)  # one scale, so he is the same size in each
    for name, p in zip(NAMES, got):
        w, h = max(1, round(p.shape[1] * f)), max(1, round(p.shape[0] * f))
        save(p, w, h, f"{OUT}/{name}.png")
        print(name, w, h)
    p = trim(key(load(f"{SRC}/planet.jpeg")))
    save(p, 640, max(1, round(p.shape[0] * 640 / p.shape[1])), f"{OUT}/planet.png")
    print("planet")

if __name__ == "__main__":
    main()
