#!/usr/bin/env python3
"""
MERMAI Official Python SDK
"""
import json
import urllib.request
import os

class MermaiClient:
    def __init__(self, rpc_url="http://localhost:7400"):
        self.rpc_url = rpc_url

    def call(self, method, params=None):
        payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}
        req = urllib.request.Request(
            self.rpc_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if "error" in data:
                raise RuntimeError(f"Mermai RPC Error: {data['error']}")
            return data.get("result")

    def get_block_number(self): return self.call("mrm_blockNumber")
    def get_balance(self, address): return self.call("mrm_getBalance", {"address": address})
    def suggest_fee(self): return self.call("mrm_suggestFee")
    def deploy_contract(self, bytecode_hex): return self.call("mrm_deployContract", {"bytecode": bytecode_hex})

class MermaiAccount:
    def __init__(self, address):
        self.address = address

    @classmethod
    def generate(cls):
        return cls(f"mrm1{os.urandom(20).hex()}")
