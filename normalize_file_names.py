#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path
from collections import defaultdict


def normalize_stem(name: str) -> str:
    """
    Convert to:
    - UPPERCASE
    - spaces & separators -> hyphens
    - remove non-alphanumeric
    - collapse multiple hyphens
    """
    name = name.upper()
    name = re.sub(r"[^\w\s-]", "", name)   # remove punctuation
    name = re.sub(r"[\s_]+", "-", name)    # spaces/underscores -> hyphen
    name = re.sub(r"-{2,}", "-", name)     # collapse hyphens
    return name.strip("-")


def build_plan(dir_path: Path) -> list[tuple[Path, Path]]:
    mids = sorted(p for p in dir_path.iterdir() if p.is_file() and p.suffix.lower() == ".mid")

    plan: list[tuple[Path, Path]] = []
    used_names: defaultdict[str, int] = defaultdict(int)

    for src in mids:
        base = normalize_stem(src.stem)

        used_names[base] += 1
        if used_names[base] == 1:
            dst_name = f"{base}.MID"
        else:
            dst_name = f"{base}-{used_names[base]}.MID"

        dst = src.with_name(dst_name)

        if src.name != dst.name:
            plan.append((src, dst))

    return plan


def apply_plan(plan: list[tuple[Path, Path]], dry_run: bool) -> None:
    if not plan:
        print("Nothing to rename.")
        return

    # Two-phase rename to avoid collisions
    tmp_pairs: list[tuple[Path, Path]] = []
    for src, _ in plan:
        tmp = src.with_name(src.name + ".__tmp__")
        tmp_pairs.append((src, tmp))

    def do(pairs):
        for a, b in pairs:
            if dry_run:
                print(f"DRYRUN: {a.name} -> {b.name}")
            else:
                a.rename(b)
                print(f"RENAMED: {a.name} -> {b.name}")

    do(tmp_pairs)
    do([(tmp, dst) for (_, tmp), (_, dst) in zip(tmp_pairs, plan)])


def main() -> None:
    ap = argparse.ArgumentParser(description="Normalize MIDI filenames for game use")
    ap.add_argument(
        "root",
        nargs="?",
        default="Assets/Sounds/MIDI",
        help="MIDI directory (default: Assets/Sounds/MIDI)",
    )
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = Path(args.root)
    if not root.exists():
        raise SystemExit(f"Path not found: {root}")

    plan = build_plan(root)
    apply_plan(plan, args.dry_run)


if __name__ == "__main__":
    main()