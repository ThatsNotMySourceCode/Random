#include <immintrin.h>  // For _rdseed64_step
#include <openssl/sha.h>
#include <iostream>
#include <map>
#include <queue>

class RandomContractClient {
private:
    static const char* RANDOM_CONTRACT_ID = "DAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    static const uint32 REVEAL_AND_COMMIT_PROCEDURE = 1;
    static const uint32 BUY_ENTROPY_PROCEDURE = 2;
    static const uint32 GET_RANDOM_BYTES_FUNCTION = 1;
    static const uint32 GET_CONTRACT_INFO_FUNCTION = 2;
    static const uint32 GET_MINER_INFO_FUNCTION = 3;

    struct MiningState {
        bit_4096 entropy;
        id digest;
        uint32 commitTick;
        uint32 cycle; // 0, 1, or 2 for (3N, 3N+1, 3N+2)
    };
    
    std::queue<MiningState> pendingReveals;
    uint32 currentCycle;

public:
    RandomContractClient() : currentCycle(0) {}

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

    // Start mining on a specific cycle (3N, 3N+1, or 3N+2)
    TransactionResult startMining(uint64 securityDeposit, uint32 targetCycle) {
        uint32 currentTick = getCurrentTick();
        
        // Wait for the right cycle if needed
        while (currentTick % 3 != targetCycle) {
            waitForTicks(1);
            currentTick = getCurrentTick();
        }
        
        // Generate entropy and create commitment
        bit_4096 entropy = generateEntropy();
        id commitmentDigest = createCommitmentDigest(entropy);
        
        // Store for reveal in 3 ticks
        MiningState state;
        state.entropy = entropy;
        state.digest = commitmentDigest;
        state.commitTick = currentTick;
        state.cycle = targetCycle;
        pendingReveals.push(state);
        
        // Create transaction input (initial commit)
        RevealAndCommit_input input;
        memset(&input.revealedBits, 0, sizeof(input.revealedBits)); // No previous reveal
        input.committedDigest = commitmentDigest;
        
        std::cout << "Starting mining on cycle " << targetCycle 
                  << " at tick " << currentTick << std::endl;
        
        return sendTransaction(input, securityDeposit);
    }

    // Continue mining (reveal previous + commit new)
    TransactionResult continueMining(uint64 securityDeposit) {
        if (pendingReveals.empty()) {
            throw std::runtime_error("No pending reveals - call startMining first");
        }
        
        uint32 currentTick = getCurrentTick();
        MiningState oldState = pendingReveals.front();
        pendingReveals.pop();
        
        // Check if we're still within the 3-tick window
        if (currentTick > oldState.commitTick + 3) {
            std::cout << "Warning: Late reveal - may lose security deposit!" << std::endl;
        }
        
        // Wait for correct cycle (same as original)
        while (currentTick % 3 != oldState.cycle) {
            waitForTicks(1);
            currentTick = getCurrentTick();
        }
        
        // Generate new entropy
        bit_4096 newEntropy = generateEntropy();
        id newDigest = createCommitmentDigest(newEntropy);
        
        // Store new state for next reveal
        MiningState newState;
        newState.entropy = newEntropy;
        newState.digest = newDigest;
        newState.commitTick = currentTick;
        newState.cycle = oldState.cycle;
        pendingReveals.push(newState);
        
        // Create transaction input
        RevealAndCommit_input input;
        input.revealedBits = oldState.entropy;  // Reveal previous
        input.committedDigest = newDigest;       // Commit new
        
        std::cout << "Continue mining cycle " << oldState.cycle 
                  << " at tick " << currentTick << std::endl;
        
        return sendTransaction(input, securityDeposit);
    }

    // Stop mining (final reveal)
    TransactionResult stopMining() {
        if (pendingReveals.empty()) {
            throw std::runtime_error("No pending reveals to stop");
        }
        
        uint32 currentTick = getCurrentTick();
        MiningState finalState = pendingReveals.front();
        pendingReveals.pop();
        
        // Wait for correct cycle
        while (currentTick % 3 != finalState.cycle) {
            waitForTicks(1);
            currentTick = getCurrentTick();
        }
        
        // Create final reveal transaction
        RevealAndCommit_input input;
        input.revealedBits = finalState.entropy;    // Reveal last commitment
        memset(&input.committedDigest, 0, sizeof(input.committedDigest)); // No new commit
        
        std::cout << "Stop mining cycle " << finalState.cycle 
                  << " at tick " << currentTick << std::endl;
        
        return sendTransaction(input, 0); // No new security deposit
    }

    // Buy entropy (paid service)
    TransactionResult buyEntropy(uint32 numBytes, uint64 payment) {
        BuyEntropy_input input;
        input.numberOfBytes = numBytes;
        input.nonce = {}; // Could add custom nonce
        
        return sendBuyEntropyTransaction(input, payment);
    }

    // Get mining statistics
    MinerInfo getMinerInfo() {
        GetMinerInfo_input input;
        input.minerId = getMyPublicKey();
        
        auto response = makeContractQuery(
            RANDOM_CONTRACT_ID,
            GET_MINER_INFO_FUNCTION,
            &input,
            sizeof(input)
        );
        
        MinerInfo info;
        if (response.success) {
            GetMinerInfo_output output;
            memcpy(&output, response.data, sizeof(output));
            
            info.totalPendingRewards = output.totalPendingRewards;
            for (int i = 0; i < 3; i++) {
                info.cycles[i] = output.cycles[i];
            }
        }
        
        return info;
    }

private:
    struct TransactionResult {
        bool success;
        std::string transactionId;
        std::string errorMessage;
    };

    struct MinerInfo {
        struct CycleInfo {
            uint64 totalDeposit;
            uint32 successfulReveals;
            uint64 pendingRewards;
        } cycles[3];
        uint64 totalPendingRewards;
    };

    TransactionResult sendTransaction(const RevealAndCommit_input& input, uint64 amount) {
        // Create Qubic transaction
        Transaction tx;
        tx.sourcePublicKey = getMyPublicKey();
        tx.destinationPublicKey = parsePublicKey(RANDOM_CONTRACT_ID);
        tx.amount = amount;
        tx.tick = getCurrentTick() + 1; // Execute next tick
        tx.inputType = REVEAL_AND_COMMIT_PROCEDURE;
        tx.inputSize = sizeof(RevealAndCommit_input);
        
        // Serialize input
        memcpy(tx.input, &input, sizeof(input));
        
        // Sign and broadcast
        signTransaction(tx);
        return broadcastTransaction(tx);
    }

    TransactionResult sendBuyEntropyTransaction(const BuyEntropy_input& input, uint64 amount) {
        Transaction tx;
        tx.sourcePublicKey = getMyPublicKey();
        tx.destinationPublicKey = parsePublicKey(RANDOM_CONTRACT_ID);
        tx.amount = amount;
        tx.tick = getCurrentTick() + 1;
        tx.inputType = BUY_ENTROPY_PROCEDURE;
        tx.inputSize = sizeof(BuyEntropy_input);
        
        memcpy(tx.input, &input, sizeof(input));
        
        signTransaction(tx);
        return broadcastTransaction(tx);
    }

    uint64 getRandomFromOtherSource() {
        // Fallback entropy source
        return rand() ^ (rand() << 32);
    }
};
