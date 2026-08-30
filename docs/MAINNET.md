# Mermai Mainnet Deployment Guide

## Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU       | 4 cores | 8+ cores    |
| RAM       | 8 GB    | 32 GB       |
| Disk      | 200 GB SSD | 1 TB NVMe |
| Network   | 100 Mbps | 1 Gbps    |
| OS        | Ubuntu 22.04 LTS | Ubuntu 22.04 LTS |

## Validator Requirements

- Minimum stake: **1,000,000 MRM**
- Lockup period: **1,000 blocks** (~14 days at 2 min/block)
- Keys: **ECDSA secp256r1 (P-256)** via OpenSSL

## Launch Sequence

```bash
# 1. Build production binary
cmake -B build-prod -DCMAKE_BUILD_TYPE=Release
cmake --build build-prod --config Release -j$(nproc)

# 2. Generate validator key
openssl ecparam -name prime256v1 -genkey -noout -out validator.pem
openssl ec -in validator.pem -pubout -out validator_pub.pem

# 3. Create genesis.json (or use the network-provided genesis)
cp scripts/testnet/mermai_genesis_testnet.json genesis.json

# 4. Start node
./build-prod/mermai_node \
  --port 6400 \
  --db mainnet.db \
  --genesis genesis.json \
  --rpc-port 7400

# 5. Register as validator via RPC
curl -s -X POST http://localhost:7400 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"mrm_registerValidator",
       "params":{"address":"YOUR_ADDRESS","stake":1000000,"public_key":"HEX_PUBKEY"}}'
```

## Security Hardening

- Run node as non-root user (`mermai` system account)
- Use firewall rules to restrict RPC port (7400) to trusted IPs
- Enable full disk encryption
- Set up automated backups every 6 hours:
  ```bash
  python3 scripts/mermai_backup.py --db mainnet.db --dest /mnt/backup/
  ```

## Monitoring

The node exposes Prometheus-compatible metrics at `mrm_getMetrics` RPC:
```bash
curl -s -X POST http://localhost:7400 \
  -d '{"jsonrpc":"2.0","id":1,"method":"mrm_getMetrics","params":{}}'
```

Metrics exposed:
- `mermai_block_height` — current chain tip
- `mermai_validators_active` — number of active validators
- `mermai_peers_connected` — P2P peer count
- `mermai_mempool_size` — pending transactions

## Slashing Conditions

| Event | Penalty |
|-------|---------|
| Equivocation (double-proposal) | 50% stake burn |
| Inactivity > 100 missed slots  | 10% stake decay per epoch |
| 200 missed slots               | Ejection from active set |

## Emergency Recovery

```bash
python3 scripts/mermai_recovery.py --db mainnet.db --height <KNOWN_GOOD_HEIGHT>
```
