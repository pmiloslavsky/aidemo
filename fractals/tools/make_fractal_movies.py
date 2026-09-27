#!/usr/bin/env python3
"""Render a fractal animation from a JSON key, then make a GIF and two MP4s.

Each frame zooms in a little and moves the light, starting from a seed key
saved with the app's "Save Key" button (FractalsData/keys/*.json). The app
renders every frame with:  fractals --save-and-exit <frame key> <png> --hide
and writes the key it actually rendered to changed_key.json, which seeds the
next frame.

Example:  python make_fractal_movies.py 2 5 60 --key FractalsData/keys/my.json
The GIF and MP4 steps need Pillow and moviepy (pip install pillow moviepy);
without them only the PNG frames are made.
"""
import argparse
import glob
import json
import math
import os
import platform
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE_NAME = "fractals.exe" if platform.system() == "Windows" else "fractals"


def default_exe():
    """The newest release build in fractals/build, if there is one."""
    found = glob.glob(os.path.join(HERE, "..", "build", "*-release*", "bin", EXE_NAME))
    return max(found, key=os.path.getmtime) if found else EXE_NAME


def read_key(name):
    with open(name, encoding="utf-8") as f:
        return json.load(f)


def write_key(name, key):
    with open(name, "w", encoding="utf-8") as f:
        json.dump(key, f, indent=2)


def create_evolved_frames(args):
    """Write one key per frame and have the app render it."""
    count = args.tf
    key = read_key(args.key)
    print("Seed key:", json.dumps(key, indent=2))

    for j in range(count):
        if j != 0:
            # The key the app rendered last time, with this frame's zoom applied
            key = read_key(os.path.join(args.out, "changed_key.json"))

        view = key.setdefault("view", {})
        zoom = view.get("requested_zoom", view.get("zoom", 1.0))
        view["requested_zoom"] = zoom / (1.0 + (args.z / count))

        # Move the light around a circle of radius 2 in fractal coordinates
        angle = args.lr * j * 2 * math.pi / count
        lighting = key.setdefault("lighting", {})
        lighting["pos_r"] = 2 * math.cos(angle)
        lighting["pos_i"] = 2 * math.sin(angle)

        frame_key = "frame_{:05d}.json".format(j)
        png = "frame_{:05d}.png".format(j)
        write_key(os.path.join(args.out, frame_key), key)
        print("Rendering frame {}/{}: {}".format(j + 1, count, frame_key))
        # Run inside the output folder: the app writes changed_key.json there
        result = subprocess.run([args.exe, "--save-and-exit", frame_key, png, "--hide"], cwd=args.out)
        if result.returncode != 0 or not os.path.exists(os.path.join(args.out, png)):
            sys.exit("frame {} failed (exit code {})".format(j, result.returncode))


def frame_pngs(args):
    return sorted(glob.glob(os.path.join(args.out, "frame_*.png")))


def create_gif(args):
    from PIL import Image

    frames = [Image.open(p) for p in frame_pngs(args)]
    # duration is the display time of each frame in ms; loop forever
    frames[0].save(os.path.join(args.out, args.name + ".gif"), format="GIF",
                   append_images=frames[1:], save_all=True, duration=args.tg, loop=0)


def create_movie(args, fps, movie_name):
    try:
        from moviepy import ImageSequenceClip  # moviepy 2.x
    except ImportError:
        from moviepy.video.io.ImageSequenceClip import ImageSequenceClip  # 1.x
    clip = ImageSequenceClip(frame_pngs(args), fps=fps)
    clip.write_videofile(os.path.join(args.out, movie_name + ".mp4"))


def main():
    parser = argparse.ArgumentParser(description="Create a GIF and 2 movies from a fractal evolution")
    parser.add_argument("lr", type=float, help="total full light rotations")
    parser.add_argument("z", type=float, help="zoom in z times")
    parser.add_argument("tf", type=int, help="total frames")
    parser.add_argument("--key", default="movie_base.json", help="seed key (JSON)")
    parser.add_argument("--exe", default=default_exe(), help="fractals executable")
    parser.add_argument("--out", default="fractal_movie", help="output folder")
    parser.add_argument("--name", default="shadow", help="base name of the GIF and MP4s")
    parser.add_argument("--tg", type=int, default=20, help="GIF display time per frame in ms")
    parser.add_argument("--frames-only", action="store_true", help="only render the PNG frames")
    parser.add_argument("--keep-frames", action="store_true", help="keep the frame PNGs and keys")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    print("Using", args.exe, "writing to", os.path.abspath(args.out))

    create_evolved_frames(args)
    if args.frames_only:
        return

    create_gif(args)
    create_movie(args, 5, args.name + "_slow")
    create_movie(args, 30, args.name + "_fast")

    if not args.keep_frames:
        for p in glob.glob(os.path.join(args.out, "frame_*")):
            os.remove(p)
        os.remove(os.path.join(args.out, "changed_key.json"))


if __name__ == "__main__":
    main()
