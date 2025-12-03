#pragma once

using namespace QPI;

struct RANDOM_CONTRACT_STATE
{
    uint64 contractFeeReserve;
    
    // Global entropy pool - combines all revealed entropy
    m256i currentEntropyPool;
    uint64 entropyPoolVersion;
    
    // Tracking statistics
    uint64 totalCommits;
    uint64 totalReveals;
    uint64 totalSecurityDepositsLocked;
    
    // Contract configuration
    uint64 minimumSecurityDeposit;
    uint32 revealTimeoutBlocks;
    uint64 contractFeePercentage; // In basis points (e.g., 100 = 1%)
    
    // Valid deposit amounts (powers of 10)
    uint64 validDepositAmounts[16]; // 1, 10, 100, ..., 1,000,000,000,000,000
    
    // Commitment tracking
    struct EntropyCommitment {
        id digest;
        id invocatorId;
        uint64 amount;
        uint32 commitTick;
        uint32 revealDeadlineTick;
        bool hasRevealed;
    } commitments[1024];
    
    uint32 commitmentCount;
};

struct RANDOM : public ContractBase
{
public:
    // PROCEDURES (Write Operations)
    struct RevealAndCommit_input
    {
        bit_4096 revealedBits;
        id committedDigest;
    };
    struct RevealAndCommit_output
    {
    };

    // FUNCTIONS (Read-Only Operations)
    struct GetRandomBytes_input
    {
        uint32 numberOfBytes; // Max 32
        m256i nonce;         // Optional nonce for additional entropy
    };
    struct GetRandomBytes_output
    {
        uint8 randomBytes[32];
        uint64 entropyVersion;
    };

    struct GetContractInfo_input
    {
    };
    struct GetContractInfo_output
    {
        uint64 totalCommits;
        uint64 totalReveals;
        uint64 totalSecurityDepositsLocked;
        uint64 minimumSecurityDeposit;
        uint32 revealTimeoutBlocks;
        uint64 contractFeePercentage;
        uint32 activeCommitments;
        uint64 validDepositAmounts[16]; // Show valid deposit amounts
    };

    struct GetUserCommitments_input
    {
        id userId;
    };
    struct GetUserCommitments_output
    {
        struct UserCommitment {
            id digest;
            uint64 amount;
            uint32 commitTick;
            uint32 revealDeadlineTick;
            bool hasRevealed;
        } commitments[32]; // Max 32 commitments per user query
        uint32 commitmentCount;
    };

private:
    uint64 _contractFeeReserve;
    uint64 _totalCommits;
    uint64 _totalReveals;
    uint32 _minimumSecurityDeposit;

    // PROCEDURE: RevealAndCommit (Write Operation)
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
        const auto& input = qpi.input<RevealAndCommit_input>();
        auto invocatorId = qpi.invocator();
        auto invocatorAmount = qpi.invocationReward();
        auto currentTick = qpi.tick();
        
        bool isInitialCommit = isZeroBits(input.revealedBits);
        bool hasNewCommit = !isZeroId(input.committedDigest);
        
        // Process reveal if not initial commit
        if (!isInitialCommit)
        {
            // Find the commitment that matches the revealed bits
            for (uint32 i = 0; i < state.commitmentCount; i++)
            {
                if (!state.commitments[i].hasRevealed && 
                    isEqualId(state.commitments[i].invocatorId, invocatorId))
                {
                    // Verify the revealed bits match the committed digest
                    id computedHash = computeHash(input.revealedBits);
                    if (isEqualId(computedHash, state.commitments[i].digest))
                    {
                        // Check if reveal is within timeout
                        if (currentTick > state.commitments[i].revealDeadlineTick)
                        {
                            // Timeout exceeded, security deposit is forfeited
                            state.contractFeeReserve += state.commitments[i].amount;
                        }
                        else
                        {
                            // Valid reveal - add entropy to pool
                            updateEntropyPool(input.revealedBits);

                            // Calculate fee and return amount
                            uint64 fee = (state.commitments[i].amount * state.contractFeePercentage) / 10000;
                            uint64 returnAmount = state.commitments[i].amount - fee;

                            state.contractFeeReserve += fee;

                            // Return security deposit minus fee to invocator
                            if (returnAmount > 0)
                            {
                                qpi.transfer(invocatorId, returnAmount);
                            }
                            
                            state.totalReveals++;
                            state.totalSecurityDepositsLocked -= state.commitments[i].amount;
                        }

                        // Mark as revealed
                        state.commitments[i].hasRevealed = true;
                        break;
                    }
                }
            }
        }

