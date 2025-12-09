#pragma once

using namespace QPI;

// Max miners to consider for distribution
#define MAX_RECENT_MINERS 369
#define ENTROPY_HISTORY_LEN 3 // For 2-tick-back entropy pool

struct RANDOM_CONTRACT_STATE
{
    // Entropy pool history (circular buffer for look-back)
    m256i entropyHistory[ENTROPY_HISTORY_LEN];
    uint64 entropyPoolVersionHistory[ENTROPY_HISTORY_LEN];
    uint32 entropyHistoryHead; // points to most recent

    // Global entropy pool - combines all revealed entropy
    m256i currentEntropyPool;
    uint64 entropyPoolVersion;

    // Tracking statistics
    uint64 totalCommits;
    uint64 totalReveals;
    uint64 totalSecurityDepositsLocked;

    // Contract configuration
    uint64 minimumSecurityDeposit;
    uint32 revealTimeoutTicks; // now e.g. 9 ticks

    // Revenue distribution system
    uint64 totalRevenue;
    uint64 pendingShareholderDistribution;
    uint64 lostDepositsRevenue;

    // Earnings pools
    uint64 minerEarningsPool;
    uint64 shareholderEarningsPool;

    // Pricing config
    uint64 pricePerByte;         // e.g. 10 QU (default)
    uint64 priceDepositDivisor;  // e.g. 1000 (matches contract formula)

    // Epoch tracking for miner rewards
    struct RecentMiner {
        id minerId;
        uint64 deposit;
        uint64 lastEntropyVersion;
    } recentMiners[MAX_RECENT_MINERS];
    uint32 recentMinerCount;

    uint64 lastRevealTick;

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
    // --------------------------------------------------
    // Entropy mining (commit-reveal)
    struct RevealAndCommit_input
    {
        bit_4096 revealedBits;   // Previous entropy to reveal (or zeros for first commit)
        id committedDigest;      // Hash of new entropy to commit (or zeros if stopping)
    };

    struct RevealAndCommit_output
    {
        uint8 randomBytes[32];
        uint64 entropyVersion;
        bool revealSuccessful;
        bool commitSuccessful;
        uint64 depositReturned;
    };

    // --------------------------------------------------
    // READ-ONLY FUNCTIONS

