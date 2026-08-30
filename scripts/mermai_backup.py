#!/usr/bin/env python3
"""
Mermai Blockchain Database Backup Script
Phase 5 - Operational Tooling

Usage:
    python3 scripts/backup.py --db mainnet.db --dest /mnt/backup/
"""

import argparse
import datetime
import os
import shutil
import sys


def backup(db_path: str, dest_dir: str) -> str:
    if not os.path.isfile(db_path):
        print(f"[ERROR] Database not found: {db_path}")
        sys.exit(1)

    os.makedirs(dest_dir, exist_ok=True)
    ts = datetime.datetime.utcnow().strftime("%Y%m%d_%H%M%S")
    db_name = os.path.basename(db_path)
    dest_name = f"{os.path.splitext(db_name)[0]}_backup_{ts}.db"
    dest_path = os.path.join(dest_dir, dest_name)

    print(f"[Backup] {db_path} -> {dest_path}")
    shutil.copy2(db_path, dest_path)

    size_mb = os.path.getsize(dest_path) / (1024 * 1024)
    print(f"[OK] Backup complete. Size: {size_mb:.2f} MB")
    return dest_path


def prune_old_backups(dest_dir: str, keep: int = 10) -> None:
    backups = sorted([
        f for f in os.listdir(dest_dir) if "_backup_" in f and f.endswith(".db")
    ])
    while len(backups) > keep:
        old = backups.pop(0)
        os.remove(os.path.join(dest_dir, old))
        print(f"[Prune] Removed old backup: {old}")


def main():
    parser = argparse.ArgumentParser(description="Mermai blockchain DB backup")
    parser.add_argument("--db", default="mermai.db", help="Path to database file")
    parser.add_argument("--dest", default="backups/", help="Backup destination directory")
    parser.add_argument("--keep", type=int, default=10, help="Number of backups to retain")
    args = parser.parse_args()

    backup(args.db, args.dest)
    prune_old_backups(args.dest, args.keep)


if __name__ == "__main__":
    main()
