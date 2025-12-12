using namespace QPI;

constexpr uint32_t RANDOM_MAX_RECENT_MINERS = 512;   // 2^9
constexpr uint32_t RANDOM_MAX_COMMITMENTS = 1024;    // 2^10
constexpr uint32_t RANDOM_ENTROPY_HISTORY_LEN = 4;   // 2^2, even if 3 would suffice
constexpr uint32_t RANDOM_VALID_DEPOSIT_AMOUNTS = 16;
constexpr uint32_t RANDOM_MAX_USER_COMMITMENTS = 32;
constexpr uint32_t RANDOM_RANDOMBYTES_LEN = 32;

struct RANDOM2 {};

struct RANDOM_RecentMiner
{
	id     minerId;
	uint64 deposit;
	uint64 lastEntropyVersion;
	uint32 lastRevealTick;
};

struct RANDOM_EntropyCommitment
{
	id     digest;
	id     invocatorId;
	uint64 amount;
	uint32 commitTick;
	uint32 revealDeadlineTick;
	bool   hasRevealed;
};

struct RANDOM : public ContractBase
{
private:
	// --- QPI contract state ---
	Array<m256i, RANDOM_ENTROPY_HISTORY_LEN> entropyHistory;
	Array<uint64, RANDOM_ENTROPY_HISTORY_LEN> entropyPoolVersionHistory;
	uint32 entropyHistoryHead;

	m256i currentEntropyPool;
	uint64 entropyPoolVersion;

	uint64 totalCommits;
	uint64 totalReveals;
	uint64 totalSecurityDepositsLocked;

	uint64 minimumSecurityDeposit;
	uint32 revealTimeoutTicks;

	uint64 totalRevenue;
	uint64 pendingShareholderDistribution;
	uint64 lostDepositsRevenue;
	uint64 minerEarningsPool;
	uint64 shareholderEarningsPool;

	uint64 pricePerByte;
	uint64 priceDepositDivisor;

	Array<RANDOM_RecentMiner, RANDOM_MAX_RECENT_MINERS> recentMiners;
	uint32 recentMinerCount;

	Array<uint64, RANDOM_VALID_DEPOSIT_AMOUNTS> validDepositAmounts;

	Array<RANDOM_EntropyCommitment, RANDOM_MAX_COMMITMENTS> commitments;
	uint32 commitmentCount;

	// --- QPI-compliant helpers ---
	static inline void xorEntropy(m256i& dst, const bit_4096& src)
	{
		const Array<uint64, 4>& entropyData = *reinterpret_cast<const Array<uint64, 4>*>(&src);
		for (uint32 i = 0; i < 4; i++)
		{
			dst.u64._0 = dst.u64._0 ^ entropyData.get(0);
			dst.u64._1 = dst.u64._1 ^ entropyData.get(1);
			dst.u64._2 = dst.u64._2 ^ entropyData.get(2);
			dst.u64._3 = dst.u64._3 ^ entropyData.get(3);
			break; // Only need to do this once, as all 4 at once
		}
	}

	static inline void updateEntropyPoolData(RANDOM& stateRef, const bit_4096& newEntropy)
	{
		m256i newPool = stateRef.currentEntropyPool; // QPI: edit-copy-set
		xorEntropy(newPool, newEntropy);
		stateRef.entropyHistoryHead = (stateRef.entropyHistoryHead + 1) & (RANDOM_ENTROPY_HISTORY_LEN - 1);
		stateRef.currentEntropyPool = newPool;
		stateRef.entropyHistory.set(stateRef.entropyHistoryHead, newPool);

		stateRef.entropyPoolVersion++;
		stateRef.entropyPoolVersionHistory.set(stateRef.entropyHistoryHead, stateRef.entropyPoolVersion);
	}

