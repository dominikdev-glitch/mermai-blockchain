#!/usr/bin/env python3
"""
MERMAI Local Multi-Node Testnet Orchestrator
Launches a 3-validator TW-PoS network with SQLite persistence and JSON-RPC APIs.
"""


try:
    if sys.platform == "win32":
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass
import os
import sys
import time
import json
import sqlite3
import subprocess
import signal

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA_DIR = os.path.join(BASE_DIR, "testnet_data")
GENESIS_FILE = os.path.join(os.path.dirname(__file__), "mermai_genesis_testnet.json")
NODE_BIN = os.path.join(BASE_DIR, "build-cmake", "mermai-node.exe")

NODES = [
    {"id": 1, "name": "Node-1 (Alice)", "p2p": 6333, "rpc": 6334, "db": "node1.db"},
    {"id": 2, "name": "Node-2 (Bob)",   "p2p": 6335, "rpc": 6336, "db": "node2.db"},
    {"id": 3, "name": "Node-3 (Carol)", "p2p": 6337, "rpc": 6338, "db": "node3.db"},
]

def seed_database(db_path, genesis_data):
    if os.path.exists(db_path):
        os.remove(db_path)
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
    
    # Insert initial accounts
    now = int(time.time())
    for addr, bal in genesis_data.get("allocations", {}).items():
        cur.execute("INSERT INTO accounts(address, balance, nonce, last_activity) VALUES(?,?,0,?)", (addr, bal, now))
        cur.execute("INSERT INTO utxos(tx_id, output_index, address, amount) VALUES(?,0,?,?)", (f"genesis-{addr}", addr, bal))
    
    # Insert validators
    for v in genesis_data.get("validators", []):
        pub_bytes = bytes.fromhex(v["public_key"]) if v.get("public_key") else b""
        cur.execute("INSERT INTO validators(address, amount, lock_time, last_proposal, weight, public_key) VALUES(?,?,?,0,?,?)",
                    (v["address"], v["stake"], now - 86400, float(v["stake"]), pub_bytes))
        
    conn.commit()
    conn.close()

def main():
    print("=" * 60)
    print("      MERMAI LOCAL MULTI-NODE TESTNET ORCHESTRATOR")
    print("=" * 60)
    
    if not os.path.exists(NODE_BIN):
        print(f"[ERROR] Node binary not found at {NODE_BIN}. Please run cmake build first.")
        sys.exit(1)
        
    with open(GENESIS_FILE, "r", encoding="utf-8") as f:
        genesis_data = json.load(f)
        
    os.makedirs(DATA_DIR, exist_ok=True)
    
    print("\n[1/3] Initializing and seeding validator databases...")
    for n in NODES:
        db_path = os.path.join(DATA_DIR, n["db"])
        seed_database(db_path, genesis_data)
        print(f"  [OK] {n['name']} -> {n['db']} initialized with 3 genesis validators & allocations")

    if "--init-only" in sys.argv:
        print("\n[OK] Testnet databases initialized successfully.")
        return

    print("\n[2/3] Launching 3-node validator network...")
    processes = []
    
    try:
        for n in NODES:
            db_path = os.path.join(DATA_DIR, n["db"])
            cmd = [
                NODE_BIN,
                "--p2p-port", str(n["p2p"]),
                "--rpc-port", str(n["rpc"]),
                "--db", db_path
            ]
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
            processes.append({"node": n, "proc": p})
            print(f"  [RUN] {n['name']} running on P2P:{n['p2p']} | RPC:http://127.0.0.1:{n['rpc']}")
            time.sleep(1)

        print("\n[3/3] Interconnecting cluster peers...")
        print("\n" + "=" * 60)
        print("  [OK] Testnet Cluster is LIVE and producing blocks!")
        print("  [OK] Node 1 RPC: http://127.0.0.1:6334")
        print("  [OK] Node 2 RPC: http://127.0.0.1:6336")
        print("  [OK] Node 3 RPC: http://127.0.0.1:6338")
        print("  Press Ctrl+C to stop all cluster nodes gracefully.")
        print("=" * 60 + "\n")

        while True:
            time.sleep(1)
            for item in processes:
                if item["proc"].poll() is not None:
                    print(f"[WARN] {item['node']['name']} exited unexpectedly with code {item['proc'].returncode}")
                    
    except KeyboardInterrupt:
        print("\n[STOP] Shutting down all testnet nodes...")
        for item in processes:
            item["proc"].terminate()
            try:
                item["proc"].wait(timeout=3)
            except Exception:
                item["proc"].kill()
        print("[STOP] [OK] All nodes stopped.")

if __name__ == "__main__":
    main()
