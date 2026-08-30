import urllib.request
import json
import sys
import socket

NODES = [
    {"name": "Node 1 (Alice)", "host": "127.0.0.1", "port": 6334},
    {"name": "Node 2 (Bob)",   "host": "127.0.0.1", "port": 6336},
    {"name": "Node 3 (Carol)", "host": "127.0.0.1", "port": 6338},
]

def is_port_open(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.2)
    try:
        s.connect((host, port))
        s.close()
        return True
    except Exception:
        return False

def rpc_call(url, method, params=None):
    payload = {
        "jsonrpc": "2.0",
        "method": method,
        "id": 1
    }
    if params:
        payload["params"] = params
    try:
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=0.8) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data.get("result")
    except Exception:
        return None

def main():
    print("\n" + "=" * 70)
    print("                 MERMAI TESTNET CLUSTER STATUS")
    print("=" * 70)
    print(f"{'Node':<18} | {'Status':<8} | {'Height':<8} | {'Finalized':<10} | {'Validators':<10} | {'Peers'}")
    print("-" * 70)

    for n in NODES:
        url = f"http://{n['host']}:{n['port']}"
        if not is_port_open(n["host"], n["port"]):
            print(f"{n['name']:<18} | OFFLINE  | {'-':<8} | {'-':<10} | {'-':<10} | -")
            continue

        height = rpc_call(url, "mrm_blockNumber")
        finalized = rpc_call(url, "mrm_getFinalizedBlock")
        validators = rpc_call(url, "mrm_getAllValidators")
        peers = rpc_call(url, "mrm_peerCount")

        val_count = len(validators) if isinstance(validators, list) else "?"
        fin_height = finalized.get("height", 0) if isinstance(finalized, dict) else "?"
        print(f"{n['name']:<18} | ONLINE   | {str(height):<8} | {str(fin_height):<10} | {str(val_count):<10} | {str(peers)}")

    print("=" * 70 + "\n")

if __name__ == "__main__":
    main()