	static inline void generateRandomBytesData(const RANDOM& stateRef, Array<uint8, RANDOM_RANDOMBYTES_LEN>& output, uint32 numBytes, uint32 historyIdx, uint32 currentTick)
	{
		const m256i selectedPool = stateRef.entropyHistory.get(
			(stateRef.entropyHistoryHead + RANDOM_ENTROPY_HISTORY_LEN - historyIdx) & (RANDOM_ENTROPY_HISTORY_LEN - 1)
		);

		m256i tickEntropy;
		tickEntropy.u64._0 = static_cast<uint64_t>(currentTick);
		tickEntropy.u64._1 = 0;
		tickEntropy.u64._2 = 0;
		tickEntropy.u64._3 = 0;

		m256i combinedEntropy = selectedPool;
		combinedEntropy.u64._0 ^= tickEntropy.u64._0;
		combinedEntropy.u64._1 ^= tickEntropy.u64._1;
		combinedEntropy.u64._2 ^= tickEntropy.u64._2;
		combinedEntropy.u64._3 ^= tickEntropy.u64._3;

		for (uint32 i = 0; i < ((numBytes > RANDOM_RANDOMBYTES_LEN) ? RANDOM_RANDOMBYTES_LEN : numBytes); i++)
		{
			output.set(i, combinedEntropy.m256i_u8[i]);
		}
	}

	static inline bool isValidDepositAmountCheck(const RANDOM& stateRef, uint64 amount)
	{
		for (uint32 i = 0; i < RANDOM_VALID_DEPOSIT_AMOUNTS; i++)
		{
			if (amount == stateRef.validDepositAmounts.get(i))
			{
				return true;
			}
		}
		return false;
	}

	static inline bool isEqualIdCheck(const id& a, const id& b)
	{
		return a == b;
	}

	static inline bool isZeroIdCheck(const id& value)
	{
		return isZero(value);
	}

	static inline bool isZeroBitsCheck(const bit_4096& value)
	{
		const Array<uint64, 64>& data = *reinterpret_cast<const Array<uint64, 64>*>(&value);
		for (uint32 i = 0; i < 64; i++)
		{
			if (data.get(i) != 0)
			{
				return false;
			}
		}
		return true;
	}

	static inline bool k12CommitmentMatches(
		const QPI::QpiContextFunctionCall& qpi,
		const QPI::bit_4096& revealedBits,
		const QPI::id& committedDigest)
	{
		QPI::id computedDigest = qpi.K12(revealedBits);
		return computedDigest == committedDigest;
	}

public:
	struct RevealAndCommit_input
	{
		bit_4096 revealedBits;
		id committedDigest;
	};
	struct RevealAndCommit_output
	{
		Array<uint8, RANDOM_RANDOMBYTES_LEN> randomBytes;
		uint64 entropyVersion;
		bool   revealSuccessful;
		bool   commitSuccessful;
		uint64 depositReturned;
	};

	struct GetContractInfo_input {};
	struct GetContractInfo_output
	{
		uint64 totalCommits;
		uint64 totalReveals;
		uint64 totalSecurityDepositsLocked;
		uint64 minimumSecurityDeposit;
		uint32 revealTimeoutTicks;
		uint32 activeCommitments;
		Array<uint64, RANDOM_VALID_DEPOSIT_AMOUNTS> validDepositAmounts;
		uint32 currentTick;
		uint64 entropyPoolVersion;
		uint64 totalRevenue;
		uint64 pendingShareholderDistribution;
		uint64 lostDepositsRevenue;
		uint64 minerEarningsPool;
		uint64 shareholderEarningsPool;
		uint32 recentMinerCount;
	};

	struct GetUserCommitments_input
	{
		id userId;
	};
	struct GetUserCommitments_output
	{
		struct UserCommitment
		{
			id digest;
			uint64 amount;
			uint32 commitTick;
			uint32 revealDeadlineTick;
			bool hasRevealed;
		};
		Array<UserCommitment, RANDOM_MAX_USER_COMMITMENTS> commitments;
		uint32 commitmentCount;
	};

