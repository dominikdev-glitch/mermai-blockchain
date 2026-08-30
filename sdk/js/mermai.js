/**
 * MERMAI Official JavaScript / TypeScript SDK
 * High-Performance Client & WebAuthn Biometric Passkey Engine
 */

class MermaiClient {
    constructor(rpcUrl = "http://localhost:6334") {
        this.rpcUrl = rpcUrl;
    }

    async call(method, params = {}) {
        const payload = { jsonrpc: "2.0", id: Date.now(), method, params };
        const res = await fetch(this.rpcUrl, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        if (data.error) throw new Error(data.error.message || JSON.stringify(data.error));
        return data.result;
    }

    async getBlockNumber() { return await this.call("mrm_blockNumber"); }
    async getFinalizedBlock() { return await this.call("mrm_getFinalizedBlock"); }
    async getBlockByHeight(height) { return await this.call("mrm_getBlockByHeight", { height }); }
    async getBlockByHash(hash) { return await this.call("mrm_getBlockByHash", { hash }); }
    async getBalance(address) { return await this.call("mrm_getBalance", { address }); }
    async getAllValidators() { return await this.call("mrm_getAllValidators"); }
    async suggestFee() { return await this.call("mrm_suggestFee"); }
    async getMetrics() { return await this.call("mrm_getMetrics"); }
    async getQuorumStatus() { return await this.call("mrm_getQuorumStatus"); }
    async deployContract(bytecodeHex) { return await this.call("mrm_deployContract", { bytecode: bytecodeHex }); }
    async sendTransaction(txData) { return await this.call("mrm_sendTransaction", txData); }
}

class MermaiWallet {
    constructor(address, publicKeyHex = "") {
        this.address = address;
        this.publicKeyHex = publicKeyHex;
    }

    static async fromHardwarePasskey(rpName = "MERMAI Network", userName = "mermai_user") {
        if (typeof window === 'undefined' || !navigator.credentials) {
            throw new Error("WebAuthn hardware credentials not supported in this environment");
        }

        const challenge = crypto.getRandomValues(new Uint8Array(32));
        const userId = crypto.getRandomValues(new Uint8Array(16));

        const credential = await navigator.credentials.create({
            publicKey: {
                challenge: challenge,
                rp: { name: rpName, id: window.location.hostname || "localhost" },
                user: {
                    id: userId,
                    name: userName,
                    displayName: userName
                },
                pubKeyCredParams: [
                    { alg: -7, type: "public-key" } // ES256 (NIST P-256 / secp256r1)
                ],
                authenticatorSelection: {
                    authenticatorAttachment: "platform", // FaceID / TouchID / Windows Hello
                    userVerification: "required",
                    residentKey: "preferred"
                },
                timeout: 60000,
                attestation: "direct"
            }
        });

        const rawPubkey = new Uint8Array(credential.response.getPublicKey());
        const pubkeyHash = new Uint8Array(await crypto.subtle.digest("SHA-256", rawPubkey));
        const hex = Array.from(pubkeyHash.slice(0, 20)).map(b => b.toString(16).padStart(2, '0')).join('');
        const address = `mrm_bio1${hex}`;

        const wallet = new MermaiWallet(address, Array.from(rawPubkey).map(b => b.toString(16).padStart(2, '0')).join(''));
        wallet.credentialId = credential.id;
        return wallet;
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { MermaiClient, MermaiWallet };
}
