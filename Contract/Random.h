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
    uint32 revealTimeoutTicks; // Changed to 3 ticks
    uint64 entropyMinerSharePercentage; // 50% to miners
    
    // Valid deposit amounts (powers of 10)
    uint64 validDepositAmounts[16];
    
    // Commitment tracking by tick cycle (3N, 3N+1, 3N+2)
    struct EntropyCommitment {
        id digest;
        id invocatorId;
        uint64 amount;
        uint32 commitTick;
        uint32 revealDeadlineTick;
        bool hasRevealed;
        uint32 tickCycle; // 0, 1, or 2 for (3N, 3N+1, 3N+2)
    } commitments[1024];
    
    uint32 commitmentCount;
    
    // Revenue tracking for shareholder distribution
    uint64 totalRevenue;
    uint64 pendingShareholderDistribution;
    uint64 pendingMinerRewards;
    
    // Active miners by cycle for reward distribution
    struct CycleMiner {
        id minerId;
        uint64 totalDeposit;
        uint32 successfulReveals;
    } activeMiners[3][256]; // 3 cycles, max 256 miners per cycle
    
    uint32 activeMinerCount[3];
    
    // Entropy sales tracking
    uint64 entropySalesRevenue;
    uint64 entropySalesCount;
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

    struct BuyEntropy_input
    {
        uint32 numberOfBytes; // Max 32
        m256i nonce;         // Optional nonce
    };
    struct BuyEntropy_output
    {
        uint8 randomBytes[32];
        uint64 entropyVersion;
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
        uint32 revealTimeoutTicks;
        uint64 entropyMinerSharePercentage;
        uint32 activeCommitments;
        uint64 validDepositAmounts[16];
        uint64 totalRevenue;
        uint64 entropySalesRevenue;
        uint32 currentTick;
        uint32 currentCycle; // 0, 1, or 2
    };

    struct GetMinerInfo_input
    {
        id minerId;
    };
    struct GetMinerInfo_output
    {
        struct CycleInfo {
            uint64 totalDeposit;
            uint32 successfulReveals;
            uint64 pendingRewards;
        } cycles[3];
        uint64 totalPendingRewards;
    };

