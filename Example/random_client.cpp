#include <immintrin.h>  // For _rdseed64_step
#include <openssl/sha.h>

class RandomContractClient {
private:
    static const char* RANDOM_CONTRACT_ID = "DAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; // Your contract ID
    static const uint32 REVEAL_AND_COMMIT_PROCEDURE = 1;
    static const uint32 GET_RANDOM_BYTES_FUNCTION = 1;
    static const uint32 GET_CONTRACT_INFO_FUNCTION = 2;
    static const uint32 GET_USER_COMMITMENTS_FUNCTION = 3;

    std::map<id, bit_4096> pendingSecrets; // Store secrets until reveal time

public:
    // Generate 4096 bits of entropy using hardware RNG
    bit_4096 generateEntropy() {
        bit_4096 entropy;
        
        for (int i = 0; i < 64; i++) {
            uint64_t value;
            
            // Try hardware RNG first
            if (_rdseed64_step(&value) == 1) {
                entropy.data[i] = value;
            } else {
                // Fallback to other entropy sources
                entropy.data[i] = getRandomFromOtherSource();
            }
        }
        
        return entropy;
    }

    // Create commitment digest (SHA-256 hash)
    id createCommitmentDigest(const bit_4096& entropy) {
        id digest;
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, &entropy, sizeof(entropy));
        SHA256_Final(digest.bytes, &sha256);
        return digest;
    }

    // Initial commit (first time participating)
    TransactionResult makeInitialCommit(uint64 securityDeposit) {
        // Generate entropy and create commitment
        bit_4096 entropy = generateEntropy();
        id commitmentDigest = createCommitmentDigest(entropy);
        
        // Store the secret for later reveal
        pendingSecrets[commitmentDigest] = entropy;
        
        // Create transaction input
        RevealAndCommit_input input;
        memset(&input.revealedBits, 0, sizeof(input.revealedBits)); // Zero for initial commit
        input.committedDigest = commitmentDigest;
        
        // Send transaction
        return sendTransaction(input, securityDeposit);
    }

    // Reveal previous commitment and make new commitment
    TransactionResult revealAndCommit(const id& previousDigest, uint64 securityDeposit) {
        // Find the secret for the previous commitment
        auto it = pendingSecrets.find(previousDigest);
        if (it == pendingSecrets.end()) {
            throw std::runtime_error("No secret found for this commitment");
        }
        
        bit_4096 previousSecret = it->second;
        
        // Generate new entropy
        bit_4096 newEntropy = generateEntropy();
        id newDigest = createCommitmentDigest(newEntropy);
        
        // Store new secret
        pendingSecrets[newDigest] = newEntropy;
        
        // Remove old secret
        pendingSecrets.erase(it);
        
        // Create transaction input
        RevealAndCommit_input input;
        input.revealedBits = previousSecret;  // Reveal previous
        input.committedDigest = newDigest;     // Commit new
        
        return sendTransaction(input, securityDeposit);
    }

    // Final reveal (no new commitment)
    TransactionResult finalReveal(const id& digestToReveal) {
        auto it = pendingSecrets.find(digestToReveal);
        if (it == pendingSecrets.end()) {
            throw std::runtime_error("No secret found for this commitment");
        }
        
        RevealAndCommit_input input;
        input.revealedBits = it->second;
        memset(&input.committedDigest, 0, sizeof(input.committedDigest)); // No new commitment
        
        pendingSecrets.erase(it);
        
        return sendTransaction(input, 0); // No new security deposit
    }

private:
    struct TransactionResult {
        bool success;
        std::string transactionId;
        std::string errorMessage;
    };

    TransactionResult sendTransaction(const RevealAndCommit_input& input, uint64 amount) {
        // Create Qubic transaction
        Transaction tx;
        tx.sourcePublicKey = getMyPublicKey();
        tx.destinationPublicKey = parsePublicKey(RANDOM_CONTRACT_ID);
        tx.amount = amount;
        tx.tick = getCurrentTick() + 10; // Execute in 10 ticks
        tx.inputType = REVEAL_AND_COMMIT_PROCEDURE;
        tx.inputSize = sizeof(RevealAndCommit_input);
        
        // Serialize input
        memcpy(tx.input, &input, sizeof(input));
        
        // Sign and broadcast
        signTransaction(tx);
        return broadcastTransaction(tx);
    }
};