    struct GetContractInfo_input {};
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
        // Revenue + pools
        uint64 totalRevenue;
        uint64 pendingShareholderDistribution;
        uint64 lostDepositsRevenue;
        uint64 minerEarningsPool;
        uint64 shareholderEarningsPool;
        uint32 recentMinerCount;
    };

    struct GetUserCommitments_input { id userId; };
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

    // --------------------------------------------------
    // SELL ENTROPY (random bytes)
    struct BuyEntropy_input
    {
        uint32 numberOfBytes;      // 1-32
        uint64 minMinerDeposit;    // required deposit of recent miner
    };
    struct BuyEntropy_output
    {
        bool success;
        uint8 randomBytes[32];
        uint64 entropyVersion; // version of pool 2 ticks ago!
        uint64 usedMinerDeposit;
        uint64 usedPoolVersion;
    };

    // --------------------------------------------------
    // CLAIMING
    struct ClaimMinerEarnings_input { };
    struct ClaimMinerEarnings_output
    {
        uint64 payout;
    };
	
	// --------------------------------------------------
    // PRICE QUERY
    struct QueryPrice_input {
        uint32 numberOfBytes;
        uint64 minMinerDeposit;
    };
    struct QueryPrice_output {
        uint64 price;
    };

    // --------------------------------------------------
    // Mining: RevealAndCommit 
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
        // Empty tick handling:
        if(qpi.numberOfTickTransactions() == -1) {
            // Tick is empty: refund all pending commitments whose deadline is now
            for(uint32 i = 0; i < state.commitmentCount; i++)
            {
                if(!state.commitments[i].hasRevealed &&
                   state.commitments[i].revealDeadlineTick == qpi.tick())
                {
                    qpi.transfer(state.commitments[i].invocatorId, state.commitments[i].amount);
                    state.commitments[i].hasRevealed = true;
                    state.totalSecurityDepositsLocked -= state.commitments[i].amount;
                }
            }
            // Return blank output (all refunds handled above)
            qpi.setMem(&output, 0, sizeof(output));
            return;
        }

        const auto& input = qpi.input<RevealAndCommit_input>();
        auto invocatorId = qpi.invocator();
        auto invocatorAmount = qpi.invocationReward();
        auto currentTick = qpi.tick();

        qpi.setMem(&output, 0, sizeof(output));

        bool hasRevealData = !isZeroBits(input.revealedBits);
        bool hasNewCommit = !isZeroId(input.committedDigest);
        bool isStoppingMining = (invocatorAmount == 0);

        // Step 1: Process reveal if provided
        if (hasRevealData)
        {
            for (uint32 i = 0; i < state.commitmentCount; i++)
            {
                if (!state.commitments[i].hasRevealed && 
                    isEqualId(state.commitments[i].invocatorId, invocatorId))
                {
                    id computedHash = computeHash(input.revealedBits);
                    if (isEqualId(computedHash, state.commitments[i].digest))
                    {
                        if (currentTick > state.commitments[i].revealDeadlineTick)
                        {
                            uint64 lostDeposit = state.commitments[i].amount;
                            state.lostDepositsRevenue += lostDeposit;
                            state.totalRevenue += lostDeposit;
                            state.pendingShareholderDistribution += lostDeposit;

                            output.revealSuccessful = false;
                        }
                        else
                        {
                            updateEntropyPool(input.revealedBits);

                            qpi.transfer(invocatorId, state.commitments[i].amount);

                            output.revealSuccessful = true;
                            output.depositReturned = state.commitments[i].amount;

                            state.totalReveals++;
                            state.totalSecurityDepositsLocked -= state.commitments[i].amount;

                            // Update RecentMiners
                            int32 existingIndex = -1;
                            for(uint32 rm = 0; rm < state.recentMinerCount; ++rm) {
                                if(isEqualId(state.recentMiners[rm].minerId, invocatorId)) {
                                    existingIndex = rm;
                                    break;
                                }
                            }
                            if(existingIndex >= 0) {
                                if(state.recentMiners[existingIndex].deposit < state.commitments[i].amount) {
                                    state.recentMiners[existingIndex].deposit = state.commitments[i].amount;
                                    state.recentMiners[existingIndex].lastEntropyVersion = state.entropyPoolVersion;
                                }
                            }
                            else {
                                if(state.recentMinerCount < MAX_RECENT_MINERS) {
                                    state.recentMiners[state.recentMinerCount].minerId = invocatorId;
                                    state.recentMiners[state.recentMinerCount].deposit = state.commitments[i].amount;
                                    state.recentMiners[state.recentMinerCount].lastEntropyVersion = state.entropyPoolVersion;
                                    state.recentMinerCount++;
                                }
                                else {
                                    // Overflow: evict lowest-stake or least-recent miner
                                    uint32 lowestIx = 0;
                                    for(uint32 rm = 1; rm < MAX_RECENT_MINERS; ++rm) {
                                        if(state.recentMiners[rm].deposit < state.recentMiners[lowestIx].deposit ||
                                           (state.recentMiners[rm].deposit == state.recentMiners[lowestIx].deposit && 
                                            state.recentMiners[rm].lastEntropyVersion < state.recentMiners[lowestIx].lastEntropyVersion))
                                            lowestIx = rm;
                                    }
                                    if(state.commitments[i].amount > state.recentMiners[lowestIx].deposit ||
                                       (state.commitments[i].amount == state.recentMiners[lowestIx].deposit &&
                                        state.entropyPoolVersion > state.recentMiners[lowestIx].lastEntropyVersion)) 
                                    {
                                        state.recentMiners[lowestIx].minerId = invocatorId;
                                        state.recentMiners[lowestIx].deposit = state.commitments[i].amount;
                                        state.recentMiners[lowestIx].lastEntropyVersion = state.entropyPoolVersion;
                                    }
                                }
                            }

                            state.lastRevealTick = currentTick;
                        }
                        state.commitments[i].hasRevealed = true;
                        break;
                    }
                }
            }
        }

        // Step 2: Process new commitment
        if (hasNewCommit && !isStoppingMining)
        {
            if (isValidDepositAmount(invocatorAmount) && invocatorAmount >= state.minimumSecurityDeposit)
            {
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

        // Always return random bytes (current pool)
        generateRandomBytes(output.randomBytes, 32, 0); // 0 = current pool
        output.entropyVersion = state.entropyPoolVersion;
    }

    // --------------------------------------------------
    // BUY ENTROPY / RANDOM BYTES 
    PUBLIC_PROCEDURE(BuyEntropy)
    {
        if(qpi.numberOfTickTransactions() == -1) {
            qpi.setMem(&output, 0, sizeof(output));
            output.success = false;
            return;
        }
        const auto& input = qpi.input<BuyEntropy_input>();
        qpi.setMem(&output, 0, sizeof(output));
        output.success = false;

        auto currentTick = qpi.tick();
        uint64 buyerFee = qpi.invocationReward();

        // Find a fresh enough miner with deposit >= minMinerDeposit
        bool eligible = false;
        uint64 usedMinerDeposit = 0;
        for(uint32 i=0; i < state.recentMinerCount; ++i) {
            if(state.recentMiners[i].deposit >= input.minMinerDeposit && 
               (currentTick - state.lastRevealTick) <= state.revealTimeoutTicks) {
                   eligible = true;
                   usedMinerDeposit = state.recentMiners[i].deposit;
                   break;
            }
        }

        if(!eligible)
            return;

        // True pricing
        uint64 minPrice = state.pricePerByte
                        * input.numberOfBytes
                        * (input.minMinerDeposit / state.priceDepositDivisor + 1);
        if(buyerFee < minPrice)
            return;

        // Serve — use entropy from 2 ticks ago!
        uint32 hist_idx = (state.entropyHistoryHead + ENTROPY_HISTORY_LEN - 2) % ENTROPY_HISTORY_LEN;
        generateRandomBytes(output.randomBytes, input.numberOfBytes > 32 ? 32 : input.numberOfBytes, hist_idx);
        output.entropyVersion = state.entropyPoolVersionHistory[hist_idx];
        output.usedMinerDeposit = usedMinerDeposit;
        output.usedPoolVersion  = state.entropyPoolVersionHistory[hist_idx];
        output.success = true;

        uint64 half = buyerFee/2;
        state.minerEarningsPool += half;
        state.shareholderEarningsPool += (buyerFee - half);
    }

    // --------------------------------------------------
    // Read-only contract info
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

        output.totalRevenue = state.totalRevenue;
        output.pendingShareholderDistribution = state.pendingShareholderDistribution;
        output.lostDepositsRevenue = state.lostDepositsRevenue;

        output.minerEarningsPool = state.minerEarningsPool;
        output.shareholderEarningsPool = state.shareholderEarningsPool;
        output.recentMinerCount = state.recentMinerCount;

        for (int i = 0; i < 16; i++)
            output.validDepositAmounts[i] = state.validDepositAmounts[i];

        uint32 activeCount = 0;
        for (uint32 i = 0; i < state.commitmentCount; i++)
            if (!state.commitments[i].hasRevealed)
                activeCount++;
        output.activeCommitments = activeCount;
    }

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
	
    PUBLIC_FUNCTION(QueryPrice)
    {
        const auto& input = qpi.input<QueryPrice_input>();
        output.price = state.pricePerByte
                    * input.numberOfBytes
                    * (input.minMinerDeposit / state.priceDepositDivisor + 1);
    }

    // --------------------------------------------------
    // Epoch End: Distribute pools
    void EndEpoch()
    {
        // Distribute miner pool
        if(state.minerEarningsPool > 0 && state.recentMinerCount > 0) {
            uint64 payout = state.minerEarningsPool / state.recentMinerCount;
            for(uint32 i=0; i<state.recentMinerCount; ++i) {
                if(!isZeroId(state.recentMiners[i].minerId))
                    qpi.transfer(state.recentMiners[i].minerId, payout);
            }
            state.minerEarningsPool = 0;
            qpi.setMem(state.recentMiners, 0, sizeof(state.recentMiners));
            state.recentMinerCount = 0;
        }

        // Distribute to shareholders
        if(state.shareholderEarningsPool > 0) {
            qpi.transferShareToShareOwners(state.shareholderEarningsPool);
            state.shareholderEarningsPool = 0;
        }

        // Continue current lost deposit distribution as before
        if (state.pendingShareholderDistribution > 0)
        {
            qpi.transferShareToShareOwners(state.pendingShareholderDistribution);
            state.pendingShareholderDistribution = 0;
        }
    }

    // --------------------------------------------------
	REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
	{
		// READ-ONLY USER FUNCTIONS 
		REGISTER_USER_FUNCTION(GetContractInfo,        1);
		REGISTER_USER_FUNCTION(GetUserCommitments,     2);
		REGISTER_USER_FUNCTION(QueryPrice,             3);

		// USER PROCEDURES 
		REGISTER_USER_PROCEDURE(RevealAndCommit,       1);
		REGISTER_USER_PROCEDURE(BuyEntropy,            2);
	}

    INITIALIZE()
    {
        state.entropyHistoryHead = 0;
        for (int i = 0; i < ENTROPY_HISTORY_LEN; ++i) {
            qpi.setMem(&state.entropyHistory[i], 0, sizeof(m256i));
            state.entropyPoolVersionHistory[i] = 0;
        }
        qpi.setMem(&state.currentEntropyPool, 0, sizeof(m256i));
        state.entropyPoolVersion = 0;
        state.totalCommits = 0;
        state.totalReveals = 0;
        state.totalSecurityDepositsLocked = 0;
        state.minimumSecurityDeposit = 1; // Now allow 1 QU
        state.revealTimeoutTicks = 9;
        state.commitmentCount = 0;
        state.totalRevenue = 0;
        state.pendingShareholderDistribution = 0;
        state.lostDepositsRevenue = 0;
        state.minerEarningsPool = 0;
        state.shareholderEarningsPool = 0;
        state.recentMinerCount = 0;
        state.lastRevealTick = 0;
        state.pricePerByte = 10;
        state.priceDepositDivisor = 1000;
        for (int i = 0; i < 16; i++) {
            state.validDepositAmounts[i] = 1ULL;
            for (int j = 0; j < i; j++)
                state.validDepositAmounts[i] *= 10;
        }
        qpi.setMem(state.commitments, 0, sizeof(state.commitments));
        qpi.setMem(state.recentMiners, 0, sizeof(state.recentMiners));
    }

