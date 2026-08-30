#!/usr/bin/env python3
"""
MERMAI Mainnet Multi-Node Network Orchestrator
Launches a 3-validator TW-PoS cluster with SQLite persistence and JSON-RPC APIs.

Usage:
    python scripts/mermai_launch_network.py              # launch full cluster
    python scripts/mermai_launch_network.py --init-only  # seed databases only
"""

import os
import sys
import time
import json
import sqlite3
import subprocess
import threading

try:
    if sys.platform == "win32":
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA_DIR = os.path.join(BASE_DIR, "network_data")
GENESIS_FILE = os.path.join(BASE_DIR, "scripts", "network", "mermai_genesis_mainnet.json")
NODE_BIN = os.path.join(BASE_DIR, "build-cmake", "mermai-node.exe")

if not os.path.exists(NODE_BIN):
    NODE_BIN = os.path.join(BASE_DIR, "build-cmake", "mermai-node")

NODES = [
    {"id": 1, "name": "Node-1 (Alice)",  "p2p": 6333, "rpc": 6334, "db": "node1.db"},
    {"id": 2, "name": "Node-2 (Bob)",    "p2p": 6335, "rpc": 6336, "db": "node2.db"},
    {"id": 3, "name": "Node-3 (Carol)",  "p2p": 6337, "rpc": 6338, "db": "node3.db"},
]


def seed_database(db_path, genesis_data):
    """Create a fresh SQLite database and seed it with genesis validators and allocations."""
    if os.path.exists(db_path):
        try:
            os.remove(db_path)
        except Exception:
            pass
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("PRAGMA foreign_keys = ON")
    cur.execute("CREATE TABLE IF NOT EXISTS blocks (hash TEXT PRIMARY KEY, height INTEGER NOT NULL UNIQUE, previous_hash TEXT NOT NULL, timestamp INTEGER NOT NULL, serialized BLOB NOT NULL)")
    cur.execute("CREATE TABLE IF NOT EXISTS transactions (id TEXT PRIMARY KEY, block_hash TEXT, serialized BLOB NOT NULL, timestamp INTEGER NOT NULL)")
    cur.execute("CREATE TABLE IF NOT EXISTS utxos (tx_id TEXT NOT NULL, output_index INTEGER NOT NULL, address TEXT NOT NULL, amount INTEGER NOT NULL CHECK(amount > 0), PRIMARY KEY(tx_id, output_index))")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_utxos_address ON utxos(address)")
    cur.execute("CREATE TABLE IF NOT EXISTS accounts (address TEXT PRIMARY KEY, balance INTEGER NOT NULL, nonce INTEGER NOT NULL, last_activity INTEGER NOT NULL)")
    cur.execute("CREATE TABLE IF NOT EXISTS validators (address TEXT PRIMARY KEY, amount INTEGER NOT NULL, lock_time INTEGER NOT NULL, last_proposal INTEGER NOT NULL, weight REAL NOT NULL, public_key BLOB NOT NULL DEFAULT X'')")
    cur.execute("CREATE TABLE IF NOT EXISTS contract_storage (contract_address TEXT NOT NULL, storage_key TEXT NOT NULL, value BLOB NOT NULL, PRIMARY KEY(contract_address, storage_key))")
    cur.execute("CREATE TABLE IF NOT EXISTS contracts (contract_address TEXT PRIMARY KEY, code BLOB NOT NULL)")
    cur.execute("CREATE TABLE IF NOT EXISTS mempool (id TEXT PRIMARY KEY, serialized BLOB NOT NULL, timestamp INTEGER NOT NULL)")
    cur.execute("CREATE TABLE IF NOT EXISTS state_roots (block_height INTEGER PRIMARY KEY, state_root TEXT NOT NULL)")

    now = int(time.time())

    # Seed initial account balances
    for addr, bal in genesis_data.get("allocations", {}).items():
        cur.execute("INSERT INTO accounts(address, balance, nonce, last_activity) VALUES(?,?,0,?)", (addr, bal, now))
        cur.execute("INSERT INTO utxos(tx_id, output_index, address, amount) VALUES(?,0,?,?)", (f"genesis-{addr}", addr, bal))

    # Seed genesis validators
    for v in genesis_data.get("validators", []):
        pub_bytes = bytes.fromhex(v["public_key"]) if v.get("public_key") else b""
        cur.execute(
            "INSERT INTO validators(address, amount, lock_time, last_proposal, weight, public_key) VALUES(?,?,?,0,?,?)",
            (v["address"], v["stake"], now - 86400, float(v["stake"]), pub_bytes)
        )

    conn.commit()
    conn.close()


def stream_logs(pipe, prefix, log_file):
    """Continuously read process output and write to both stdout and logfile without blocking."""
    try:
        with open(log_file, "a", encoding="utf-8", errors="replace") as f:
            for line in iter(pipe.readline, ""):
                if line:
                    formatted = f"[{prefix}] {line.strip()}"
                    f.write(formatted + "\n")
                    f.flush()
                    if any(k in line for k in ["✓", "Produced", "Proposer", "Block #", "ERROR", "WARN", "Listening"]):
                        print(formatted, flush=True)
    except Exception:
        pass


