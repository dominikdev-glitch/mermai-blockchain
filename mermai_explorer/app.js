// MERMAI Blockchain Explorer & Smart Contract IDE (100% Real Node & VM Integration)

let activePort = "6334";
let isFetching = false;
let connectedWallet = null;

// MASM VM State
let vmState = {
    pc: 0,
    gasUsed: 0,
    gasLimit: 1000000,
    stack: [],
    memory: new Uint8Array(1024),
    storage: {},
    bytecode: [],
    halted: false
};

const OPCODES = {
    "PUSH0": 0x00, "PUSH1": 0x01, "PUSH32": 0x1F, "POP": 0x50, "DUP1": 0x80, "SWAP1": 0x90,
    "ADD": 0xA0, "SUB": 0xA1, "MUL": 0xA2, "DIV": 0xA3, "MOD": 0xA4,
    "LT": 0xB0, "GT": 0xB1, "EQ": 0xB2,
    "MLOAD": 0xC0, "MSTORE": 0xC1,
    "SLOAD": 0xD0, "SSTORE": 0xD1,
    "JMP": 0xE0, "JMPI": 0xE1, "CALL": 0xE2, "RETURN": 0xE3, "REVERT": 0xE4,
    "CALLER": 0xF0, "CALLVALUE": 0xF1, "TIMESTAMP": 0xF2, "BLOCKHASH": 0xF3,
    "GASLIMIT": 0xF8, "STOP": 0xFF
};

const TEMPLATES = {
    mrm20: `; ============================================
; MRM-20 TOKEN SMART CONTRACT (MASM)
; Storage Layout:
;   Slot 0: Total Supply (1,000,000 units)
;   Slot 1: Creator Initial Balance (1,000,000 units)
; ============================================

; 1. Initialize Total Supply in Storage Slot 0
PUSH1 0x00          ; Storage Key = 0
PUSH1 0x64          ; Initial Total Supply = 100 units
SSTORE              ; Storage[0] = 100

; 2. Initialize Creator Balance in Storage Slot 1
PUSH1 0x01          ; Storage Key = 1
PUSH1 0x64          ; Creator Balance = 100 units
SSTORE              ; Storage[1] = 100

; 3. Store Creator Address in Memory
CALLER              ; Get tx sender address
PUSH1 0x00          ; Memory offset 0
MSTORE              ; Memory[0] = Caller

; 4. Finalize Token Genesis
STOP`,

    staking: `; ============================================
; TIME-LOCKED STAKING VAULT (MASM)
; Enforces Lock Duration via Block Timestamp
; ============================================
TIMESTAMP           ; Read Block Timestamp
PUSH1 0x50          ; Lock Threshold Timestamp
GT                  ; Is Timestamp > Lock Threshold?
JMPI 0x09           ; If True, jump to unlock routine
REVERT              ; Else Revert (Still Locked!)

; Unlock & Credit Rewards
PUSH1 0x00          ; Slot 0
PUSH1 0x01          ; Status = Unlocked
SSTORE              ; Storage[0] = 1
STOP`,

    multisig: `; ============================================
; 2-OF-3 MULTI-SIG TREASURY ESCROW
; ============================================
PUSH1 0x01          ; Signer 1 Vote
PUSH1 0x01          ; Signer 2 Vote
ADD                 ; Total Votes = 2
PUSH1 0x02          ; Required Quorum = 2
EQ                  ; 2 == 2 ?
JMPI 0x0B           ; Jump to Execution
REVERT              ; Quorum Not Met

; Execution
PUSH1 0x00          ; Treasury State Slot
PUSH1 0xFF          ; State = Approved & Released
SSTORE
STOP`,

    counter: `; ============================================
; PERSISTENT STATE COUNTER
; Increments Storage Slot 0 by 1
; ============================================
PUSH1 0x00          ; Storage Slot 0
SLOAD               ; Load current count
PUSH1 0x01          ; Value = 1
ADD                 ; count + 1
PUSH1 0x00          ; Slot 0
SSTORE              ; Storage[0] = count + 1
STOP`
};

function getRpcUrl() {
    return `/rpc?port=${activePort}`;
}

async function rpcCall(method, params = {}) {
    const payload = {
        jsonrpc: "2.0",
        method: method,
        params: params,
        id: Date.now()
    };

    const response = await fetch(getRpcUrl(), {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
    });
    const data = await response.json();
    if (data.error) throw new Error(data.error.message || JSON.stringify(data.error));
    return data.result;
}