private:
    // PROCEDURE: RevealAndCommit (Write Operation)
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
        const auto& input = qpi.input<RevealAndCommit_input>();
        auto invocatorId = qpi.invocator();
        auto invocatorAmount = qpi.invocationReward();
        auto currentTick = qpi.tick();
        
        // Determine current cycle (0 = 3N, 1 = 3N+1, 2 = 3N+2)
        uint32 currentCycle = currentTick % 3;
        
        bool isInitialCommit = isZeroBits(input.revealedBits);
        bool hasNewCommit = !isZeroId(input.committedDigest);
        bool isStoppingMining = (invocatorAmount == 0);
        
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
                        // Check if reveal is within 3-tick timeout
                        if (currentTick > state.commitments[i].revealDeadlineTick)
                        {
                            // Timeout exceeded, security deposit is forfeited
                            state.contractFeeReserve += state.commitments[i].amount;
                            state.pendingMinerRewards += state.commitments[i].amount; // Lost deposits go to miners
                        }
                        else
                        {
                            // Valid reveal - add entropy to pool
                            updateEntropyPool(input.revealedBits);

                            // Return security deposit to miner
                            qpi.transfer(invocatorId, state.commitments[i].amount);
                            
                            // Update miner stats for reward calculation
                            updateMinerStats(invocatorId, state.commitments[i].tickCycle, state.commitments[i].amount);
                            
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

        // Process new commitment if provided and not stopping
        if (hasNewCommit && !isStoppingMining)
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
                state.commitments[state.commitmentCount].revealDeadlineTick = currentTick + state.revealTimeoutTicks;
                state.commitments[state.commitmentCount].hasRevealed = false;
                state.commitments[state.commitmentCount].tickCycle = currentCycle;

                state.commitmentCount++;
                state.totalCommits++;
                state.totalSecurityDepositsLocked += invocatorAmount;
            }
        }
    }

    // PROCEDURE: BuyEntropy (Write Operation)
    PUBLIC_PROCEDURE(BuyEntropy)
    {
        const auto& input = qpi.input<BuyEntropy_input>();
        auto buyer = qpi.invocator();
        auto payment = qpi.invocationReward();
        
        if (input.numberOfBytes > 32 || input.numberOfBytes == 0)
        {
            return; // Invalid request
        }

        // Calculate price (e.g., 100 QU per byte)
        uint64 pricePerByte = 100;
        uint64 totalPrice = input.numberOfBytes * pricePerByte;
        
        if (payment < totalPrice)
        {
            return; // Insufficient payment
        }

        // Generate and return random bytes
        m256i combinedEntropy = xorM256i(state.currentEntropyPool, input.nonce);
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        combinedEntropy = xorM256i(combinedEntropy, tickEntropy);
        id finalHash = computeHashFromM256i(combinedEntropy);

        // Return random bytes as transfer memo or through output mechanism
        // (Implementation depends on Qubic's output handling)

        // Record revenue
        state.entropySalesRevenue += payment;
        state.totalRevenue += payment;
        state.entropySalesCount++;

        // Split revenue: 50% to miners, 50% to shareholders
        uint64 minerShare = (payment * state.entropyMinerSharePercentage) / 100;
        uint64 shareholderShare = payment - minerShare;

        state.pendingMinerRewards += minerShare;
        state.pendingShareholderDistribution += shareholderShare;
    }

    // FUNCTION: GetRandomBytes (Read-Only - Free)
    PUBLIC_FUNCTION(GetRandomBytes)
    {
        const auto& input = qpi.input<GetRandomBytes_input>();
        
        if (input.numberOfBytes > 32)
        {
            qpi.setMem(&output, 0, sizeof(output));
            return;
        }

        // Generate random bytes (free version)
        m256i combinedEntropy = xorM256i(state.currentEntropyPool, input.nonce);
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        combinedEntropy = xorM256i(combinedEntropy, tickEntropy);
        id finalHash = computeHashFromM256i(combinedEntropy);

        qpi.copyMem(output.randomBytes, finalHash.bytes, input.numberOfBytes);

        if (input.numberOfBytes < 32)
        {
            qpi.setMem(&output.randomBytes[input.numberOfBytes], 0, 32 - input.numberOfBytes);
        }

        output.entropyVersion = state.entropyPoolVersion;
    }

    // FUNCTION: GetContractInfo (Read-Only)
    PUBLIC_FUNCTION(GetContractInfo)
    {
        auto currentTick = qpi.tick();
        
        output.totalCommits = state.totalCommits;
        output.totalReveals = state.totalReveals;
        output.totalSecurityDepositsLocked = state.totalSecurityDepositsLocked;
        output.minimumSecurityDeposit = state.minimumSecurityDeposit;
        output.revealTimeoutTicks = state.revealTimeoutTicks;
        output.entropyMinerSharePercentage = state.entropyMinerSharePercentage;
        output.totalRevenue = state.totalRevenue;
        output.entropySalesRevenue = state.entropySalesRevenue;
        output.currentTick = currentTick;
        output.currentCycle = currentTick % 3;
        
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

    // FUNCTION: GetMinerInfo (Read-Only)
    PUBLIC_FUNCTION(GetMinerInfo)
    {
        const auto& input = qpi.input<GetMinerInfo_input>();
        
        qpi.setMem(&output, 0, sizeof(output));
        
        // Calculate pending rewards for each cycle
        for (int cycle = 0; cycle < 3; cycle++)
        {
            for (uint32 i = 0; i < state.activeMinerCount[cycle]; i++)
            {
                if (isEqualId(state.activeMiners[cycle][i].minerId, input.minerId))
                {
                    output.cycles[cycle].totalDeposit = state.activeMiners[cycle][i].totalDeposit;
                    output.cycles[cycle].successfulReveals = state.activeMiners[cycle][i].successfulReveals;
                    
                    // Calculate pending rewards based on contribution
                    uint64 totalCycleDeposits = 0;
                    for (uint32 j = 0; j < state.activeMinerCount[cycle]; j++)
                    {
                        totalCycleDeposits += state.activeMiners[cycle][j].totalDeposit;
                    }
                    
                    if (totalCycleDeposits > 0)
                    {
                        output.cycles[cycle].pendingRewards = 
                            (state.pendingMinerRewards * output.cycles[cycle].totalDeposit) / (totalCycleDeposits * 3);
                    }
                    
                    output.totalPendingRewards += output.cycles[cycle].pendingRewards;
                    break;
                }
            }
        }
    }

    void BeginEpoch()
    {
        // Called at the beginning of each epoch
        // Could be used for epoch-based calculations if needed
    }

    void EndEpoch()
    {
        // Distribute rewards to shareholders (like in Quottery)
        if (state.pendingShareholderDistribution > 0)
        {
            // Distribute to Qubic shareholders
            // Implementation similar to Quottery contract
            qpi.transferShareToShareOwners(state.pendingShareholderDistribution);
            state.pendingShareholderDistribution = 0;
        }

        // Distribute miner rewards
        if (state.pendingMinerRewards > 0)
        {
            distributeMinerRewards();
            state.pendingMinerRewards = 0;
        }
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        REGISTER_USER_FUNCTION(GetRandomBytes, 1);
        REGISTER_USER_FUNCTION(GetContractInfo, 2);
        REGISTER_USER_FUNCTION(GetMinerInfo, 3);
        
        REGISTER_USER_PROCEDURE(RevealAndCommit, 1);
        REGISTER_USER_PROCEDURE(BuyEntropy, 2);
    }

    INITIALIZE()
    {
        state.contractFeeReserve = 0;
        qpi.setMem(&state.currentEntropyPool, 0, sizeof(m256i));
        state.entropyPoolVersion = 0;
        state.totalCommits = 0;
        state.totalReveals = 0;
        state.totalSecurityDepositsLocked = 0;
        state.minimumSecurityDeposit = 1000; // 1,000 QU minimum
        state.revealTimeoutTicks = 3; // 3 ticks timeout
        state.entropyMinerSharePercentage = 50; // 50% to miners
        state.commitmentCount = 0;
        
        state.totalRevenue = 0;
        state.pendingShareholderDistribution = 0;
        state.pendingMinerRewards = 0;
        state.entropySalesRevenue = 0;
        state.entropySalesCount = 0;
        
        // Initialize valid deposit amounts (powers of 10)
        state.validDepositAmounts[0] = 1ULL;
        state.validDepositAmounts[1] = 10ULL;
        state.validDepositAmounts[2] = 100ULL;
        state.validDepositAmounts[3] = 1000ULL;
        state.validDepositAmounts[4] = 10000ULL;
        state.validDepositAmounts[5] = 100000ULL;
        state.validDepositAmounts[6] = 1000000ULL;
        state.validDepositAmounts[7] = 10000000ULL;
        state.validDepositAmounts[8] = 100000000ULL;
        state.validDepositAmounts[9] = 1000000000ULL;
        state.validDepositAmounts[10] = 10000000000ULL;
        state.validDepositAmounts[11] = 100000000000ULL;
        state.validDepositAmounts[12] = 1000000000000ULL;
        state.validDepositAmounts[13] = 10000000000000ULL;
        state.validDepositAmounts[14] = 100000000000000ULL;
        state.validDepositAmounts[15] = 1000000000000000ULL;
        
        qpi.setMem(state.commitments, 0, sizeof(state.commitments));
        qpi.setMem(state.activeMiners, 0, sizeof(state.activeMiners));
        qpi.setMem(state.activeMinerCount, 0, sizeof(state.activeMinerCount));
    }

private:
    void distributeMinerRewards()
    {
        // Distribute rewards proportionally to miners across all cycles
        uint64 totalMinerDeposits = 0;
        
        // Calculate total deposits across all cycles
        for (int cycle = 0; cycle < 3; cycle++)
        {
            for (uint32 i = 0; i < state.activeMinerCount[cycle]; i++)
            {
                totalMinerDeposits += state.activeMiners[cycle][i].totalDeposit;
            }
        }
        
        if (totalMinerDeposits == 0) return;
        
        // Distribute rewards
        for (int cycle = 0; cycle < 3; cycle++)
        {
            for (uint32 i = 0; i < state.activeMinerCount[cycle]; i++)
            {
                uint64 minerReward = (state.pendingMinerRewards * state.activeMiners[cycle][i].totalDeposit) / totalMinerDeposits;
                if (minerReward > 0)
                {
                    qpi.transfer(state.activeMiners[cycle][i].minerId, minerReward);
                }
            }
        }
    }

    void updateMinerStats(const id& minerId, uint32 cycle, uint64 deposit)
    {
        // Find or add miner in the specific cycle
        for (uint32 i = 0; i < state.activeMinerCount[cycle]; i++)
        {
            if (isEqualId(state.activeMiners[cycle][i].minerId, minerId))
            {
                state.activeMiners[cycle][i].successfulReveals++;
                return;
            }
        }
        
        // Add new miner if not found and space available
        if (state.activeMinerCount[cycle] < 256)
        {
            state.activeMiners[cycle][state.activeMinerCount[cycle]].minerId = minerId;
            state.activeMiners[cycle][state.activeMinerCount[cycle]].totalDeposit = deposit;
            state.activeMiners[cycle][state.activeMinerCount[cycle]].successfulReveals = 1;
            state.activeMinerCount[cycle]++;
        }
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