	struct BuyEntropy_input
	{
		uint32 numberOfBytes;
		uint64 minMinerDeposit;
	};
	struct BuyEntropy_output
	{
		bool   success;
		Array<uint8, RANDOM_RANDOMBYTES_LEN> randomBytes;
		uint64 entropyVersion;
		uint64 usedMinerDeposit;
		uint64 usedPoolVersion;
	};

	struct QueryPrice_input { uint32 numberOfBytes; uint64 minMinerDeposit; };
	struct QueryPrice_output { uint64 price; };

	struct RevealAndCommit_locals
	{
		uint32 currentTick;
		bool hasRevealData;
		bool hasNewCommit;
		bool isStoppingMining;
		sint32 existingIndex;
		uint32 i;
		uint32 rm;
		uint32 lowestIx;
		bool hashMatches;
	};
	struct BuyEntropy_locals
	{
		uint32 currentTick;
		bool eligible;
		uint64 usedMinerDeposit;
		uint32 i;
		uint64 minPrice;
		uint64 buyerFee;
		uint32 histIdx;
		uint64 half;
	};
	struct END_EPOCH_locals
	{
		uint32 currentTick;
		uint32 i;
		uint64 payout;
	};

	// --------------------------------------------------
	PUBLIC_PROCEDURE_WITH_LOCALS(RevealAndCommit)
	{
		locals.currentTick = qpi.tick();

		// Remove expired commitments
		for (locals.i = 0; locals.i < state.commitmentCount;)
		{
			RANDOM_EntropyCommitment cmt = state.commitments.get(locals.i);
			if (!cmt.hasRevealed && locals.currentTick > cmt.revealDeadlineTick)
			{
				uint64 lostDeposit = cmt.amount;
				state.lostDepositsRevenue += lostDeposit;
				state.totalRevenue += lostDeposit;
				state.pendingShareholderDistribution += lostDeposit;
				state.totalSecurityDepositsLocked -= lostDeposit;
				if (locals.i != state.commitmentCount - 1)
				{
					RANDOM_EntropyCommitment last = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, last);
				}
				state.commitmentCount--;
			}
			else
			{
				locals.i++;
			}
		}

		// Early epoch: forcibly return expired
		if (qpi.numberOfTickTransactions() == -1)
		{
			for (locals.i = 0; locals.i < state.commitmentCount;)
			{
				RANDOM_EntropyCommitment cmt = state.commitments.get(locals.i);
				if (!cmt.hasRevealed && cmt.revealDeadlineTick == qpi.tick())
				{
					qpi.transfer(cmt.invocatorId, cmt.amount);
					state.totalSecurityDepositsLocked -= cmt.amount;
					if (locals.i != state.commitmentCount - 1)
					{
						RANDOM_EntropyCommitment last = state.commitments.get(state.commitmentCount - 1);
						state.commitments.set(locals.i, last);
					}
					state.commitmentCount--;
				}
				else
				{
					locals.i++;
				}
			}
			return;
		}

		locals.hasRevealData = !isZeroBitsCheck(input.revealedBits);
		locals.hasNewCommit = !isZeroIdCheck(input.committedDigest);
		locals.isStoppingMining = (qpi.invocationReward() == 0);