private:
    // -- Other helper functions
    
    void updateEntropyPool(const bit_4096& newEntropy)
    {
        // XOR new entropy into the global pool
        for (int i = 0; i < 4; i++)
            state.currentEntropyPool.m256i_u64[i] ^= newEntropy.data[i];

        // Update entropy history (circular buffer)
        state.entropyHistoryHead = (state.entropyHistoryHead + 1) % ENTROPY_HISTORY_LEN;
        state.entropyHistory[state.entropyHistoryHead] = state.currentEntropyPool;
        state.entropyPoolVersion++;
        state.entropyPoolVersionHistory[state.entropyHistoryHead] = state.entropyPoolVersion;
    }
	
    void generateRandomBytes(uint8* output, uint32 numBytes, uint32 historyIdx)
    {
        m256i selectedPool = state.entropyHistory[(state.entropyHistoryHead + ENTROPY_HISTORY_LEN - historyIdx) % ENTROPY_HISTORY_LEN];
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        m256i combinedEntropy;
        for (int i = 0; i < 4; i++) combinedEntropy.m256i_u64[i] = selectedPool.m256i_u64[i] ^ tickEntropy.m256i_u64[i];
        id finalHash = computeHashFromM256i(combinedEntropy);
        qpi.copyMem(output, finalHash.bytes, (numBytes > 32) ? 32 : numBytes);
    }
	
	void processTimeouts()
    {
        // Check for failed reveals and process lost deposits
        uint32 currentTick = qpi.tick();
        
        for (uint32 i = 0; i < state.commitmentCount; i++)
        {
            if (!state.commitments[i].hasRevealed && 
                currentTick > state.commitments[i].revealDeadlineTick)
            {
                // This commit has timed out - deposit goes to shareholders
                uint64 lostDeposit = state.commitments[i].amount;
                state.lostDepositsRevenue += lostDeposit;
                state.totalRevenue += lostDeposit;
                state.pendingShareholderDistribution += lostDeposit;
                
                // Mark as processed
                state.commitments[i].hasRevealed = true;
                state.totalSecurityDepositsLocked -= lostDeposit;
            }
        }
    }

    bool isValidDepositAmount(uint64 amount)
    {
        for (int i = 0; i < 16; i++) if (amount == state.validDepositAmounts[i]) return true;
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

    bool isEqualId(const id& a, const id& b)
    {
        for (int i = 0; i < 32; i++) if (a.bytes[i] != b.bytes[i]) return false;
        return true;
    }

    bool isZeroId(const id& value)
    {
        for (int i = 0; i < 32; i++) if (value.bytes[i] != 0) return false;
        return true;
    }

    bool isZeroBits(const bit_4096& value)
    {
        for (int i = 0; i < 64; i++) if (value.data[i] != 0) return false;
        return true;
    }
};