        // Process new commitment if provided
        if (hasNewCommit)
        {
            // Check if deposit amount is valid (must be power of 10)
            if (!isValidDepositAmount(invocatorAmount))
            {
                return; // Invalid deposit amount
            }

            // Check minimum security deposit
            if (invocatorAmount < state.minimumSecurityDeposit)
            {
                return; // Insufficient security deposit
            }

            // Add new commitment
            if (state.commitmentCount < 1024)
            {
                state.commitments[state.commitmentCount].digest = input.committedDigest;
                state.commitments[state.commitmentCount].invocatorId = invocatorId;
                state.commitments[state.commitmentCount].amount = invocatorAmount;
                state.commitments[state.commitmentCount].commitTick = currentTick;
                state.commitments[state.commitmentCount].revealDeadlineTick = currentTick + state.revealTimeoutBlocks;
                state.commitments[state.commitmentCount].hasRevealed = false;

                state.commitmentCount++;
                state.totalCommits++;
                state.totalSecurityDepositsLocked += invocatorAmount;
            }
        }
    }

    // FUNCTION: GetRandomBytes (Read-Only)
    PUBLIC_FUNCTION(GetRandomBytes)
    {
        const auto& input = qpi.input<GetRandomBytes_input>();
        
        if (input.numberOfBytes > 32)
        {
            qpi.setMem(&output, 0, sizeof(output));
            return;
        }

        // Combine current entropy pool with nonce and current tick
        m256i combinedEntropy = xorM256i(state.currentEntropyPool, input.nonce);

        // Add current tick for additional unpredictability
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        combinedEntropy = xorM256i(combinedEntropy, tickEntropy);

        // Generate final random bytes
        id finalHash = computeHashFromM256i(combinedEntropy);

        // Copy requested number of bytes
        qpi.copyMem(output.randomBytes, finalHash.bytes, input.numberOfBytes);

        // Set remaining bytes to zero
        if (input.numberOfBytes < 32)
        {
            qpi.setMem(&output.randomBytes[input.numberOfBytes], 0, 32 - input.numberOfBytes);
        }

        output.entropyVersion = state.entropyPoolVersion;
    }

    // FUNCTION: GetContractInfo (Read-Only)
    PUBLIC_FUNCTION(GetContractInfo)
    {
        output.totalCommits = state.totalCommits;
        output.totalReveals = state.totalReveals;
        output.totalSecurityDepositsLocked = state.totalSecurityDepositsLocked;
        output.minimumSecurityDeposit = state.minimumSecurityDeposit;
        output.revealTimeoutBlocks = state.revealTimeoutBlocks;
        output.contractFeePercentage = state.contractFeePercentage;
        
        // Copy valid deposit amounts
        for (int i = 0; i < 16; i++)
        {
            output.validDepositAmounts[i] = state.validDepositAmounts[i];
        }
        
        // Count active (unrevealed) commitments
        uint32 activeCount = 0;
        for (uint32 i = 0; i < state.commitmentCount; i++)
        {
            if (!state.commitments[i].hasRevealed)
            {
                activeCount++;
            }
        }
        output.activeCommitments = activeCount;
    }

    // FUNCTION: GetUserCommitments (Read-Only)
    PUBLIC_FUNCTION(GetUserCommitments)
    {
        const auto& input = qpi.input<GetUserCommitments_input>();
        
        qpi.setMem(&output, 0, sizeof(output));
        
        uint32 userCommitmentCount = 0;
        for (uint32 i = 0; i < state.commitmentCount && userCommitmentCount < 32; i++)
        {
            if (isEqualId(state.commitments[i].invocatorId, input.userId))
            {
                output.commitments[userCommitmentCount].digest = state.commitments[i].digest;
                output.commitments[userCommitmentCount].amount = state.commitments[i].amount;
                output.commitments[userCommitmentCount].commitTick = state.commitments[i].commitTick;
                output.commitments[userCommitmentCount].revealDeadlineTick = state.commitments[i].revealDeadlineTick;
                output.commitments[userCommitmentCount].hasRevealed = state.commitments[i].hasRevealed;
                userCommitmentCount++;
            }
        }
        
        output.commitmentCount = userCommitmentCount;
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        // Functions (Read-Only Queries)
        REGISTER_USER_FUNCTION(GetRandomBytes, 1);
        REGISTER_USER_FUNCTION(GetContractInfo, 2);
        REGISTER_USER_FUNCTION(GetUserCommitments, 3);
        
        // Procedures (Write Operations)
        REGISTER_USER_PROCEDURE(RevealAndCommit, 1);
    }

    INITIALIZE()
    {
        state.contractFeeReserve = 0;
        qpi.setMem(&state.currentEntropyPool, 0, sizeof(m256i));
        state.entropyPoolVersion = 0;
        state.totalCommits = 0;
        state.totalReveals = 0;
        state.totalSecurityDepositsLocked = 0;
        state.minimumSecurityDeposit = 1000; // 1,000 QU minimum (not 1M!)
        state.revealTimeoutBlocks = 1000; // 1000 ticks timeout
        state.contractFeePercentage = 100; // 1% fee
        state.commitmentCount = 0;
        
        // Initialize valid deposit amounts (powers of 10)
        // 1, 10, 100, 1K, 10K, 100K, 1M, 10M, 100M, 1B, 10B, 100B, 1T, 10T, 100T, 1000T
        state.validDepositAmounts[0] = 1ULL;                    // 1 QU
        state.validDepositAmounts[1] = 10ULL;                   // 10 QU
        state.validDepositAmounts[2] = 100ULL;                  // 100 QU
        state.validDepositAmounts[3] = 1000ULL;                 // 1K QU
        state.validDepositAmounts[4] = 10000ULL;                // 10K QU
        state.validDepositAmounts[5] = 100000ULL;               // 100K QU
        state.validDepositAmounts[6] = 1000000ULL;              // 1M QU
        state.validDepositAmounts[7] = 10000000ULL;             // 10M QU
        state.validDepositAmounts[8] = 100000000ULL;            // 100M QU
        state.validDepositAmounts[9] = 1000000000ULL;           // 1B QU
        state.validDepositAmounts[10] = 10000000000ULL;         // 10B QU
        state.validDepositAmounts[11] = 100000000000ULL;        // 100B QU
        state.validDepositAmounts[12] = 1000000000000ULL;       // 1T QU
        state.validDepositAmounts[13] = 10000000000000ULL;      // 10T QU
        state.validDepositAmounts[14] = 100000000000000ULL;     // 100T QU
        state.validDepositAmounts[15] = 1000000000000000ULL;    // 1000T QU
        
        qpi.setMem(state.commitments, 0, sizeof(state.commitments));
    }

