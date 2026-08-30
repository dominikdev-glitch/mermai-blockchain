#!/usr/bin/env python3
"""
Mermai MRM-20 Token Deployment & Interaction Suite
Usage:
    python3 scripts/token_deploy.py deploy --name "Mermai USD" --symbol "mUSD" --supply 1000000 --owner alice
"""

import argparse
import hashlib
import json
import urllib.request
import sys

RPC_URL = "http://localhost:7400"

def rpc_call(method, params=None, url=RPC_URL):
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "params": params or {}
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            res = json.loads(response.read().decode("utf-8"))
            if "error" in res:
                print(f"[RPC Error] {res['error']}")
                return None
            return res.get("result")
    except Exception as e:
        print(f"[Error connecting to {url}] {e}")
        return None

def deploy_token(name, symbol, decimals, supply, owner):
    print(f"Deploying MRM-20 Token: {name} ({symbol}) with {supply} initial supply to {owner}...")
    
    metadata = f"{name}:{symbol}:{decimals}:{supply}:{owner}"
    bytecode = "6001600055" + metadata.encode("utf-8").hex() + "00"

    digest = hashlib.sha256(bytes.fromhex(bytecode)).hexdigest()
    contract_addr = "0x" + digest[:40]

    result = rpc_call("mrm_deployContract", {"bytecode": bytecode})
    if result:
        print(f"\n[SUCCESS] MRM-20 Token Deployed!")
        print(f"  Name:             {name}")
        print(f"  Symbol:           {symbol}")
        print(f"  Decimals:         {decimals}")
        print(f"  Initial Supply:   {supply}")
        print(f"  Owner:            {owner}")
        print(f"  Contract Address: {result}")
        return result
    else:
        print("[FAIL] Deployment failed.")
        return None

def main():
    parser = argparse.ArgumentParser(description="Mermai Token Management")
    subparsers = parser.add_subparsers(dest="command")

    deploy_p = subparsers.add_parser("deploy", help="Deploy new MRM-20 token")
    deploy_p.add_argument("--name", default="Mermai USD", help="Token Name")
    deploy_p.add_argument("--symbol", default="mUSD", help="Token Symbol")
    deploy_p.add_argument("--decimals", type=int, default=18, help="Token Decimals")
    deploy_p.add_argument("--supply", type=int, default=1000000, help="Initial Supply")
    deploy_p.add_argument("--owner", default="alice", help="Owner Address")
    deploy_p.add_argument("--rpc", default=RPC_URL, help="RPC Node URL")

    args = parser.parse_args()

    if args.command == "deploy":
        deploy_token(args.name, args.symbol, args.decimals, args.supply, args.owner)
    else:
        deploy_token("Mermai USD", "mUSD", 18, 1000000, "alice")

if __name__ == "__main__":
    main()