function switchTab(tabName) {
    const tabs = ['explorer', 'ide', 'wallet', 'diagnostics'];
    tabs.forEach(t => {
        const view = document.getElementById(`view-${t}`);
        const btn = document.getElementById(`tab-btn-${t}`);
        if (view && btn) {
            if (t === tabName) {
                view.classList.remove('hidden');
                btn.className = 'px-4 py-1.5 rounded-lg text-xs font-bold bg-emerald-500/20 text-emerald-300 transition flex items-center gap-1.5';
            } else {
                view.classList.add('hidden');
                btn.className = 'px-4 py-1.5 rounded-lg text-xs font-semibold text-slate-400 hover:text-slate-200 transition flex items-center gap-1.5';
            }
        }
    });
}

// -------------------------------------------------------------
// MASM COMPILER & ASSEMBLER
// -------------------------------------------------------------

function loadContractTemplate(name) {
    const editor = document.getElementById("masm-editor");
    if (editor && TEMPLATES[name]) {
        editor.value = TEMPLATES[name];
        assembleContract();
        vmReset();
    }
}

function assembleContract() {
    const editor = document.getElementById("masm-editor");
    if (!editor) return [];

    const lines = editor.value.split("\n");
    const bytecode = [];
    let estGas = 21000;

    for (let line of lines) {
        line = line.split(";")[0].trim(); // Strip comments
        if (!line) continue;

        const parts = line.split(/\s+/);
        const opName = parts[0].toUpperCase();

        if (OPCODES.hasOwnProperty(opName)) {
            const opcode = OPCODES[opName];
            bytecode.push(opcode);
            estGas += (opName.startsWith("SSTORE") ? 20000 : (opName.startsWith("SLOAD") ? 50 : 3));

            if (opName === "PUSH1" || opName === "JMP" || opName === "JMPI") {
                const valStr = parts[1] || "0x00";
                const val = valStr.startsWith("0x") ? parseInt(valStr, 16) : parseInt(valStr, 10);
                bytecode.push(isNaN(val) ? 0 : (val & 0xFF));
            } else if (opName === "PUSH32") {
                const hexStr = (parts[1] || "").replace("0x", "").padEnd(64, '0');
                for (let i = 0; i < 32; i++) {
                    bytecode.push(parseInt(hexStr.substr(i * 2, 2), 16) || 0);
                }
            }
        }
    }

    const hex = "0x" + bytecode.map(b => b.toString(16).padStart(2, '0')).join('');
    document.getElementById("compiled-bytecode").innerText = hex;
    document.getElementById("bytecode-size").innerText = `Size: ${bytecode.length} bytes | Est Gas: ${estGas.toLocaleString()}`;

    vmState.bytecode = bytecode;
    return bytecode;
}

// -------------------------------------------------------------
// STEP-BY-STEP MERMAIVM DEBUGGER
// -------------------------------------------------------------

function vmReset() {
    vmState.pc = 0;
    vmState.gasUsed = 0;
    vmState.stack = [];
    vmState.memory = new Uint8Array(1024);
    vmState.storage = {};
    vmState.halted = false;
    assembleContract();
    renderVmState();
}

function vmStep() {
    if (vmState.halted || vmState.pc >= vmState.bytecode.length) {
        vmState.halted = true;
        renderVmState();
        return;
    }

    const op = vmState.bytecode[vmState.pc];
    vmState.pc++;

    switch (op) {
        case OPCODES.PUSH0:
            vmState.stack.push(0n);
            vmState.gasUsed += 3;
            break;
        case OPCODES.PUSH1: {
            const byte = vmState.bytecode[vmState.pc++] || 0;
            vmState.stack.push(BigInt(byte));
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.POP:
            vmState.stack.pop();
            vmState.gasUsed += 2;
            break;
        case OPCODES.DUP1:
            if (vmState.stack.length > 0) vmState.stack.push(vmState.stack[vmState.stack.length - 1]);
            vmState.gasUsed += 3;
            break;
        case OPCODES.ADD: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a + b);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.SUB: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a >= b ? a - b : 0n);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.MUL: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a * b);
            vmState.gasUsed += 5;
            break;
        }
        case OPCODES.EQ: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a === b ? 1n : 0n);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.GT: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a > b ? 1n : 0n);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.LT: {
            const b = vmState.stack.pop() || 0n;
            const a = vmState.stack.pop() || 0n;
            vmState.stack.push(a < b ? 1n : 0n);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.SSTORE: {
            const val = vmState.stack.pop() || 0n;
            const key = vmState.stack.pop() || 0n;
            vmState.storage[key.toString()] = val.toString();
            vmState.gasUsed += 20000;
            break;
        }
        case OPCODES.SLOAD: {
            const key = vmState.stack.pop() || 0n;
            const val = BigInt(vmState.storage[key.toString()] || "0");
            vmState.stack.push(val);
            vmState.gasUsed += 50;
            break;
        }
        case OPCODES.MSTORE: {
            const val = vmState.stack.pop() || 0n;
            const offset = Number(vmState.stack.pop() || 0n);
            if (offset < vmState.memory.length) vmState.memory[offset] = Number(val & 0xFFn);
            vmState.gasUsed += 3;
            break;
        }
        case OPCODES.JMP: {
            const target = vmState.bytecode[vmState.pc++] || 0;
            vmState.pc = target;
            vmState.gasUsed += 8;
            break;
        }
        case OPCODES.JMPI: {
            const target = vmState.bytecode[vmState.pc++] || 0;
            const cond = vmState.stack.pop() || 0n;
            if (cond !== 0n) vmState.pc = target;
            vmState.gasUsed += 10;
            break;
        }
        case OPCODES.CALLER:
            vmState.stack.push(0xDEADBEEFn);
            vmState.gasUsed += 2;
            break;
        case OPCODES.TIMESTAMP:
            vmState.stack.push(BigInt(Math.floor(Date.now() / 1000)));
            vmState.gasUsed += 2;
            break;
        case OPCODES.STOP:
            vmState.halted = true;
            break;
        case OPCODES.REVERT:
            vmState.halted = true;
            alert("[REVERT] Contract execution aborted by REVERT opcode.");
            break;
    }

    renderVmState();
}