		if (locals.hasRevealData)
		{
			for (locals.i = 0; locals.i < state.commitmentCount;)
			{
				RANDOM_EntropyCommitment cmt = state.commitments.get(locals.i);
				if (!cmt.hasRevealed && isEqualIdCheck(cmt.invocatorId, qpi.invocator()))
				{
					locals.hashMatches = k12CommitmentMatches(qpi, input.revealedBits, cmt.digest);

					if (locals.hashMatches)
					{
						if (locals.currentTick > cmt.revealDeadlineTick)
						{
							uint64 lostDeposit = cmt.amount;
							state.lostDepositsRevenue += lostDeposit;
							state.totalRevenue += lostDeposit;
							state.pendingShareholderDistribution += lostDeposit;
							output.revealSuccessful = false;
						}
						else
						{
							updateEntropyPoolData(state, input.revealedBits);
							qpi.transfer(qpi.invocator(), cmt.amount);
							output.revealSuccessful = true;
							output.depositReturned = cmt.amount;
							state.totalReveals++;
							state.totalSecurityDepositsLocked -= cmt.amount;

							// Maintain LRU recentMiner
							locals.existingIndex = -1;
							for (locals.rm = 0; locals.rm < state.recentMinerCount; ++locals.rm)
							{
								RANDOM_RecentMiner test = state.recentMiners.get(locals.rm);
								if (isEqualIdCheck(test.minerId, qpi.invocator()))
								{
									locals.existingIndex = locals.rm;
									break;
								}
							}
							if (locals.existingIndex >= 0)
							{
								RANDOM_RecentMiner rm = state.recentMiners.get(locals.existingIndex);
								if (rm.deposit < cmt.amount)
								{
									rm.deposit = cmt.amount;
									rm.lastEntropyVersion = state.entropyPoolVersion;
								}
								rm.lastRevealTick = locals.currentTick;
								state.recentMiners.set(locals.existingIndex, rm);
							}
							else if (state.recentMinerCount < RANDOM_MAX_RECENT_MINERS)
							{
								RANDOM_RecentMiner rm;
								rm.minerId = qpi.invocator();
								rm.deposit = cmt.amount;
								rm.lastEntropyVersion = state.entropyPoolVersion;
								rm.lastRevealTick = locals.currentTick;
								state.recentMiners.set(state.recentMinerCount, rm);
								state.recentMinerCount++;
							}
							else
							{
								locals.lowestIx = 0;
								for (locals.rm = 1; locals.rm < RANDOM_MAX_RECENT_MINERS; ++locals.rm)
								{
									RANDOM_RecentMiner test = state.recentMiners.get(locals.rm);
									RANDOM_RecentMiner lo = state.recentMiners.get(locals.lowestIx);
									if (test.deposit < lo.deposit ||
										(test.deposit == lo.deposit && test.lastEntropyVersion < lo.lastEntropyVersion))
									{
										locals.lowestIx = locals.rm;
									}
								}
								RANDOM_RecentMiner rm = state.recentMiners.get(locals.lowestIx);
								if (
									cmt.amount > rm.deposit ||
									(cmt.amount == rm.deposit && state.entropyPoolVersion > rm.lastEntropyVersion)
									)
								{
									rm.minerId = qpi.invocator();
									rm.deposit = cmt.amount;
									rm.lastEntropyVersion = state.entropyPoolVersion;
									rm.lastRevealTick = locals.currentTick;
									state.recentMiners.set(locals.lowestIx, rm);
								}
							}
						}

						state.totalSecurityDepositsLocked -= cmt.amount;
						if (locals.i != state.commitmentCount - 1)
						{
							RANDOM_EntropyCommitment last = state.commitments.get(state.commitmentCount - 1);
							state.commitments.set(locals.i, last);
						}
						state.commitmentCount--;
						continue;
					}
				}
				locals.i++;
			}
		}

		if (locals.hasNewCommit && !locals.isStoppingMining)
		{
			if (isValidDepositAmountCheck(state, qpi.invocationReward())
				&& qpi.invocationReward() >= state.minimumSecurityDeposit)
			{
				if (state.commitmentCount < RANDOM_MAX_COMMITMENTS)
				{
					RANDOM_EntropyCommitment ncmt;
					ncmt.digest = input.committedDigest;
					ncmt.invocatorId = qpi.invocator();
					ncmt.amount = qpi.invocationReward();
					ncmt.commitTick = locals.currentTick;
					ncmt.revealDeadlineTick = locals.currentTick + state.revealTimeoutTicks;
					ncmt.hasRevealed = false;
					state.commitments.set(state.commitmentCount, ncmt);
					state.commitmentCount++;
					state.totalCommits++;
					state.totalSecurityDepositsLocked += qpi.invocationReward();
					output.commitSuccessful = true;
				}
			}
		}