def main():
    print("=" * 60, flush=True)
    print("         MERMAI MAINNET MULTI-NODE ORCHESTRATOR", flush=True)
    print("=" * 60, flush=True)

    if not os.path.exists(NODE_BIN):
        print(f"\n[ERROR] Node binary not found: {NODE_BIN}", flush=True)
        print("        Build with: cmake -B build-cmake && cmake --build build-cmake --config Release", flush=True)
        sys.exit(1)

    if not os.path.exists(GENESIS_FILE):
        print(f"\n[ERROR] Genesis file not found: {GENESIS_FILE}", flush=True)
        sys.exit(1)

    with open(GENESIS_FILE, "r", encoding="utf-8") as f:
        genesis_data = json.load(f)

    chain_name = genesis_data.get("chain_name", "Mermai Mainnet")
    network_id = genesis_data.get("network_id", 7331)
    print(f"\n  Chain:      {chain_name}", flush=True)
    print(f"  Network ID: {network_id}", flush=True)
    print(f"  Consensus:  {genesis_data.get('consensus', 'TW-PoS')}", flush=True)
    print(f"  Validators: {len(genesis_data.get('validators', []))}", flush=True)

    os.makedirs(DATA_DIR, exist_ok=True)

    # Phase 1: Seed databases
    print("\n[1/3] Initializing and seeding validator databases...", flush=True)
    for n in NODES:
        db_path = os.path.join(DATA_DIR, n["db"])
        seed_database(db_path, genesis_data)
        print(f"  [OK] {n['name']} -> {n['db']} initialized with genesis state", flush=True)

    if "--init-only" in sys.argv:
        print("\n[OK] Databases initialized successfully. Exiting (--init-only).", flush=True)
        return

    # Phase 2: Launch validator processes
    print("\n[2/3] Launching 3-node validator network...", flush=True)
    processes = []
    threads = []

    try:
        for n in NODES:
            db_path = os.path.join(DATA_DIR, n["db"])
            log_file = os.path.join(DATA_DIR, f"{n['db']}.log")
            if os.path.exists(log_file):
                try: os.remove(log_file)
                except Exception: pass

            cmd = [
                NODE_BIN,
                "--p2p-port", str(n["p2p"]),
                "--rpc-port", str(n["rpc"]),
                "--db", db_path
            ]
            p = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                encoding="utf-8",
                errors="replace"
            )
            processes.append({"node": n, "proc": p, "log": log_file})

            t = threading.Thread(target=stream_logs, args=(p.stdout, n["name"], log_file), daemon=True)
            t.start()
            threads.append(t)

            print(f"  [RUN] {n['name']} running on P2P:{n['p2p']} | RPC:http://127.0.0.1:{n['rpc']}", flush=True)
            time.sleep(1)

        # Phase 3: Announce readiness
        print("\n[3/3] Interconnecting cluster peers...", flush=True)
        print()
        print("=" * 60, flush=True)
        print(f"  Mermai {chain_name} Cluster is LIVE", flush=True)
        print()
        print("  Node 1 (Alice)  RPC: http://127.0.0.1:6334", flush=True)
        print("  Node 2 (Bob)    RPC: http://127.0.0.1:6336", flush=True)
        print("  Node 3 (Carol)  RPC: http://127.0.0.1:6338", flush=True)
        print()
        print("  Explorer: python mermai_explorer/mermai_serve.py", flush=True)
        print("            then open http://localhost:8080", flush=True)
        print()
        print("  Press Ctrl+C to stop all cluster nodes gracefully.", flush=True)
        print("=" * 60, flush=True)
        print()

        # Keep alive indefinitely and monitor child processes
        while True:
            time.sleep(2)
            for item in processes:
                rc = item["proc"].poll()
                if rc is not None:
                    print(f"  [WARN] {item['node']['name']} exited with code {rc}. Auto-restarting...", flush=True)
                    cmd = [
                        NODE_BIN,
                        "--p2p-port", str(item["node"]["p2p"]),
                        "--rpc-port", str(item["node"]["rpc"]),
                        "--db", os.path.join(DATA_DIR, item["node"]["db"])
                    ]
                    p = subprocess.Popen(
                        cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1,
                        encoding="utf-8",
                        errors="replace"
                    )
                    item["proc"] = p
                    t = threading.Thread(target=stream_logs, args=(p.stdout, item["node"]["name"], item["log"]), daemon=True)
                    t.start()

    except KeyboardInterrupt:
        print("\n[STOP] Shutting down all cluster nodes...", flush=True)
        for item in processes:
            try:
                item["proc"].terminate()
                item["proc"].wait(timeout=2)
            except Exception:
                try: item["proc"].kill()
                except Exception: pass
        print("[STOP] All nodes stopped cleanly.\n", flush=True)


if __name__ == "__main__":
    main()
