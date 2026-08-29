#!/usr/bin/env python3
"""
Asset integrity verifier.

Read-only tool: computes hashes of files under a target directory (e.g. an
extracted client's asset folder) and compares them against a trusted
reference manifest. Every discrepancy (missing, modified, or unexpected
extra file) is written to a structured JSON report. The scanned files are
never modified, moved, or deleted.

Manifest format (JSON):
{
  "generated_at": "2026-08-29T00:00:00Z",
  "root": "assets",
  "files": [
    {"path": "cache/items.dat", "size": 123456, "sha256": "..."},
    {"path": "cache/cache.rttex", "size": 42, "sha256": "..."}
  ]
}

Typical workflow:
  1. Build a trusted reference manifest once, from a client install you
     know is unmodified:
       python verify_assets.py snapshot --root /path/to/known-good/assets \\
           --out reference_manifest.json
  2. Later, verify another copy (e.g. the client at launch, or after an
     update) against that reference:
       python verify_assets.py verify --root /path/to/assets \\
           --manifest reference_manifest.json --report report.json
"""

import argparse
import hashlib
import json
import os
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, List, Optional

CHUNK_SIZE = 1024 * 1024  # 1 MiB


@dataclass
class FileRecord:
    path: str  # POSIX-style, relative to root
    size: int
    sha256: str


@dataclass
class Discrepancy:
    path: str
    kind: str  # "missing" | "modified" | "extra" | "size_mismatch"
    expected_size: Optional[int] = None
    actual_size: Optional[int] = None
    expected_sha256: Optional[str] = None
    actual_sha256: Optional[str] = None


def sha256_of_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def to_posix(rel_path: str) -> str:
    return rel_path.replace(os.sep, "/")


def scan_directory(root: str) -> Dict[str, FileRecord]:
    """Read-only walk of `root`; never writes to any file under it."""
    records: Dict[str, FileRecord] = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            full_path = os.path.join(dirpath, name)
            rel_path = to_posix(os.path.relpath(full_path, root))
            try:
                size = os.path.getsize(full_path)
                digest = sha256_of_file(full_path)
            except OSError as exc:
                print(f"warning: could not read {full_path}: {exc}", file=sys.stderr)
                continue
            records[rel_path] = FileRecord(path=rel_path, size=size, sha256=digest)
    return records


def load_manifest(manifest_path: str) -> Dict[str, FileRecord]:
    with open(manifest_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    records: Dict[str, FileRecord] = {}
    for entry in data.get("files", []):
        rec = FileRecord(
            path=to_posix(entry["path"]),
            size=int(entry["size"]),
            sha256=str(entry["sha256"]).lower(),
        )
        records[rec.path] = rec
    return records


def write_manifest(root: str, records: Dict[str, FileRecord], out_path: str) -> None:
    payload = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "root": root,
        "files": [
            {"path": r.path, "size": r.size, "sha256": r.sha256}
            for r in sorted(records.values(), key=lambda r: r.path)
        ],
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")


def diff_records(
    expected: Dict[str, FileRecord], actual: Dict[str, FileRecord]
) -> List[Discrepancy]:
    discrepancies: List[Discrepancy] = []

    for path, exp in expected.items():
        act = actual.get(path)
        if act is None:
            discrepancies.append(
                Discrepancy(
                    path=path,
                    kind="missing",
                    expected_size=exp.size,
                    expected_sha256=exp.sha256,
                )
            )
            continue

        if act.sha256 != exp.sha256:
            kind = "size_mismatch" if act.size != exp.size else "modified"
            discrepancies.append(
                Discrepancy(
                    path=path,
                    kind=kind,
                    expected_size=exp.size,
                    actual_size=act.size,
                    expected_sha256=exp.sha256,
                    actual_sha256=act.sha256,
                )
            )

    for path, act in actual.items():
        if path not in expected:
            discrepancies.append(
                Discrepancy(
                    path=path,
                    kind="extra",
                    actual_size=act.size,
                    actual_sha256=act.sha256,
                )
            )

    return discrepancies


def write_report(
    root: str,
    manifest_path: str,
    expected_count: int,
    actual_count: int,
    discrepancies: List[Discrepancy],
    out_path: str,
) -> None:
    by_kind: Dict[str, int] = {}
    for d in discrepancies:
        by_kind[d.kind] = by_kind.get(d.kind, 0) + 1

    payload = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "root": root,
        "manifest": manifest_path,
        "summary": {
            "expected_files": expected_count,
            "scanned_files": actual_count,
            "discrepancy_count": len(discrepancies),
            "by_kind": by_kind,
            "status": "clean" if not discrepancies else "discrepancies_found",
        },
        "discrepancies": [
            {
                "path": d.path,
                "kind": d.kind,
                "expected_size": d.expected_size,
                "actual_size": d.actual_size,
                "expected_sha256": d.expected_sha256,
                "actual_sha256": d.actual_sha256,
            }
            for d in sorted(discrepancies, key=lambda d: d.path)
        ],
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")


def cmd_snapshot(args: argparse.Namespace) -> int:
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: root directory not found: {root}", file=sys.stderr)
        return 1

    print(f"Scanning {root} ...")
    records = scan_directory(root)
    write_manifest(root, records, args.out)
    print(f"Wrote reference manifest with {len(records)} files to {args.out}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: root directory not found: {root}", file=sys.stderr)
        return 1
    if not os.path.isfile(args.manifest):
        print(f"error: manifest not found: {args.manifest}", file=sys.stderr)
        return 1

    expected = load_manifest(args.manifest)
    print(f"Loaded {len(expected)} expected files from {args.manifest}")

    print(f"Scanning {root} ...")
    actual = scan_directory(root)
    print(f"Scanned {len(actual)} files")

    discrepancies = diff_records(expected, actual)
    write_report(root, args.manifest, len(expected), len(actual), discrepancies, args.report)

    if discrepancies:
        print(f"Found {len(discrepancies)} discrepancies. Report: {args.report}")
    else:
        print(f"No discrepancies found. Report: {args.report}")

    return 1 if (discrepancies and args.fail_on_discrepancy) else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read-only asset integrity verifier (snapshot + verify)."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_snap = sub.add_parser(
        "snapshot", help="Build a reference manifest from a trusted asset directory."
    )
    p_snap.add_argument("--root", required=True, help="Path to the trusted asset directory.")
    p_snap.add_argument("--out", required=True, help="Path to write the reference manifest JSON.")
    p_snap.set_defaults(func=cmd_snapshot)

    p_verify = sub.add_parser(
        "verify", help="Verify an asset directory against a reference manifest."
    )
    p_verify.add_argument("--root", required=True, help="Path to the asset directory to check.")
    p_verify.add_argument("--manifest", required=True, help="Path to the reference manifest JSON.")
    p_verify.add_argument("--report", required=True, help="Path to write the structured report JSON.")
    p_verify.add_argument(
        "--fail-on-discrepancy",
        action="store_true",
        help="Exit with status 1 if any discrepancy is found (useful in CI/launch hooks).",
    )
    p_verify.set_defaults(func=cmd_verify)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
