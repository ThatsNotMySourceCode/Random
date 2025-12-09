#pragma once

using namespace QPI;

// Max miners to consider for distribution
#define MAX_RECENT_MINERS 369

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
    uint32 revealTimeoutTicks; // e.g. 3, 6 or 9 ticks for reveal

    // Revenue distribution system
    uint64 totalRevenue;                    // All revenue generated
    uint64 pendingShareholderDistribution;  // 50% to Qubic shareholders (from lost deposits + entropy sales)
    uint64 lostDepositsRevenue;             // From timeout failures - goes to shareholders

    // Earnings pools
    uint64 minerEarningsPool;               // Accumulated earnings for miners (for entropy sales)
    uint64 shareholderEarningsPool;         // Accumulated for Qubic shareholders
	
	uint64 pricePerByte;         // e.g. 10 QU (default)
	uint64 priceFreshRevealMul;  // e.g. 1 (just multiplies by minFreshReveals)
	uint64 priceDepositDivisor;  // e.g. 1000 (matches contract formula)

    // Epoch tracking for miner rewards
    struct RecentMiner {
        id minerId;
        uint64 deposit;
        uint64 lastEntropyVersion;        // When miner's last reveal was mixed in
    } recentMiners[MAX_RECENT_MINERS];
    uint32 recentMinerCount;

    uint64 lastRevealTick; // Track tick of last entropy reveal

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
        uint32 numberOfBytes;         // 1-32
        uint32 minFreshReveals;       // Require at least this many new reveals in pool
        uint64 minMinerDeposit;       // Require each entropy miner to have at least this deposit
    };

    struct BuyEntropy_output
    {
        bool success;
        uint8 randomBytes[32];
        uint64 entropyVersion;
        uint32 actualFreshReveals;
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
		uint32 minFreshReveals;
		uint64 minMinerDeposit;
	};

	struct QueryPrice_output {
		uint64 price;
	};

    // --------------------------------------------------
    // Mining: RevealAndCommit 
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
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
                            // Timeout - deposit goes to shareholders
                            uint64 lostDeposit = state.commitments[i].amount;
                            state.lostDepositsRevenue += lostDeposit;
                            state.totalRevenue += lostDeposit;
                            state.pendingShareholderDistribution += lostDeposit;

                            output.revealSuccessful = false;
                        }
                        else
                        {
                            // Successful reveal - add entropy to pool
                            updateEntropyPool(input.revealedBits);

                            // Return full deposit to successful miner
                            qpi.transfer(invocatorId, state.commitments[i].amount);

                            output.revealSuccessful = true;
                            output.depositReturned = state.commitments[i].amount;

                            state.totalReveals++;
                            state.totalSecurityDepositsLocked -= state.commitments[i].amount;

                            // --- Best performance/highest stake ranking logic for RecentMiners ---

                            // Check if miner is already in the list
                            int32 existingIndex = -1;
                            for(uint32 rm = 0; rm < state.recentMinerCount; ++rm) {
                                if(isEqualId(state.recentMiners[rm].minerId, invocatorId)) {
                                    existingIndex = rm;
                                    break;
                                }
                            }

                            // If present, update deposit if this is higher
                            if(existingIndex >= 0) {
                                if(state.recentMiners[existingIndex].deposit < state.commitments[i].amount) {
                                    state.recentMiners[existingIndex].deposit = state.commitments[i].amount;
                                    state.recentMiners[existingIndex].lastEntropyVersion = state.entropyPoolVersion;
                                }
                            }
                            else {
                                // Add new miner
                                if(state.recentMinerCount < MAX_RECENT_MINERS) {
                                    state.recentMiners[state.recentMinerCount].minerId = invocatorId;
                                    state.recentMiners[state.recentMinerCount].deposit = state.commitments[i].amount;
                                    state.recentMiners[state.recentMinerCount].lastEntropyVersion = state.entropyPoolVersion;
                                    state.recentMinerCount++;
                                }
                                else {
                                    // Handle overflow: evict the lowest-stake or least-recent miner
                                    // Find the lowest-stake; if tie, pick oldest
                                    uint32 lowestIx = 0;
                                    for(uint32 rm = 1; rm < MAX_RECENT_MINERS; ++rm) {
                                        // Lower deposit, or (if equal deposit) older entropy version is less preferred
                                        if(state.recentMiners[rm].deposit < state.recentMiners[lowestIx].deposit ||
                                            (state.recentMiners[rm].deposit == state.recentMiners[lowestIx].deposit && 
                                             state.recentMiners[rm].lastEntropyVersion < state.recentMiners[lowestIx].lastEntropyVersion))
                                        {
                                            lowestIx = rm;
                                        }
                                    }
                                    // Replace that entry with the new higher-stake miner
                                    if (state.commitments[i].amount > state.recentMiners[lowestIx].deposit ||
                                        // If equal deposit, prefer newer entropy version
                                        (state.commitments[i].amount == state.recentMiners[lowestIx].deposit &&
                                         state.entropyPoolVersion > state.recentMiners[lowestIx].lastEntropyVersion)) 
                                    {
                                        state.recentMiners[lowestIx].minerId = invocatorId;
                                        state.recentMiners[lowestIx].deposit = state.commitments[i].amount;
                                        state.recentMiners[lowestIx].lastEntropyVersion = state.entropyPoolVersion;
                                    }
                                    // Otherwise, do not include: not high enough stake/relevance
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

        // Always return random bytes
        generateRandomBytes(output.randomBytes, 32);
        output.entropyVersion = state.entropyPoolVersion;
    }

    // --------------------------------------------------
    // BUY ENTROPY / RANDOM BYTES 
    PUBLIC_PROCEDURE(BuyEntropy)
    {
        const auto& input = qpi.input<BuyEntropy_input>();
        qpi.setMem(&output, 0, sizeof(output));
        output.success = false;

        auto currentTick = qpi.tick();
        uint64 buyerFee = qpi.invocationReward();

        uint32 validFreshReveals = 0;
        uint64 usedDeposit = 0;
        uint64 usedPoolVersion = state.entropyPoolVersion;

        // First: ensure recent entropy exists
        // Recent = new entropy since last sales/within last X ticks
        for(uint32 i=0; i < state.recentMinerCount; ++i) {
            if(state.recentMiners[i].deposit >= input.minMinerDeposit &&
               (currentTick - state.lastRevealTick) <= state.revealTimeoutTicks) { // Use revealTimeoutTicks as freshness window
                ++validFreshReveals;
                usedDeposit = state.recentMiners[i].deposit; // Could use min/max/average
            }
        }

        if(validFreshReveals < input.minFreshReveals) {
            // Not enough high-stake entropy — don't sell
            return;
        }

		uint64 minPrice = 
			  state.pricePerByte
			* input.numberOfBytes
			* state.priceFreshRevealMul * input.minFreshReveals
			* (input.minMinerDeposit / state.priceDepositDivisor + 1);
        if(buyerFee < minPrice) {
            // Not enough paid
            return;
        }

        // Return random bytes (secure)
        generateRandomBytes(output.randomBytes, input.numberOfBytes > 32 ? 32 : input.numberOfBytes);
        output.entropyVersion = state.entropyPoolVersion;
        output.actualFreshReveals = validFreshReveals;
        output.usedMinerDeposit = usedDeposit;
        output.usedPoolVersion = usedPoolVersion;
        output.success = true;

        // Split earnings: 50% miners, 50% shareholders (holding in pools until epoch end)
        uint64 half = buyerFee/2;
        state.minerEarningsPool += half;
        state.shareholderEarningsPool += (buyerFee-half);
    }

    // --------------------------------------------------
    // MINER REWARD CLAIM (Optional)
    PUBLIC_PROCEDURE(ClaimMinerEarnings)
    {
        // (For individual, advanced; not needed if you only distribute at epoch end.)
        output.payout = 0; // All paid out at epoch end in this implementation.
        // Optionally allow immediate/minimum payout if you want.
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
		output.price = 
			state.pricePerByte
		  * input.numberOfBytes
		  * state.priceFreshRevealMul * input.minFreshReveals
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
		REGISTER_USER_PROCEDURE(ClaimMinerEarnings,    3);
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
        state.revealTimeoutTicks = 9; // 9 ticks timeout by default (can change)
        state.commitmentCount = 0;

        // Revenue tracking
        state.totalRevenue = 0;
        state.pendingShareholderDistribution = 0;
        state.lostDepositsRevenue = 0;

        state.minerEarningsPool = 0;
        state.shareholderEarningsPool = 0;
        state.recentMinerCount = 0;
        state.lastRevealTick = 0;
		
		// Pricing (for buyers)
		state.pricePerByte = 10;
		state.priceFreshRevealMul = 1;
		state.priceDepositDivisor = 1000;

        // Initialize valid security deposit amounts (powers of 10) for miners
        for (int i = 0; i < 16; i++)
        {
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
        {
            state.currentEntropyPool.m256i_u64[i] ^= newEntropy.data[i];
        }
        state.entropyPoolVersion++;
    }
	
	void generateRandomBytes(uint8* output, uint32 numBytes)
    {
        // Combine current entropy pool with current tick for uniqueness
        m256i combinedEntropy = state.currentEntropyPool;
        m256i tickEntropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        combinedEntropy = xorM256i(combinedEntropy, tickEntropy);
        
        // Generate hash
        id finalHash = computeHashFromM256i(combinedEntropy);
        
        // Copy requested bytes (max 32)
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
        for (int i = 0; i < 16; i++) {
            if (amount == state.validDepositAmounts[i])
                return true;
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
            if (a.bytes[i] != b.bytes[i])
                return false;
        return true;
    }

    bool isZeroId(const id& value)
    {
        for (int i = 0; i < 32; i++)
            if (value.bytes[i] != 0)
                return false;
        return true;
    }

    bool isZeroBits(const bit_4096& value)
    {
        for (int i = 0; i < 64; i++)
            if (value.data[i] != 0)
                return false;
        return true;
    }
};
