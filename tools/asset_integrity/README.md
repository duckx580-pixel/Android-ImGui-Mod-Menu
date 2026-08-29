# Asset Integrity Verifier

A small, read-only Python tool for verifying that a client's asset files
match a trusted reference. It never modifies, moves, or deletes anything it
scans — it only reads files to compute their SHA-256 hash and writes a
report to a separate output path you choose.

## How it works

1. **`snapshot`** — walk a directory you trust is unmodified (a clean
   install, a verified backup, etc.) and record each file's relative path,
   size, and SHA-256 hash into a reference manifest (JSON).
2. **`verify`** — walk another directory (e.g. the live client's asset
   folder at launch) and diff it against that reference manifest. Every
   difference is classified and written to a structured JSON report:
   - `missing` — a file in the manifest is absent from the scanned directory
   - `modified` — a file's content hash no longer matches
   - `size_mismatch` — same as modified, but the size differs too
   - `extra` — a file exists in the scanned directory but isn't in the manifest

## Usage

Build the reference manifest once, from an install you trust:

```bash
python3 verify_assets.py snapshot \
    --root /path/to/known-good/assets \
    --out reference_manifest.json
```

Later, verify a (possibly different) copy against that reference:

```bash
python3 verify_assets.py verify \
    --root /path/to/assets \
    --manifest reference_manifest.json \
    --report report.json \
    --fail-on-discrepancy   # optional: exit 1 if anything differs
```

`report.json` looks like:

```json
{
  "generated_at": "2026-08-29T00:00:00+00:00",
  "root": "/path/to/assets",
  "manifest": "reference_manifest.json",
  "summary": {
    "expected_files": 512,
    "scanned_files": 513,
    "discrepancy_count": 2,
    "by_kind": {"modified": 1, "extra": 1},
    "status": "discrepancies_found"
  },
  "discrepancies": [
    {"path": "cache/items.dat", "kind": "modified", "expected_size": 123456, "actual_size": 123456, "expected_sha256": "...", "actual_sha256": "..."},
    {"path": "cache/unexpected.bin", "kind": "extra", "actual_size": 42, "actual_sha256": "..."}
  ]
}
```

## Notes

- This tool has no game-specific knowledge of Growtopia's own update/manifest
  protocol — it works against a generic `{path, size, sha256}` manifest
  format that you build yourself from a trusted source. That keeps it
  reusable and auditable, and avoids relying on any undocumented internals
  of the game's real asset-delivery format.
- Wiring this into "at launch" behavior (e.g. running `verify` automatically
  before the client starts, or from a startup hook) is a separate,
  environment-specific step left to you — this tool only does the
  read-and-report part.
