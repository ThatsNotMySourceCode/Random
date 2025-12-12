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
	// We'll compute K12 digests (id) from bit_4096 where needed (qpi.K12) and
	// operate on id (256-bit) which fits m256i/id layout.

	static inline bool validDepositAmountAt(const RANDOM& stateRef, uint64 amount, uint32 idx)
	{
		return amount == stateRef.validDepositAmounts.get(idx);
	}

	static inline bool isEqualIdCheck(const id& a, const id& b)
	{
		return a == b;
	}

	static inline bool isZeroIdCheck(const id& value)
	{
		return isZero(value);
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

		// added locals for inlined random-bytes generation (no stack locals)
		uint32 histIdx;
		uint32 rb_i;

		// precompute K12 digest of revealedBits once per call to avoid casts
		id revealedDigest;

		// move per-iteration commitment here (no stack-local)
		RANDOM_EntropyCommitment cmt;

		// per-iteration temporaries moved into locals
		uint64 lostDeposit;
		RANDOM_RecentMiner recentMinerA;
		RANDOM_RecentMiner recentMinerB;

		// deposit validity flag moved into locals
		bool depositValid;

		// per-commit creation temporary (moved out of stack)
		RANDOM_EntropyCommitment ncmt;
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

		// per-iteration commitment
		RANDOM_EntropyCommitment cmt;

		// per-iteration temporaries
		uint64 lostDeposit;
		RANDOM_RecentMiner recentMinerTemp;
	};
	struct END_EPOCH_locals
	{
		uint32 currentTick;
		uint32 i;
		uint64 payout;

		// per-iteration commitment
		RANDOM_EntropyCommitment cmt;

		// per-iteration temporaries
		uint64 lostDeposit;
		RANDOM_RecentMiner recentMinerTemp;
	};
	struct GetUserCommitments_locals
	{
		uint32 userCommitmentCount;
		uint32 i;

		// per-iteration commitment
		RANDOM_EntropyCommitment cmt;

		// per-iteration output item
		GetUserCommitments_output::UserCommitment ucmt;
	};
	struct GetContractInfo_locals
	{
		uint32 currentTick;
		uint32 activeCount;
		uint32 i;
	};
	struct INITIALIZE_locals
	{
		uint32 i;
		uint32 j;
		uint64 val;
	};

	// --------------------------------------------------
	PUBLIC_PROCEDURE_WITH_LOCALS(RevealAndCommit)
	{
		locals.currentTick = qpi.tick();

		// Remove expired commitments
		for (locals.i = 0; locals.i < state.commitmentCount;)
		{
			locals.cmt = state.commitments.get(locals.i);
			if (!locals.cmt.hasRevealed && locals.currentTick > locals.cmt.revealDeadlineTick)
			{
				locals.lostDeposit = locals.cmt.amount;
				state.lostDepositsRevenue += locals.lostDeposit;
				state.totalRevenue += locals.lostDeposit;
				state.pendingShareholderDistribution += locals.lostDeposit;
				state.totalSecurityDepositsLocked -= locals.lostDeposit;
				if (locals.i != state.commitmentCount - 1)
				{
					locals.cmt = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, locals.cmt);
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
				locals.cmt = state.commitments.get(locals.i);
				if (!locals.cmt.hasRevealed && locals.cmt.revealDeadlineTick == qpi.tick())
				{
					qpi.transfer(locals.cmt.invocatorId, locals.cmt.amount);
					state.totalSecurityDepositsLocked -= locals.cmt.amount;
					if (locals.i != state.commitmentCount - 1)
					{
						locals.cmt = state.commitments.get(state.commitmentCount - 1);
						state.commitments.set(locals.i, locals.cmt);
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

		// Precompute digest of revealedBits once (avoid inspecting bit_4096 internals)
		locals.revealedDigest = qpi.K12(input.revealedBits);
		// Treat presence of a reveal as attempt to match digest
		locals.hasRevealData = true;
		locals.hasNewCommit = !isZeroIdCheck(input.committedDigest);
		locals.isStoppingMining = (qpi.invocationReward() == 0);

		if (locals.hasRevealData)
		{
			for (locals.i = 0; locals.i < state.commitmentCount;)
			{
				locals.cmt = state.commitments.get(locals.i);
				if (!locals.cmt.hasRevealed && isEqualIdCheck(locals.cmt.invocatorId, qpi.invocator()))
				{
					// Match using precomputed K12 digest
					locals.hashMatches = (locals.cmt.digest == locals.revealedDigest);

					if (locals.hashMatches)
					{
						if (locals.currentTick > locals.cmt.revealDeadlineTick)
						{
							locals.lostDeposit = locals.cmt.amount;
							state.lostDepositsRevenue += locals.lostDeposit;
							state.totalRevenue += locals.lostDeposit;
							state.pendingShareholderDistribution += locals.lostDeposit;
							output.revealSuccessful = false;
						}
						else
						{
							// XOR the 256-bit digest (id) into the currentEntropyPool (m256i).
							state.currentEntropyPool.u64._0 ^= locals.revealedDigest.u64._0;
							state.currentEntropyPool.u64._1 ^= locals.revealedDigest.u64._1;
							state.currentEntropyPool.u64._2 ^= locals.revealedDigest.u64._2;
							state.currentEntropyPool.u64._3 ^= locals.revealedDigest.u64._3;

							// advance history
							state.entropyHistoryHead = (state.entropyHistoryHead + 1) & (RANDOM_ENTROPY_HISTORY_LEN - 1);
							state.entropyHistory.set(state.entropyHistoryHead, state.currentEntropyPool);

							state.entropyPoolVersion++;
							state.entropyPoolVersionHistory.set(state.entropyHistoryHead, state.entropyPoolVersion);

							qpi.transfer(qpi.invocator(), locals.cmt.amount);
							output.revealSuccessful = true;
							output.depositReturned = locals.cmt.amount;
							state.totalReveals++;
							state.totalSecurityDepositsLocked -= locals.cmt.amount;

							// Maintain LRU recentMiner
							locals.existingIndex = -1;
							for (locals.rm = 0; locals.rm < state.recentMinerCount; ++locals.rm)
							{
								locals.recentMinerA = state.recentMiners.get(locals.rm);
								if (isEqualIdCheck(locals.recentMinerA.minerId, qpi.invocator()))
								{
									locals.existingIndex = locals.rm;
									break;
								}
							}
							if (locals.existingIndex >= 0)
							{
								locals.recentMinerA = state.recentMiners.get(locals.existingIndex);
								if (locals.recentMinerA.deposit < locals.cmt.amount)
								{
									locals.recentMinerA.deposit = locals.cmt.amount;
									locals.recentMinerA.lastEntropyVersion = state.entropyPoolVersion;
								}
								locals.recentMinerA.lastRevealTick = locals.currentTick;
								state.recentMiners.set(locals.existingIndex, locals.recentMinerA);
							}
							else if (state.recentMinerCount < RANDOM_MAX_RECENT_MINERS)
							{
								locals.recentMinerA.minerId = qpi.invocator();
								locals.recentMinerA.deposit = locals.cmt.amount;
								locals.recentMinerA.lastEntropyVersion = state.entropyPoolVersion;
								locals.recentMinerA.lastRevealTick = locals.currentTick;
								state.recentMiners.set(state.recentMinerCount, locals.recentMinerA);
								state.recentMinerCount++;
							}
							else
							{
								locals.lowestIx = 0;
								for (locals.rm = 1; locals.rm < RANDOM_MAX_RECENT_MINERS; ++locals.rm)
								{
									locals.recentMinerA = state.recentMiners.get(locals.rm);
									locals.recentMinerB = state.recentMiners.get(locals.lowestIx);
									if (locals.recentMinerA.deposit < locals.recentMinerB.deposit ||
										(locals.recentMinerA.deposit == locals.recentMinerB.deposit && locals.recentMinerA.lastEntropyVersion < locals.recentMinerB.lastEntropyVersion))
									{
										locals.lowestIx = locals.rm;
									}
								}
								locals.recentMinerA = state.recentMiners.get(locals.lowestIx);
								if (
									locals.cmt.amount > locals.recentMinerA.deposit ||
									(locals.cmt.amount == locals.recentMinerA.deposit && state.entropyPoolVersion > locals.recentMinerA.lastEntropyVersion)
									)
								{
									locals.recentMinerA.minerId = qpi.invocator();
									locals.recentMinerA.deposit = locals.cmt.amount;
									locals.recentMinerA.lastEntropyVersion = state.entropyPoolVersion;
									locals.recentMinerA.lastRevealTick = locals.currentTick;
									state.recentMiners.set(locals.lowestIx, locals.recentMinerA);
								}
							}
						}

						state.totalSecurityDepositsLocked -= locals.cmt.amount;
						if (locals.i != state.commitmentCount - 1)
						{
							locals.cmt = state.commitments.get(state.commitmentCount - 1);
							state.commitments.set(locals.i, locals.cmt);
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
			// Inline deposit validity check using locals (no loop inside helper).
			locals.depositValid = false;
			for (locals.i = 0; locals.i < RANDOM_VALID_DEPOSIT_AMOUNTS; ++locals.i)
			{
				if (validDepositAmountAt(state, qpi.invocationReward(), locals.i))
				{
					locals.depositValid = true;
					break;
				}
			}

			if (locals.depositValid && qpi.invocationReward() >= state.minimumSecurityDeposit)
			{
				if (state.commitmentCount < RANDOM_MAX_COMMITMENTS)
				{
					locals.ncmt.digest = input.committedDigest;
					locals.ncmt.invocatorId = qpi.invocator();
					locals.ncmt.amount = qpi.invocationReward();
					locals.ncmt.commitTick = locals.currentTick;
					locals.ncmt.revealDeadlineTick = locals.currentTick + state.revealTimeoutTicks;
					locals.ncmt.hasRevealed = false;
					state.commitments.set(state.commitmentCount, locals.ncmt);
					state.commitmentCount++;
					state.totalCommits++;
					state.totalSecurityDepositsLocked += qpi.invocationReward();
					output.commitSuccessful = true;
				}
			}
		}

		// Inlined generation of RANDOM_RANDOMBYTES_LEN random bytes without stack locals.
		locals.histIdx = (state.entropyHistoryHead + RANDOM_ENTROPY_HISTORY_LEN - 0) & (RANDOM_ENTROPY_HISTORY_LEN - 1);
		for (locals.rb_i = 0; locals.rb_i < RANDOM_RANDOMBYTES_LEN; ++locals.rb_i)
		{
			// Extract the correct 64-bit lane and then the requested byte without using plain [].
			output.randomBytes.set(
				locals.rb_i,
				static_cast<uint8_t>(
					(
						(
							(locals.rb_i < 8) ? state.entropyHistory.get(locals.histIdx).u64._0 :
							(locals.rb_i < 16) ? state.entropyHistory.get(locals.histIdx).u64._1 :
							(locals.rb_i < 24) ? state.entropyHistory.get(locals.histIdx).u64._2 :
												state.entropyHistory.get(locals.histIdx).u64._3
						) >> (8 * (locals.rb_i & 7))
					) & 0xFF
				) ^
				(locals.rb_i < 8 ? static_cast<uint8_t>((static_cast<uint64_t>(locals.currentTick) >> (8 * locals.rb_i)) & 0xFF) : 0)
			);
		}

		output.entropyVersion = state.entropyPoolVersion;
	}

	PUBLIC_PROCEDURE_WITH_LOCALS(BuyEntropy)
	{
		locals.currentTick = qpi.tick();

		for (locals.i = 0; locals.i < state.commitmentCount;)
		{
			locals.cmt = state.commitments.get(locals.i);
			if (!locals.cmt.hasRevealed && locals.currentTick > locals.cmt.revealDeadlineTick)
			{
				locals.lostDeposit = locals.cmt.amount;
				state.lostDepositsRevenue += locals.lostDeposit;
				state.totalRevenue += locals.lostDeposit;
				state.pendingShareholderDistribution += locals.lostDeposit;
				state.totalSecurityDepositsLocked -= locals.lostDeposit;
				if (locals.i != state.commitmentCount - 1)
				{
					locals.cmt = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, locals.cmt);
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
			locals.recentMinerTemp = state.recentMiners.get(locals.i);
			if (locals.recentMinerTemp.deposit >= input.minMinerDeposit &&
				(locals.currentTick - locals.recentMinerTemp.lastRevealTick) <= state.revealTimeoutTicks)
			{
				locals.eligible = true;
				locals.usedMinerDeposit = locals.recentMinerTemp.deposit;
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

		// Inlined generation of requested random bytes without stack locals.
		for (locals.i = 0; locals.i < ((input.numberOfBytes > RANDOM_RANDOMBYTES_LEN) ? RANDOM_RANDOMBYTES_LEN : input.numberOfBytes); ++locals.i)
		{
			output.randomBytes.set(
				locals.i,
				static_cast<uint8_t>(
					(
						(
							(locals.i < 8) ? state.entropyHistory.get(locals.histIdx).u64._0 :
							(locals.i < 16) ? state.entropyHistory.get(locals.histIdx).u64._1 :
							(locals.i < 24) ? state.entropyHistory.get(locals.histIdx).u64._2 :
											 state.entropyHistory.get(locals.histIdx).u64._3
						) >> (8 * (locals.i & 7))
					) & 0xFF
				) ^
				(locals.i < 8 ? static_cast<uint8_t>((static_cast<uint64_t>(locals.currentTick) >> (8 * locals.i)) & 0xFF) : 0)
			);
		}

		output.entropyVersion = state.entropyPoolVersionHistory.get(locals.histIdx);
		output.usedMinerDeposit = locals.usedMinerDeposit;
		output.usedPoolVersion = state.entropyPoolVersionHistory.get(locals.histIdx);
		output.success = true;

		locals.half = div(locals.buyerFee, (uint64)2);
		state.minerEarningsPool += locals.half;
		state.shareholderEarningsPool += (locals.buyerFee - locals.half);
	}

	PUBLIC_FUNCTION_WITH_LOCALS(GetContractInfo)
	{
		locals.currentTick = qpi.tick();
		locals.activeCount = 0;

		output.totalCommits = state.totalCommits;
		output.totalReveals = state.totalReveals;
		output.totalSecurityDepositsLocked = state.totalSecurityDepositsLocked;
		output.minimumSecurityDeposit = state.minimumSecurityDeposit;
		output.revealTimeoutTicks = state.revealTimeoutTicks;
		output.currentTick = locals.currentTick;
		output.entropyPoolVersion = state.entropyPoolVersion;

		output.totalRevenue = state.totalRevenue;
		output.pendingShareholderDistribution = state.pendingShareholderDistribution;
		output.lostDepositsRevenue = state.lostDepositsRevenue;
		output.minerEarningsPool = state.minerEarningsPool;
		output.shareholderEarningsPool = state.shareholderEarningsPool;
		output.recentMinerCount = state.recentMinerCount;

		for (locals.i = 0; locals.i < RANDOM_VALID_DEPOSIT_AMOUNTS; ++locals.i)
		{
			output.validDepositAmounts.set(locals.i, state.validDepositAmounts.get(locals.i));
		}
		for (locals.i = 0; locals.i < state.commitmentCount; ++locals.i)
		{
			if (!state.commitments.get(locals.i).hasRevealed)
			{
				locals.activeCount++;
			}
		}
		output.activeCommitments = locals.activeCount;
	}

	PUBLIC_FUNCTION_WITH_LOCALS(GetUserCommitments)
	{
		locals.userCommitmentCount = 0;
		for (locals.i = 0; locals.i < state.commitmentCount && locals.userCommitmentCount < RANDOM_MAX_USER_COMMITMENTS; ++locals.i)
		{
			locals.cmt = state.commitments.get(locals.i);
			if (isEqualIdCheck(locals.cmt.invocatorId, input.userId))
			{
				locals.ucmt.digest = locals.cmt.digest;
				locals.ucmt.amount = locals.cmt.amount;
				locals.ucmt.commitTick = locals.cmt.commitTick;
				locals.ucmt.revealDeadlineTick = locals.cmt.revealDeadlineTick;
				locals.ucmt.hasRevealed = locals.cmt.hasRevealed;
				output.commitments.set(locals.userCommitmentCount, locals.ucmt);
				locals.userCommitmentCount++;
			}
		}
		output.commitmentCount = locals.userCommitmentCount;
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
			locals.cmt = state.commitments.get(locals.i);
			if (!locals.cmt.hasRevealed && locals.currentTick > locals.cmt.revealDeadlineTick)
			{
				locals.lostDeposit = locals.cmt.amount;
				state.lostDepositsRevenue += locals.lostDeposit;
				state.totalRevenue += locals.lostDeposit;
				state.pendingShareholderDistribution += locals.lostDeposit;
				state.totalSecurityDepositsLocked -= locals.lostDeposit;

				if (locals.i != state.commitmentCount - 1)
				{
					locals.cmt = state.commitments.get(state.commitmentCount - 1);
					state.commitments.set(locals.i, locals.cmt);
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
				locals.recentMinerTemp = state.recentMiners.get(locals.i);
				if (!isZeroIdCheck(locals.recentMinerTemp.minerId))
				{
					qpi.transfer(locals.recentMinerTemp.minerId, locals.payout);
				}
			}
			state.minerEarningsPool = 0;
			for (locals.i = 0; locals.i < RANDOM_MAX_RECENT_MINERS; ++locals.i)
			{
				locals.recentMinerTemp.minerId = id::zero();
				locals.recentMinerTemp.deposit = 0;
				locals.recentMinerTemp.lastEntropyVersion = 0;
				locals.recentMinerTemp.lastRevealTick = 0;
				state.recentMiners.set(locals.i, locals.recentMinerTemp);
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

	INITIALIZE_WITH_LOCALS()
	{
		locals.i = 0;
		locals.j = 0;
		locals.val = 0;

		state.entropyHistoryHead = 0;
		state.minimumSecurityDeposit = 1;
		state.revealTimeoutTicks = 9;
		state.pricePerByte = 10;
		state.priceDepositDivisor = 1000;

		for (locals.i = 0; locals.i < RANDOM_VALID_DEPOSIT_AMOUNTS; ++locals.i)
		{
			locals.val = 1ULL;
			for (locals.j = 0; locals.j < locals.i; ++locals.j)
			{
				locals.val *= 10;
			}
			state.validDepositAmounts.set(locals.i, locals.val);
		}
	}
};