private:
    // Check if deposit amount is a valid power of 10
    bool isValidDepositAmount(uint64 amount)
    {
        for (int i = 0; i < 16; i++)
        {
            if (amount == state.validDepositAmounts[i])
            {
                return true;
            }
        }
        return false;
    }

    id computeHash(const bit_4096& data)
    {
        id result;
        qpi.setMem(&result, 0, sizeof(result));
        qpi.computeHash(&data, sizeof(data), &result);
        return result;
    }

    id computeHashFromM256i(const m256i& data)
    {
        id result;
        qpi.computeHash(&data, sizeof(data), &result);
        return result;
    }

    void updateEntropyPool(const bit_4096& newEntropy)
    {
        // XOR new entropy into the pool (taking first 256 bits)
        for (int i = 0; i < 4; i++)
        {
            state.currentEntropyPool.m256i_u64[i] ^= newEntropy.data[i];
        }
        state.entropyPoolVersion++;
    }

    m256i xorM256i(const m256i& a, const m256i& b)
    {
        m256i result;
        result.m256i_u64[0] = a.m256i_u64[0] ^ b.m256i_u64[0];
        result.m256i_u64[1] = a.m256i_u64[1] ^ b.m256i_u64[1];
        result.m256i_u64[2] = a.m256i_u64[2] ^ b.m256i_u64[2];
        result.m256i_u64[3] = a.m256i_u64[3] ^ b.m256i_u64[3];
        return result;
    }

    bool isEqualId(const id& a, const id& b)
    {
        for (int i = 0; i < 32; i++)
        {
            if (a.bytes[i] != b.bytes[i])
                return false;
        }
        return true;
    }

    bool isZeroId(const id& value)
    {
        for (int i = 0; i < 32; i++)
        {
            if (value.bytes[i] != 0)
                return false;
        }
        return true;
    }

    bool isZeroBits(const bit_4096& value)
    {
        for (int i = 0; i < 64; i++)
        {
            if (value.data[i] != 0)
                return false;
        }
        return true;
    }
};