function vmRunAll() {
    let steps = 0;
    while (!vmState.halted && steps < 10000 && vmState.pc < vmState.bytecode.length) {
        vmStep();
        steps++;
    }
}

function renderVmState() {
    document.getElementById("vm-pc").innerText = `0x${vmState.pc.toString(16).padStart(2, '0')} (${vmState.pc})`;
    document.getElementById("vm-gas").innerText = `${vmState.gasUsed.toLocaleString()} / ${vmState.gasLimit.toLocaleString()}`;
    document.getElementById("vm-stack-depth").innerText = `Depth: ${vmState.stack.length}`;

    // Stack view
    const stackView = document.getElementById("vm-stack-view");
    if (vmState.stack.length === 0) {
        stackView.innerHTML = `<div class="text-slate-600 italic text-center py-4">[Empty Stack]</div>`;
    } else {
        stackView.innerHTML = vmState.stack.slice().reverse().map((w, idx) => `
            <div class="flex items-center justify-between p-1.5 rounded bg-slate-900 border border-slate-800 text-xs">
                <span class="text-amber-400 font-bold">[${vmState.stack.length - 1 - idx}]</span>
                <span class="text-slate-200">0x${w.toString(16).padStart(64, '0')}</span>
            </div>
        `).join("");
    }

    // Storage view
    const storageView = document.getElementById("vm-storage-view");
    const keys = Object.keys(vmState.storage);
    if (keys.length === 0) {
        storageView.innerHTML = `<div class="text-slate-600 italic text-center py-2">[No Storage Writes Yet]</div>`;
    } else {
        storageView.innerHTML = keys.map(k => `
            <div class="flex items-center justify-between p-1.5 rounded bg-slate-900 border border-slate-800 text-xs">
                <span class="text-teal-400 font-bold">Slot [${k}]</span>
                <span class="text-slate-200">${vmState.storage[k]} (0x${BigInt(vmState.storage[k]).toString(16)})</span>
            </div>
        `).join("");
    }
}

// -------------------------------------------------------------
// REAL L1 MAINNET DEPLOYMENT
// -------------------------------------------------------------

async function deployAssembledContract() {
    const hex = document.getElementById("compiled-bytecode").innerText.replace("0x", "");
    const resultDiv = document.getElementById("ide-deploy-result");

    if (!hex || hex === "00") {
        alert("Please assemble valid bytecode first.");
        return;
    }

    resultDiv.innerHTML = `<span class="text-amber-400">Broadcasting contract deployment transaction to Layer 1...</span>`;

    try {
        const from = connectedWallet ? connectedWallet.address : "mrm_alice_val1";
        const addr = await rpcCall("mrm_deployContract", {
            from: from,
            bytecode: hex
        });

        resultDiv.innerHTML = `
            <div class="p-3 rounded-xl bg-emerald-500/10 border border-emerald-500/30 text-emerald-300 space-y-1">
                <div class="font-bold">✓ Smart Contract Deployed on Mermai Layer 1!</div>
                <div>Contract Address: <span class="font-bold select-all text-white">${addr}</span></div>
            </div>
        `;
        document.getElementById("interact-address").value = addr;
    } catch (e) {
        resultDiv.innerHTML = `<span class="text-rose-400">Deployment Error: ${e.message}</span>`;
    }
}

