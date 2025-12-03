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
    uint32 revealTimeoutTicks; // 3 ticks for reveal
    
    // Valid deposit amounts (powers of 10)
    uint64 validDepositAmounts[16];
    
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
    // MAIN PROCEDURE - Does everything: commit, reveal, returns random bytes
    struct RevealAndCommit_input
    {
        bit_4096 revealedBits;  // Previous entropy to reveal (or zeros for first commit)
        id committedDigest;     // Hash of new entropy to commit (or zeros if stopping)
    };
    
    struct RevealAndCommit_output
    {
        uint8 randomBytes[32];      // Fresh random bytes generated from entropy pool
        uint64 entropyVersion;      // Version of entropy pool used
        bool revealSuccessful;      // Whether a reveal was processed
        bool commitSuccessful;      // Whether a new commit was accepted
        uint64 depositReturned;     // Amount of deposit returned (if any)
        uint64 rewardEarned;        // Reward earned from reveal (if any)
    };

    // READ-ONLY FUNCTIONS - For information only
    struct GetContractInfo_input
    {
    };
    struct GetContractInfo_output
    {
        uint64 totalCommits;
        uint64 totalReveals;
        uint64 totalSecurityDepositsLocked;
        uint64 minimumSecurityDeposit;
        uint32 revealTimeoutTicks;
        uint32 activeCommitments;
        uint64 validDepositAmounts[16];
        uint32 currentTick;
        uint64 entropyPoolVersion;
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
        } commitments[32];
        uint32 commitmentCount;
    };

private:
    // THE ONLY PROCEDURE - RevealAndCommit does all the work
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
        const auto& input = qpi.input<RevealAndCommit_input>();
        auto invocatorId = qpi.invocator();
        auto invocatorAmount = qpi.invocationReward();
        auto currentTick = qpi.tick();
        
        // Initialize output
        qpi.setMem(&output, 0, sizeof(output));
        
        bool hasRevealData = !isZeroBits(input.revealedBits);
        bool hasNewCommit = !isZeroId(input.committedDigest);
        bool isStoppingMining = (invocatorAmount == 0);
        
        // Step 1: Process reveal if provided
        if (hasRevealData)
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
                        // Check if reveal is within 3-tick timeout
                        if (currentTick > state.commitments[i].revealDeadlineTick)
                        {
                            // Timeout exceeded - security deposit is forfeited
                            state.contractFeeReserve += state.commitments[i].amount;
                            output.revealSuccessful = false;
                        }
                        else
                        {
                            // Valid reveal - add entropy to pool
                            updateEntropyPool(input.revealedBits);

                            // Calculate reward (5% of deposit)
                            uint64 reward = (state.commitments[i].amount * 5) / 100;
                            uint64 totalReturn = state.commitments[i].amount + reward;
                            
                            // Return security deposit + reward to miner
                            if (totalReturn > 0)
                            {
                                qpi.transfer(invocatorId, totalReturn);
                            }
                            
                            output.revealSuccessful = true;
                            output.depositReturned = state.commitments[i].amount;
                            output.rewardEarned = reward;
                            
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

        // Step 2: Process new commitment if provided and not stopping
        if (hasNewCommit && !isStoppingMining)
        {
            // Check if deposit amount is valid (must be power of 10)
            if (isValidDepositAmount(invocatorAmount) && invocatorAmount >= state.minimumSecurityDeposit)
            {
                // Add new commitment
                if (state.commitmentCount < 1024)
                {
                    state.commitments[state.commitmentCount].digest = input.committedDigest;
                    state.commitments[state.commitmentCount].invocatorId = invocatorId;
                    state.commitments[state.commitmentCount].amount = invocatorAmount;
                    state.commitments[state.commitmentCount].commitTick = currentTick;
                    state.commitments[state.commitmentCount].revealDeadlineTick = currentTick + state.revealTimeoutTicks;
                    state.commitments[state.commitmentCount].hasRevealed = false;

                    state.commitmentCount++;
                    state.totalCommits++;
                    state.totalSecurityDepositsLocked += invocatorAmount;
                    
                    output.commitSuccessful = true;
                }
            }
        }

        // Step 3: ALWAYS generate and return fresh random bytes
        generateRandomBytes(output.randomBytes, 32);
        output.entropyVersion = state.entropyPoolVersion;
    }

    // READ-ONLY: Get contract statistics
    PUBLIC_FUNCTION(GetContractInfo)
    {
        auto currentTick = qpi.tick();
        
        output.totalCommits = state.totalCommits;
        output.totalReveals = state.totalReveals;
        output.totalSecurityDepositsLocked = state.totalSecurityDepositsLocked;
        output.minimumSecurityDeposit = state.minimumSecurityDeposit;
        output.revealTimeoutTicks = state.revealTimeoutTicks;
        output.currentTick = currentTick;
        output.entropyPoolVersion = state.entropyPoolVersion;
        
        for (int i = 0; i < 16; i++)
        {
            output.validDepositAmounts[i] = state.validDepositAmounts[i];
        }
        
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

    // READ-ONLY: Get user's commitment history
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

    void BeginEpoch()
    {
        // Called at the beginning of each epoch
    }

    void EndEpoch()
    {
        // Called at the end of each epoch
        // Could distribute accumulated fees to shareholders if needed
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        // Read-only functions
        REGISTER_USER_FUNCTION(GetContractInfo, 1);
        REGISTER_USER_FUNCTION(GetUserCommitments, 2);
        
        // The only procedure
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
        state.minimumSecurityDeposit = 1000; // 1K QU minimum
        state.revealTimeoutTicks = 3; // 3 ticks timeout
        state.commitmentCount = 0;
        
        // Initialize valid deposit amounts (powers of 10: 1, 10, 100, 1K, 10K, etc.)
        for (int i = 0; i < 16; i++)
        {
            state.validDepositAmounts[i] = 1ULL;
            for (int j = 0; j < i; j++)
            {
                state.validDepositAmounts[i] *= 10;
            }
        }
        
        qpi.setMem(state.commitments, 0, sizeof(state.commitments));
    }

private:
    void generateRandomBytes(uint8* output, uint32 numBytes)
    {
        // Combine current entropy pool with current tick
        m256i combinedEntropy = state.currentEntropyPool;
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        combinedEntropy = xorM256i(combinedEntropy, tickEntropy);
        
        // Generate hash
        id finalHash = computeHashFromM256i(combinedEntropy);
        
        // Copy requested bytes (max 32)
        qpi.copyMem(output, finalHash.bytes, (numBytes > 32) ? 32 : numBytes);
    }

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
