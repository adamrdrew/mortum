#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


FRAME_RE = re.compile(r"^(?P<weapon>[A-Z0-9_]+)-(?P<num>\d+)$")
ICON_RE = re.compile(r"^(?P<weapon>[A-Z0-9_]+)-ICON$", re.IGNORECASE)


def build_plan(dir_path: Path) -> list[tuple[Path, Path]]:
    """
    For a weapon dir containing e.g.:
      RIFLE-1.png, RIFLE-2.png, ..., RIFLE-N.png, RIFLE-ICON.png
    produce renames:
      -1 -> -PICKUP
      -2 -> -IDLE
      -3..N -> -SHOOT-(num-2)
    """
    plan: list[tuple[Path, Path]] = []

    pngs = sorted(p for p in dir_path.iterdir() if p.is_file() and p.suffix.lower() == ".png")

    for src in pngs:
        stem = src.stem

        # leave icons alone
        if ICON_RE.match(stem):
            continue

        m = FRAME_RE.match(stem)
        if not m:
            # unknown name; ignore rather than guess
            continue

        weapon = m.group("weapon").upper()
        num = int(m.group("num"))

        if num == 1:
            dst = src.with_name(f"{weapon}-PICKUP.png")
        elif num == 2:
            dst = src.with_name(f"{weapon}-IDLE.png")
        elif num >= 3:
            shoot_idx = num - 2  # 3->1, 4->2, ...
            dst = src.with_name(f"{weapon}-SHOOT-{shoot_idx}.png")
        else:
            continue

        if dst.name != src.name:
            plan.append((src, dst))

    return plan


def apply_plan(plan: list[tuple[Path, Path]], dry_run: bool) -> None:
    if not plan:
        return

    # Detect collisions within the plan
    dsts = [dst for _, dst in plan]
    if len(dsts) != len(set(dsts)):
        counts = {}
        for _, d in plan:
            counts[str(d)] = counts.get(str(d), 0) + 1
        dupes = [k for k, v in counts.items() if v > 1]
        raise RuntimeError("Collision: multiple sources map to the same destination:\n  " + "\n  ".join(dupes))

    # Refuse overwrites
    for src, dst in plan:
        if dst.exists() and dst.resolve() != src.resolve():
            raise RuntimeError(f"Refusing to overwrite existing file: {dst}")

    # Two-phase rename avoids ordering problems
    tmp_pairs: list[tuple[Path, Path]] = []
    for src, _dst in plan:
        tmp = src.with_name(src.name + ".__tmp__")
        tmp_pairs.append((src, tmp))

    def do_rename(pairs: list[tuple[Path, Path]]) -> None:
        for a, b in pairs:
            if dry_run:
                print(f"DRYRUN: {a} -> {b}")
            else:
                a.rename(b)
                print(f"RENAMED: {a} -> {b}")

    do_rename(tmp_pairs)
    do_rename([(tmp, dst) for (_, tmp), (_, dst) in zip(tmp_pairs, plan)])


def main() -> None:
    ap = argparse.ArgumentParser(description="Rename weapon frame PNGs to PICKUP/IDLE/SHOOT-* naming.")
    ap.add_argument(
        "root",
        nargs="?",
        default="Assets/Images/Weapons",
        help="Weapons root directory (default: Assets/Images/Weapons)",
    )
    ap.add_argument("--dry-run", action="store_true", help="Print what would change without renaming")
    args = ap.parse_args()

    root = Path(args.root)
    if not root.exists():
        raise SystemExit(f"Path not found: {root}")

    for weapon_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        plan = build_plan(weapon_dir)
        apply_plan(plan, dry_run=args.dry_run)


if __name__ == "__main__":
    main()