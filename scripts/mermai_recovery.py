#!/usr/bin/env python3
"""
Mermai Blockchain Recovery Script
Phase 5 - Operational Tooling

Rolls back a corrupted chain to the last known-good state root height.
Usage:
    python3 scripts/recovery.py --db mainnet.db --height 1000
"""

import argparse
import os
import shutil
import sqlite3
import sys


def get_state_root(conn: sqlite3.Connection, height: int):
    row = conn.execute(
        "SELECT state_root FROM state_roots WHERE block_height = ?", (height,)
    ).fetchone()
    return row[0] if row else None


def rollback_to(db_path: str, target_height: int) -> None:
    if not os.path.isfile(db_path):
        print(f"[ERROR] Database not found: {db_path}")
        sys.exit(1)

    # Safety: backup before rollback
    backup_path = db_path + ".pre_recovery.bak"
    shutil.copy2(db_path, backup_path)
    print(f"[Safety] Pre-recovery backup: {backup_path}")

    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA foreign_keys = ON")

    # Verify target state root exists
    root = get_state_root(conn, target_height)
    if root is None:
        print(f"[ERROR] No state root found at height {target_height}. Cannot recover.")
        conn.close()
        sys.exit(1)

    print(f"[OK] Target state root at height {target_height}: {root[:16]}...")

    # Delete blocks above target height
    deleted_blocks = conn.execute(
        "DELETE FROM blocks WHERE height > ?", (target_height,)
    ).rowcount
    print(f"[Rollback] Removed {deleted_blocks} blocks above height {target_height}")

    # Delete state roots above target
    deleted_roots = conn.execute(
        "DELETE FROM state_roots WHERE block_height > ?", (target_height,)
    ).rowcount
    print(f"[Rollback] Removed {deleted_roots} state root entries above height {target_height}")

    # Clear mempool (stale after rollback)
    conn.execute("DELETE FROM mempool")
    print("[Rollback] Cleared mempool")

    conn.commit()
    conn.close()

    print(f"[OK] Recovery complete. Chain rolled back to height {target_height}.")
    print(f"     Restart the node to rebuild from this checkpoint.")


def main():
    parser = argparse.ArgumentParser(description="Mermai chain recovery / rollback tool")
    parser.add_argument("--db", default="mermai.db", help="Path to database file")
    parser.add_argument("--height", type=int, required=True,
                        help="Target rollback height (must have a saved state root)")
    args = parser.parse_args()

    print(f"Mermai Recovery: rolling back {args.db} to height {args.height}")
    rollback_to(args.db, args.height)


if __name__ == "__main__":
    main()