		generateRandomBytesData(state, output.randomBytes, RANDOM_RANDOMBYTES_LEN, 0, locals.currentTick);
		output.entropyVersion = state.entropyPoolVersion;
	}

	PUBLIC_PROCEDURE_WITH_LOCALS(BuyEntropy)
	{
		locals.currentTick = qpi.tick();

		for (locals.i = 0; locals.i < state.commitmentCount;)
		{
			RANDOM_EntropyCommitment cmt = state.commitments.get(locals.i);
			if (!cmt.hasRevealed && locals.currentTick > cmt.revealDeadlineTick)
			{
				uint64 lostDeposit = cmt.amount;
				state.lostDepositsRevenue += lostDeposit;
				state.totalRevenue += lostDeposit;
				state.pendingShareholderDistribution += lostDeposit;
				state.totalSecurityDepositsLocked -= lostDeposit;
				if (locals.i != state.commitmentCount - 1)
				{
					RANDOM_EntropyCommitment last = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, last);
				}
				state.commitmentCount--;
			}
			else
			{
				locals.i++;
			}
		}

		if (qpi.numberOfTickTransactions() == -1)
		{
			output.success = false;
			return;
		}

		output.success = false;
		locals.buyerFee = qpi.invocationReward();
		locals.eligible = false;
		locals.usedMinerDeposit = 0;

		for (locals.i = 0; locals.i < state.recentMinerCount; ++locals.i)
		{
			RANDOM_RecentMiner rm = state.recentMiners.get(locals.i);
			if (rm.deposit >= input.minMinerDeposit &&
				(locals.currentTick - rm.lastRevealTick) <= state.revealTimeoutTicks)
			{
				locals.eligible = true;
				locals.usedMinerDeposit = rm.deposit;
				break;
			}
		}

		if (!locals.eligible)
		{
			return;
		}

		locals.minPrice = state.pricePerByte * input.numberOfBytes *
			(div(input.minMinerDeposit, state.priceDepositDivisor) + 1);

		if (locals.buyerFee < locals.minPrice)
		{
			return;
		}

		locals.histIdx = (state.entropyHistoryHead + RANDOM_ENTROPY_HISTORY_LEN - 2) & (RANDOM_ENTROPY_HISTORY_LEN - 1);
		generateRandomBytesData(
			state,
			output.randomBytes,
			(input.numberOfBytes > RANDOM_RANDOMBYTES_LEN ? RANDOM_RANDOMBYTES_LEN : input.numberOfBytes),
			locals.histIdx,
			locals.currentTick
		);

		output.entropyVersion = state.entropyPoolVersionHistory.get(locals.histIdx);
		output.usedMinerDeposit = locals.usedMinerDeposit;
		output.usedPoolVersion = state.entropyPoolVersionHistory.get(locals.histIdx);
		output.success = true;

		locals.half = div(locals.buyerFee, (uint64)2);
		state.minerEarningsPool += locals.half;
		state.shareholderEarningsPool += (locals.buyerFee - locals.half);
	}

	PUBLIC_FUNCTION(GetContractInfo)
	{
		uint32 currentTick = qpi.tick();
		uint32 activeCount = 0;
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

		for (uint32 i = 0; i < RANDOM_VALID_DEPOSIT_AMOUNTS; ++i)
		{
			output.validDepositAmounts.set(i, state.validDepositAmounts.get(i));
		}
		for (uint32 i = 0; i < state.commitmentCount; ++i)
		{
			if (!state.commitments.get(i).hasRevealed)
			{
				activeCount++;
			}
		}
		output.activeCommitments = activeCount;
	}

	PUBLIC_FUNCTION(GetUserCommitments)
	{
		uint32 userCommitmentCount = 0;
		for (uint32 i = 0; i < state.commitmentCount && userCommitmentCount < RANDOM_MAX_USER_COMMITMENTS; i++)
		{
			RANDOM_EntropyCommitment cmt = state.commitments.get(i);
			if (isEqualIdCheck(cmt.invocatorId, input.userId))
			{
				GetUserCommitments_output::UserCommitment ucmt;
				ucmt.digest = cmt.digest;
				ucmt.amount = cmt.amount;
				ucmt.commitTick = cmt.commitTick;
				ucmt.revealDeadlineTick = cmt.revealDeadlineTick;
				ucmt.hasRevealed = cmt.hasRevealed;
				output.commitments.set(userCommitmentCount, ucmt);
				userCommitmentCount++;
			}
		}
		output.commitmentCount = userCommitmentCount;
	}

	PUBLIC_FUNCTION(QueryPrice)
	{
		output.price = state.pricePerByte * input.numberOfBytes *
			(div(input.minMinerDeposit, (uint64)state.priceDepositDivisor) + 1);
	}

	END_EPOCH_WITH_LOCALS()
	{
		locals.currentTick = qpi.tick();
		for (locals.i = 0; locals.i < state.commitmentCount;)
		{
			RANDOM_EntropyCommitment cmt = state.commitments.get(locals.i);
			if (!cmt.hasRevealed && locals.currentTick > cmt.revealDeadlineTick)
			{
				uint64 lostDeposit = cmt.amount;
				state.lostDepositsRevenue += lostDeposit;
				state.totalRevenue += lostDeposit;
				state.pendingShareholderDistribution += lostDeposit;
				state.totalSecurityDepositsLocked -= lostDeposit;

				if (locals.i != state.commitmentCount - 1)
				{
					RANDOM_EntropyCommitment last = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, last);
				}
				state.commitmentCount--;
			}
			else
			{
				locals.i++;
			}
		}

		if (state.minerEarningsPool > 0 && state.recentMinerCount > 0)
		{
			locals.payout = div(state.minerEarningsPool, (uint64)state.recentMinerCount);
			for (locals.i = 0; locals.i < state.recentMinerCount; ++locals.i)
			{
				RANDOM_RecentMiner rm = state.recentMiners.get(locals.i);
				if (!isZeroIdCheck(rm.minerId))
				{
					qpi.transfer(rm.minerId, locals.payout);
				}
			}
			state.minerEarningsPool = 0;
			for (locals.i = 0; locals.i < RANDOM_MAX_RECENT_MINERS; ++locals.i)
			{
				state.recentMiners.set(locals.i, RANDOM_RecentMiner{});
			}
			state.recentMinerCount = 0;
		}
		if (state.shareholderEarningsPool > 0)
		{
			qpi.distributeDividends(div(state.shareholderEarningsPool, (uint64)NUMBER_OF_COMPUTORS));
			state.shareholderEarningsPool = 0;
		}
		if (state.pendingShareholderDistribution > 0)
		{
			qpi.distributeDividends(div(state.pendingShareholderDistribution, (uint64)NUMBER_OF_COMPUTORS));
			state.pendingShareholderDistribution = 0;
		}
	}

	REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
	{
		REGISTER_USER_FUNCTION(GetContractInfo, 1);
		REGISTER_USER_FUNCTION(GetUserCommitments, 2);
		REGISTER_USER_FUNCTION(QueryPrice, 3);

		REGISTER_USER_PROCEDURE(RevealAndCommit, 1);
		REGISTER_USER_PROCEDURE(BuyEntropy, 2);
	}

	INITIALIZE()
	{
		state.entropyHistoryHead = 0;
		state.minimumSecurityDeposit = 1;
		state.revealTimeoutTicks = 9;
		state.pricePerByte = 10;
		state.priceDepositDivisor = 1000;

		for (uint32 i = 0; i < RANDOM_VALID_DEPOSIT_AMOUNTS; ++i)
		{
			uint64 val = 1ULL;
			for (uint32 j = 0; j < i; ++j)
			{
				val *= 10;
			}
			state.validDepositAmounts.set(i, val);
		}
	}
};