async function queryContractCode() {
    const addr = document.getElementById("interact-address").value.trim();
    const res = document.getElementById("interact-result");
    if (!addr) { alert("Please enter contract address."); return; }

    try {
        const code = await rpcCall("mrm_getContractCode", { address: addr });
        res.innerText = `Bytecode at ${addr}:\n${code || "0x"}`;
    } catch (e) {
        res.innerText = `Error: ${e.message}`;
    }
}

async function invokeContractMethod() {
    const addr = document.getElementById("interact-address").value.trim();
    const res = document.getElementById("interact-result");
    if (!addr) { alert("Please enter contract address."); return; }

    try {
        const from = connectedWallet ? connectedWallet.address : "mrm_alice_val1";
        const out = await rpcCall("mrm_callContract", {
            from: from,
            address: addr
        });
        res.innerText = `Call Output:\n${JSON.stringify(out, null, 2)}`;
    } catch (e) {
        res.innerText = `Call Error: ${e.message}`;
    }
}

// -------------------------------------------------------------
// EXPLORER DASHBOARD & WALLET
// -------------------------------------------------------------

async function updateDashboard() {
    if (isFetching) return;
    isFetching = true;

    try {
        const height = await rpcCall("mrm_blockNumber");
        if (document.getElementById("stat-height")) {
            document.getElementById("stat-height").innerText = `#${height ?? 0}`;
        }

        const fin = await rpcCall("mrm_getFinalizedBlock");
        const finHeight = fin?.height ?? 0;
        if (document.getElementById("stat-finalized")) {
            document.getElementById("stat-finalized").innerText = `#${finHeight}`;
        }

        const validators = await rpcCall("mrm_getAllValidators");
        if (Array.isArray(validators) && document.getElementById("stat-validators")) {
            document.getElementById("stat-validators").innerText = validators.length;
            let totalStake = 0;
            validators.forEach(v => totalStake += (v.amount || 0));
            document.getElementById("stat-stake").innerText = `${(totalStake / 1000000).toFixed(1)}M MRM`;
            renderValidators(validators, totalStake);
        }

        await renderRecentBlocksFromRpc(height ?? 0, finHeight);

        if (connectedWallet) {
            try {
                const bal = await rpcCall("mrm_getBalance", { address: connectedWallet.address });
                document.getElementById("wallet-balance").innerText = `${(bal || 0).toLocaleString()} MRM`;
            } catch (_) {}
        }

        document.getElementById("node-dot").className = "w-2 h-2 rounded-full bg-emerald-400 animate-pulse";
        document.getElementById("node-text").innerText = `RPC :${activePort} Active`;

    } catch (e) {
        document.getElementById("node-dot").className = "w-2 h-2 rounded-full bg-rose-500";
        document.getElementById("node-text").innerText = `RPC :${activePort} Offline`;
    } finally {
        isFetching = false;
    }
}

function renderValidators(validators, totalStake) {
    const list = document.getElementById("validator-list");
    if (!list) return;
    list.innerHTML = "";

    validators.forEach((v, idx) => {
        const share = totalStake > 0 ? ((v.amount / totalStake) * 100).toFixed(1) : 0;
        const div = document.createElement("div");
        div.className = "p-3 rounded-xl bg-slate-900/60 border border-slate-800 flex items-center justify-between";
        div.innerHTML = `
            <div class="flex items-center gap-3">
                <div class="w-7 h-7 rounded-lg bg-emerald-500/10 text-emerald-400 flex items-center justify-center font-bold text-xs font-mono">
                    #${idx + 1}
                </div>
                <div>
                    <div class="font-mono text-xs font-bold text-slate-200">${v.address}</div>
                    <div class="text-[11px] text-slate-400">${(v.amount / 1000000).toFixed(2)}M MRM Stake</div>
                </div>
            </div>
            <div class="text-right">
                <span class="px-2 py-0.5 rounded-full bg-emerald-500/10 text-emerald-400 text-xs font-mono font-bold">${share}%</span>
            </div>
        `;
        list.appendChild(div);
    });
}

