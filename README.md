# MERMAI BLOCKCHAIN NETWORK

**A High-Performance Layer-1 Blockchain with Time-Weighted Proof-of-Stake Consensus**

> **9 months of independent development** — A complete, production-grade blockchain built from scratch in modern C++20. Not a fork, not a tutorial project — a real Layer-1 with consensus, P2P networking, smart contracts, biometric hardware auth, and autonomous self-aware diagnostics.

---

## About This Project

I built MERMAI from scratch as a full-stack blockchain — every layer from the cryptographic primitives to the Web3 frontend is hand-written. The core design philosophy is simple: reward long-term validator commitment through Time-Weighted Proof-of-Stake (TW-PoS) and deliver deterministic finality via 2/3+ quorum attestation.

The blockchain supports hardware biometric authentication (FaceID / TouchID / Passkey) natively through NIST P-256 curve support, runs autonomous self-aware health diagnostics on every node, ships with official JavaScript and Python developer SDKs, and includes a full Web3 explorer with token deployment studio.

Everything compiles, runs across multiple nodes, and passes 26 comprehensive unit and integration test suites at 100%.

For the full architectural breakdown, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## Creator

**[@dominikdev-glitch](https://github.com/dominikdev-glitch)** — 9 months of design, engineering, and testing.

---

## Core Features

### Consensus & Finality
- **Time-Weighted Proof-of-Stake (TW-PoS)** — Deterministic block proposer selection weighted by validator stake amount and tenure duration
- **Quorum Finality Attestations** — 2/3+ stake voting engine providing non-revertible finality on every block
- **Equivocation Slashing** — Automatic penalty for double-proposal at the same height/slot

### Cryptography & Security
- **Hardware Biometric Authentication** — Native NIST P-256 (`secp256r1`) support for FaceID, TouchID, Android Biometrics, and FIDO2 Passkey signatures. Users get seedless `mrm_bio1...` accounts with zero private key storage in the browser
- **SHA-256 Merkle State Trees** — RFC 6962 compliant tree computation and inclusion proofs for trustless light-client verification
- **Multi-Signature (M-of-N) Treasuries** — Native threshold signatures with replay protection for committee-governed treasuries
- **ECDSA Transaction Signing** — Full DER-encoded signature verification with public key recovery

### Smart Contracts & Tokens
- **MermaiVM** — Stack-based bytecode execution engine with gas metering, persistent key-value storage, and deterministic execution (no floating point)
- **MRM-20 Token Standard** — ERC-20 equivalent implementation with minting, transfers, allowances, and burn support

### Networking
- **Full P2P Mesh Network** — Peer discovery, gossip-based message propagation, rate limiting, and subnet diversity for eclipse attack defense
- **JSON-RPC 2.0 API** — Ethereum-compatible endpoint set plus Mermai-specific methods for validators, governance, and diagnostics
- **WebSocket PubSub Engine** — Real-time event subscriptions (`newHeads`, `logs`, `pendingTransactions`)

### Node Intelligence
- **Autonomous Health Diagnostics** — Continuous runtime introspection with a 0-100 health score, block propagation delay analysis, and network partition detection
- **Adaptive Fee Recommendations** — Gas price multiplier that automatically adjusts based on real-time mempool congestion
- **Peer Anomaly Isolation** — Auto-detection and quarantine of misbehaving or stale peers

### Developer Tools
- **JavaScript / TypeScript SDK** (`sdk/js/mermai.js`) — Full client library with `MermaiClient`, `MermaiWallet`, and WebAuthn biometric passkey helper. Works in browser and Node.js
- **Python SDK** (`sdk/python/mermai.py`) — Complete Python 3 client with `MermaiClient`, `MermaiAccount`, and contract deployment helpers
- **CLI Wallet** (`mermai-cli`) — Command-line tool for key generation, balance queries, staking, and transaction signing
- **MermaiVM Smart Contract IDE & Bytecode Studio** — Full in-browser MASM (Mermai Assembly) IDE with live assembler, step-by-step stack/storage debugger, gas estimator, contract invoker, and direct Layer 1 deployment
- **Web3 Explorer & Biometric Suite** — Interactive block explorer, WebAuthn Passkey biometric wallet, and self-aware diagnostics dashboard
- **Light Client (SPV)** — Header-only synchronization and cryptographic payment verification for resource-constrained devices


---

## Quick Start

### 1. Build the Binaries

```bash
# Dependencies: CMake 3.20+, C++20 compiler (GCC/Clang/MSVC), OpenSSL 3.x, SQLite3
cmake -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release -j$(nproc)
```

### 2. Run the Test Suite

```bash
ctest --test-dir build-cmake --output-on-failure
# Expected: 26/26 tests passed (100%)
```

### 3. Launch the Multi-Node Network

```bash
# Launches 3 validator nodes (Alice, Bob, Carol) with seeded genesis state
python scripts/mermai_launch_network.py

# Or just initialize databases without launching nodes:
python scripts/mermai_launch_network.py --init-only
```

### 4. Start the Web3 Explorer & Biometric Studio

```bash
# Starts the explorer on http://localhost:8080
python mermai_explorer/mermai_serve.py
```

The explorer includes:
- Live block feed with Merkle root visualization
- Quorum attestation gauge
- 1-click biometric login (FaceID / TouchID / Windows Hello)
- MRM-20 token deployment studio
- Self-aware node diagnostics dashboard

### 5. Run via Docker Compose

```bash
docker compose up --build
```

---

## Developer SDKs

### JavaScript / TypeScript

```javascript
import { MermaiClient, MermaiWallet } from './sdk/js/mermai.js';

const client = new MermaiClient('http://localhost:6334');
const height = await client.getBlockNumber();
const wallet = MermaiWallet.generate();
console.log(`Address: ${wallet.address}`);
```

### Python

```python
from sdk.python.mermai import MermaiClient, MermaiAccount

client = MermaiClient('http://localhost:6334')
height = client.get_block_number()
account = MermaiAccount.generate()
print(f"Address: {account.address}")
```

---

## Repository Layout

```
include/mermai/          Core headers
  mermai_blockchain.hpp    Blockchain state machine, blocks, transactions
  mermai_consensus.hpp     TW-PoS consensus, proposal validation, slashing
  mermai_merkle.hpp        SHA-256 Merkle tree and inclusion proofs
  mermai_multisig.hpp      M-of-N multi-signature treasury
  mermai_token.hpp         MRM-20 token standard
  mermai_vm.hpp            MermaiVM bytecode engine
  mermai_biometric.hpp     WebAuthn / FIDO2 biometric signature engine
  mermai_self_aware.hpp    Autonomous node health diagnostics

src/core/                Blockchain core logic
src/consensus/           TW-PoS selection, slashing, governance
src/db/                  SQLite atomic state storage and UTXO tracking
src/network/             P2P mesh, peer reputation, rate limiter
src/rpc/                 JSON-RPC 2.0 server and WebSocket PubSub
src/vm/                  MermaiVM execution and MRM-20 runtime
src/cli/                 mermai-cli wallet

sdk/js/                  JavaScript / TypeScript SDK (mermai.js)
sdk/python/              Python SDK (mermai.py)

mermai_explorer/         Web3 Explorer, biometric wallet, token studio
scripts/                 Network launcher, token deployer, backup tools
tests/                   26 unit and integration test suites
docs/                    Architecture, build, deployment guides
```

---

## Test Suite (26/26 Passing)

| Module | Test | Coverage |
|--------|------|----------|
| Biometric WebAuthn | `mermai_biometric_test` | NIST P-256 signature verification, `mrm_bio1` address derivation |
| Self-Aware Engine | `mermai_self_aware_test` | Health scoring, congestion detection, adaptive fees |
| Block Validation | `mermai_block_validation_test` | Genesis, chaining, timestamp, merkle root validation |
| CLI Wallet | `mermai_cli_test` | Key generation, balance queries, transaction signing |
| Consensus | `mermai_consensus_test` | TW-PoS proposer selection, weight calculation |
| Database | `mermai_database_test` | UTXO round-trip, block persistence, account transactions |
| Finality | `mermai_finality_test` | 2/3+ quorum voting, finalization thresholds |
| Governance | `mermai_governance_test` | On-chain proposal submission and voting |
| HTTP RPC | `mermai_http_rpc_test` | JSON-RPC dispatch, contract deployment |
| Mempool | `mermai_mempool_test` | Transaction ordering, eviction, deduplication |
| Merkle Tree | `mermai_merkle_test` | Tree hash computation, inclusion proof verification |
| Multi-Node Sync | `mermai_multi_node_sync_test` | Cross-node block propagation |
| Multi-Sig | `mermai_multisig_test` | M-of-N threshold signing and verification |
| Network Security | `mermai_network_security_test` | Rate limiting, peer banning |
| P2P Network | `mermai_p2p_test` | Socket connections, message exchange |
| PubSub | `mermai_pubsub_test` | WebSocket event subscription |
| Quorum | `mermai_quorum_test` | Attestation collection and threshold check |
| Restart Recovery | `mermai_restart_test` | Crash recovery from persisted state |
| Staking Rewards | `mermai_rewards_test` | Block reward distribution |
| RPC Dispatcher | `mermai_rpc_test` | Method routing, contract deployment, balance queries |
| Signatures | `mermai_signature_test` | ECDSA sign/verify round-trip |
| Slashing | `mermai_slashing_test` | Equivocation detection and stake penalty |
| Snapshots | `mermai_snapshot_test` | State snapshot creation and restoration |
| SPV Light Client | `mermai_spv_test` | Header sync, payment verification |
| MRM-20 Token | `mermai_token_test` | Mint, transfer, approve, burn |
| MermaiVM | `mermai_vm_test` | Opcode execution, gas metering, storage |

---

## Documentation

- [Architecture Guide](docs/ARCHITECTURE.md) — Full system design and component interactions
- [Build Instructions](docs/BUILD.md) — Platform-specific build setup
- [Mainnet Deployment Guide](docs/MAINNET.md) — Production deployment procedures
- [Node Deployment Guide](docs/mermai_deployment.md) — Individual node configuration
- [Testing Guide](docs/mermai_testing.md) — Running and extending the test suite

---

## License

MERMAI is open-source software licensed under the **Apache License 2.0**. See [LICENSE](LICENSE) for details.
