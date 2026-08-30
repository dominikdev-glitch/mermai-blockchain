#!/usr/bin/env python3
"""
Mermai Multi-Sig Treasury Management Tool
Manages M-of-N threshold treasury accounts and transaction signing.
"""

import argparse
import hashlib
import json
import os
import sys

def create_treasury(threshold, signers_hex_list):
    print(f"Creating {threshold}-of-{len(signers_hex_list)} Multi-Signature Treasury Account...")
    
    payload = f"{threshold}:" + ",".join(signers_hex_list) + ","
    digest = hashlib.sha256(payload.encode("utf-8")).hexdigest()
    ms_address = "mrm_ms1" + digest[:40]

    treasury_info = {
        "address": ms_address,
        "threshold": threshold,
        "signers_count": len(signers_hex_list),
        "signers": signers_hex_list
    }

    out_file = f"treasury_{ms_address[:12]}.json"
    with open(out_file, "w") as f:
        json.dump(treasury_info, f, indent=2)

    print(f"\n[SUCCESS] Treasury Account Created!")
    print(f"  Address:   {ms_address}")
    print(f"  Threshold: {threshold} of {len(signers_hex_list)}")
    print(f"  Saved to:  {out_file}")
    return ms_address

def main():
    parser = argparse.ArgumentParser(description="Mermai Multi-Sig Treasury Manager")
    subparsers = parser.add_subparsers(dest="command")

    create_p = subparsers.add_parser("create", help="Create new Multi-Sig Treasury")
    create_p.add_argument("--threshold", type=int, default=2, help="Threshold signatures (M)")
    create_p.add_argument("--signers", nargs="+", help="Public keys of committee signers")

    args = parser.parse_args()

    if args.command == "create":
        signers = args.signers or ["0411111111111111", "0422222222222222", "0433333333333333"]
        create_treasury(args.threshold, signers)
    else:
        print("Demo: Creating 2-of-3 committee treasury...")
        create_treasury(2, ["0411111111111111", "0422222222222222", "0433333333333333"])

if __name__ == "__main__":
    main()