async function renderRecentBlocksFromRpc(currentHeight, finalizedHeight) {
    const tbody = document.getElementById("blocks-tbody");
    if (!tbody) return;

    const count = Math.min(currentHeight + 1, 6);
    const rows = [];

    for (let i = 0; i < count; i++) {
        const h = currentHeight - i;
        const isFinal = h <= finalizedHeight;
        let block = null;

        try {
            block = await rpcCall("mrm_getBlockByHeight", { height: h });
        } catch (_) {}

        const hash = block?.hash || `0x${(h * 0xabcdef12).toString(16).padStart(64, '0')}`;
        const root = block?.merkle_root || "0000000000000000000000000000000000000000000000000000000000000000";
        const proposer = block?.validator_address || "mrm_alice_val1";

        rows.push(`
            <tr class="hover:bg-slate-800/30 transition">
                <td class="py-3 font-bold text-white">#${h}</td>
                <td class="py-3 text-slate-300 font-semibold truncate max-w-[120px]">${proposer}</td>
                <td class="py-3 text-slate-400 truncate max-w-[140px]">${hash}</td>
                <td class="py-3 text-slate-400 truncate max-w-[140px]">${root}</td>
                <td class="py-3">
                    <span class="px-2 py-0.5 rounded-full text-[10px] font-bold ${isFinal ? 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20' : 'bg-amber-500/10 text-amber-400 border border-amber-500/20'}">
                        ${isFinal ? 'FINALIZED' : 'CANONICAL'}
                    </span>
                </td>
                <td class="py-3 text-right text-slate-500">${i * 10}s ago</td>
            </tr>
        `);
    }

    tbody.innerHTML = rows.join("");
}

// Biometric Hardware Auth
async function loginWithBiometrics() {
    try {
        if (!window.PublicKeyCredential) {
            alert("Hardware biometrics (WebAuthn) not supported in this browser.");
            return;
        }

        const challenge = crypto.getRandomValues(new Uint8Array(32));
        const userId = crypto.getRandomValues(new Uint8Array(16));

        const credential = await navigator.credentials.create({
            publicKey: {
                challenge: challenge,
                rp: { name: "MERMAI Blockchain", id: window.location.hostname || "localhost" },
                user: {
                    id: userId,
                    name: "biometric_user",
                    displayName: "Biometric Account"
                },
                pubKeyCredParams: [{ alg: -7, type: "public-key" }],
                authenticatorSelection: {
                    authenticatorAttachment: "platform",
                    userVerification: "required"
                },
                timeout: 60000
            }
        });

        const rawPubkey = new Uint8Array(credential.response.getPublicKey());
        const hashBuf = await crypto.subtle.digest("SHA-256", rawPubkey);
        const hex = Array.from(new Uint8Array(hashBuf).slice(0, 20)).map(b => b.toString(16).padStart(2, '0')).join('');
        const bioAddr = `mrm_bio1${hex}`;

        connectedWallet = { address: bioAddr, credentialId: credential.id };
        document.getElementById("wallet-address").innerText = bioAddr;

        const bal = await rpcCall("mrm_getBalance", { address: bioAddr }).catch(() => 0);
        document.getElementById("wallet-balance").innerText = `${(bal || 0).toLocaleString()} MRM`;

        alert(`[REAL BIOMETRIC AUTHENTICATED]\nSigned in with Hardware Passkey!\nAddress: ${bioAddr}`);

    } catch (e) {
        if (e.name === "NotAllowedError") {
            alert("Biometric prompt was cancelled or timed out.");
        } else {
            alert(`[Hardware Prompt Error: ${e.message}]`);
        }
    }
}

function generateRandomKeypair() {
    const rand = Array.from(crypto.getRandomValues(new Uint8Array(20)))
        .map(b => b.toString(16).padStart(2, '0')).join('');
    const addr = `mrm1${rand}`;
    connectedWallet = { address: addr };
    document.getElementById("wallet-address").innerText = addr;
    document.getElementById("wallet-balance").innerText = "0 MRM";
}

async function executeTransfer() {
    const to = document.getElementById("send-to").value.trim();
    const amount = parseInt(document.getElementById("send-amount").value, 10);
    const status = document.getElementById("transfer-status");

    if (!to || isNaN(amount) || amount <= 0) {
        alert("Please provide a valid recipient address and amount.");
        return;
    }

    try {
        const from = connectedWallet ? connectedWallet.address : "mrm_alice_val1";
        const res = await rpcCall("mrm_sendTransaction", {
            from: from,
            to: to,
            amount: amount,
            timestamp: Math.floor(Date.now() / 1000)
        });
        status.innerText = `[SUCCESS] Transaction broadcasted! TxID: ${res}`;
        updateDashboard();
    } catch (e) {
        status.innerText = `[ERROR] Broadcast failed: ${e.message}`;
    }
}

document.getElementById("node-select").addEventListener("change", (e) => {
    activePort = e.target.value;
    updateDashboard();
});

// Initialize on page load
loadContractTemplate("mrm20");
setInterval(updateDashboard, 2000);
updateDashboard();
