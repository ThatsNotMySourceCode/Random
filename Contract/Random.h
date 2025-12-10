#pragma once

using namespace QPI;

// Max miners to consider for distribution
#define MAX_RECENT_MINERS 369
#define ENTROPY_HISTORY_LEN 3 // For 2-tick-back entropy pool

struct RANDOM_CONTRACT_STATE
{
    // Entropy pool history (circular buffer for look-back)
    m256i entropy_history[ENTROPY_HISTORY_LEN];
    uint64 entropy_pool_version_history[ENTROPY_HISTORY_LEN];
    uint32 entropy_history_head; // points to most recent

    // Global entropy pool - combines all revealed entropy
    m256i current_entropy_pool;
    uint64 entropy_pool_version;

    // Tracking statistics
    uint64 total_commits;
    uint64 total_reveals;
    uint64 total_security_deposits_locked;

    // Contract configuration
    uint64 minimum_security_deposit;
    uint32 reveal_timeout_ticks; // e.g. 9 ticks

    // Revenue distribution system
    uint64 total_revenue;
    uint64 pending_shareholder_distribution;
    uint64 lost_deposits_revenue;

    // Earnings pools
    uint64 miner_earnings_pool;
    uint64 shareholder_earnings_pool;

    // Pricing config
    uint64 price_per_byte;         // e.g. 10 QU (default)
    uint64 price_deposit_divisor;  // e.g. 1000 (matches contract formula)

    // Epoch tracking for miner rewards
    struct RecentMiner {
        id miner_id;
        uint64 deposit;
        uint64 last_entropy_version;
        uint32 last_reveal_tick;
    } recent_miners[MAX_RECENT_MINERS];
    uint32 recent_miner_count;

    // Valid deposit amounts (powers of 10)
    uint64 valid_deposit_amounts[16];

    // Commitment tracking
    struct EntropyCommitment {
        id digest;
        id invocator_id;
        uint64 amount;
        uint32 commit_tick;
        uint32 reveal_deadline_tick;
        bool has_revealed;
    } commitments[1024];
    uint32 commitment_count;
};

struct RANDOM : public ContractBase
{
    // --------------------------------------------------
    // Entropy mining (commit-reveal)
    struct RevealAndCommit_input
    {
        bit_4096 revealed_bits;   // Previous entropy to reveal (or zeros for first commit)
        id committed_digest;      // Hash of new entropy to commit (or zeros if stopping)
    };

    struct RevealAndCommit_output
    {
        uint8 random_bytes[32];
        uint64 entropy_version;
        bool reveal_successful;
        bool commit_successful;
        uint64 deposit_returned;
    };

    // --------------------------------------------------
    // READ-ONLY FUNCTIONS

    struct GetContractInfo_input {};
    struct GetContractInfo_output
    {
        uint64 total_commits;
        uint64 total_reveals;
        uint64 total_security_deposits_locked;
        uint64 minimum_security_deposit;
        uint32 reveal_timeout_ticks;
        uint32 active_commitments;
        uint64 valid_deposit_amounts[16];
        uint32 current_tick;
        uint64 entropy_pool_version;
        // Revenue + pools
        uint64 total_revenue;
        uint64 pending_shareholder_distribution;
        uint64 lost_deposits_revenue;
        uint64 miner_earnings_pool;
        uint64 shareholder_earnings_pool;
        uint32 recent_miner_count;
    };

    struct GetUserCommitments_input { id user_id; };
    struct GetUserCommitments_output
    {
        struct UserCommitment {
            id digest;
            uint64 amount;
            uint32 commit_tick;
            uint32 reveal_deadline_tick;
            bool has_revealed;
        } commitments[32];
        uint32 commitment_count;
    };

    // --------------------------------------------------
    // SELL ENTROPY (random bytes)
    struct BuyEntropy_input
    {
        uint32 number_of_bytes;      // 1-32
        uint64 min_miner_deposit;    // required deposit of recent miner
    };
    struct BuyEntropy_output
    {
        bool success;
        uint8 random_bytes[32];
        uint64 entropy_version; // version of pool 2 ticks ago!
        uint64 used_miner_deposit;
        uint64 used_pool_version;
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
        uint32 number_of_bytes;
        uint64 min_miner_deposit;
    };
    struct QueryPrice_output {
        uint64 price;
    };

    // --------------------------------------------------
    // Mining: RevealAndCommit 
    PUBLIC_PROCEDURE(RevealAndCommit)
    {
        process_timeouts();

        // Empty tick handling:
        if(qpi.numberOfTickTransactions() == -1) {
            for(uint32 i = 0; i < state.commitment_count; ) {
                if(!state.commitments[i].has_revealed &&
                   state.commitments[i].reveal_deadline_tick == qpi.tick())
                {
                    qpi.transfer(state.commitments[i].invocator_id, state.commitments[i].amount);
                    state.total_security_deposits_locked -= state.commitments[i].amount;
                    // Remove this slot by moving last in
                    if (i != state.commitment_count - 1)
                        state.commitments[i] = state.commitments[state.commitment_count - 1];
                    state.commitment_count--;
                    // Do not increment i, so the moved entry is checked next
                } else {
                    i++;
                }
            }
            qpi.setMem(&output, 0, sizeof(output));
            return;
        }

        const RevealAndCommit_input& input = qpi.input<RevealAndCommit_input>();
        id invocator_id = qpi.invocator();
        uint64 invocator_amount = qpi.invocationReward();
        uint32 current_tick = qpi.tick();
        qpi.setMem(&output, 0, sizeof(output));

        bool has_reveal_data = !is_zero_bits(input.revealed_bits);
        bool has_new_commit = !is_zero_id(input.committed_digest);
        bool is_stopping_mining = (invocator_amount == 0);

        // Step 1: Process reveal if provided
        if (has_reveal_data)
        {
            for (uint32 i = 0; i < state.commitment_count; ) {
                if (!state.commitments[i].has_revealed &&
                    is_equal_id(state.commitments[i].invocator_id, invocator_id))
                {
                    id computed_hash = compute_hash(input.revealed_bits);
                    if (is_equal_id(computed_hash, state.commitments[i].digest))
                    {
                        if (current_tick > state.commitments[i].reveal_deadline_tick)
                        {
                            uint64 lost_deposit = state.commitments[i].amount;
                            state.lost_deposits_revenue += lost_deposit;
                            state.total_revenue += lost_deposit;
                            state.pending_shareholder_distribution += lost_deposit;
                            output.reveal_successful = false;
                        }
                        else
                        {
                            update_entropy_pool(input.revealed_bits);

                            qpi.transfer(invocator_id, state.commitments[i].amount);

                            output.reveal_successful = true;
                            output.deposit_returned = state.commitments[i].amount;

                            state.total_reveals++;
                            state.total_security_deposits_locked -= state.commitments[i].amount;
                            // Update RecentMiners (with per-miner freshness)
                            int32 existing_index = -1;
                            for(uint32 rm = 0; rm < state.recent_miner_count; ++rm) {
                                if(is_equal_id(state.recent_miners[rm].miner_id, invocator_id)) {
                                    existing_index = rm;
                                    break;
                                }
                            }
                            if(existing_index >= 0) {
                                if(state.recent_miners[existing_index].deposit < state.commitments[i].amount) {
                                    state.recent_miners[existing_index].deposit = state.commitments[i].amount;
                                    state.recent_miners[existing_index].last_entropy_version = state.entropy_pool_version;
                                }
                                state.recent_miners[existing_index].last_reveal_tick = current_tick;
                            }
                            else {
                                if(state.recent_miner_count < MAX_RECENT_MINERS) {
                                    state.recent_miners[state.recent_miner_count].miner_id = invocator_id;
                                    state.recent_miners[state.recent_miner_count].deposit = state.commitments[i].amount;
                                    state.recent_miners[state.recent_miner_count].last_entropy_version = state.entropy_pool_version;
                                    state.recent_miners[state.recent_miner_count].last_reveal_tick = current_tick;
                                    state.recent_miner_count++;
                                }
                                else {
                                    // Overflow: evict
                                    uint32 lowest_ix = 0;
                                    for(uint32 rm = 1; rm < MAX_RECENT_MINERS; ++rm) {
                                        if(state.recent_miners[rm].deposit < state.recent_miners[lowest_ix].deposit ||
                                            (state.recent_miners[rm].deposit == state.recent_miners[lowest_ix].deposit &&
                                             state.recent_miners[rm].last_entropy_version < state.recent_miners[lowest_ix].last_entropy_version))
                                            lowest_ix = rm;
                                    }
                                    if(state.commitments[i].amount > state.recent_miners[lowest_ix].deposit ||
                                        (state.commitments[i].amount == state.recent_miners[lowest_ix].deposit &&
                                         state.entropy_pool_version > state.recent_miners[lowest_ix].last_entropy_version)) 
                                    {
                                        state.recent_miners[lowest_ix].miner_id = invocator_id;
                                        state.recent_miners[lowest_ix].deposit = state.commitments[i].amount;
                                        state.recent_miners[lowest_ix].last_entropy_version = state.entropy_pool_version;
                                        state.recent_miners[lowest_ix].last_reveal_tick = current_tick;
                                    }
                                }
                            }
                        }
                        // Compaction after reveal
                        state.total_security_deposits_locked -= state.commitments[i].amount;
                        if (i != state.commitment_count - 1)
                            state.commitments[i] = state.commitments[state.commitment_count - 1];
                        state.commitment_count--;
                        // do not increment i so new moved slot is checked
                        continue;
                    }
                }
                i++;
            }
        }

        // Step 2: Process new commitment
        if (has_new_commit && !is_stopping_mining)
        {
            if (is_valid_deposit_amount(invocator_amount) && invocator_amount >= state.minimum_security_deposit)
            {
                if (state.commitment_count < 1024)
                {
                    state.commitments[state.commitment_count].digest = input.committed_digest;
                    state.commitments[state.commitment_count].invocator_id = invocator_id;
                    state.commitments[state.commitment_count].amount = invocator_amount;
                    state.commitments[state.commitment_count].commit_tick = current_tick;
                    state.commitments[state.commitment_count].reveal_deadline_tick = current_tick + state.reveal_timeout_ticks;
                    state.commitments[state.commitment_count].has_revealed = false;
                    state.commitment_count++;
                    state.total_commits++;
                    state.total_security_deposits_locked += invocator_amount;
                    output.commit_successful = true;
                }
            }
        }

        // Always return random bytes (current pool)
        generate_random_bytes(output.random_bytes, 32, 0); // 0 = current pool
        output.entropy_version = state.entropy_pool_version;
    }

    // --------------------------------------------------
    // BUY ENTROPY / RANDOM BYTES 
    PUBLIC_PROCEDURE(BuyEntropy)
    {
        process_timeouts();
        if(qpi.numberOfTickTransactions() == -1) {
            qpi.setMem(&output, 0, sizeof(output));
            output.success = false;
            return;
        }
        const BuyEntropy_input& input = qpi.input<BuyEntropy_input>();
        qpi.setMem(&output, 0, sizeof(output));
        output.success = false;
        uint32 current_tick = qpi.tick();
        uint64 buyer_fee = qpi.invocationReward();

        bool eligible = false;
        uint64 used_miner_deposit = 0;
        for(uint32 i=0; i < state.recent_miner_count; ++i) {
            if(state.recent_miners[i].deposit >= input.min_miner_deposit && 
               (current_tick - state.recent_miners[i].last_reveal_tick) <= state.reveal_timeout_ticks) {
                eligible = true;
                used_miner_deposit = state.recent_miners[i].deposit;
                break;
            }
        }

        if(!eligible)
            return;

        uint64 min_price = state.price_per_byte
                        * input.number_of_bytes
                        * (input.min_miner_deposit / state.price_deposit_divisor + 1);
        if(buyer_fee < min_price)
            return;

        uint32 hist_idx = (state.entropy_history_head + ENTROPY_HISTORY_LEN - 2) % ENTROPY_HISTORY_LEN;
        generate_random_bytes(output.random_bytes, (input.number_of_bytes > 32 ? 32 : input.number_of_bytes), hist_idx);
        output.entropy_version = state.entropy_pool_version_history[hist_idx];
        output.used_miner_deposit = used_miner_deposit;
        output.used_pool_version  = state.entropy_pool_version_history[hist_idx];
        output.success = true;
        uint64 half = buyer_fee/2;
        state.miner_earnings_pool += half;
        state.shareholder_earnings_pool += (buyer_fee - half);
    }

    // --------------------------------------------------
    // Read-only contract info
    PUBLIC_FUNCTION(GetContractInfo)
    {
        uint32 current_tick = qpi.tick();

        output.total_commits = state.total_commits;
        output.total_reveals = state.total_reveals;
        output.total_security_deposits_locked = state.total_security_deposits_locked;
        output.minimum_security_deposit = state.minimum_security_deposit;
        output.reveal_timeout_ticks = state.reveal_timeout_ticks;
        output.current_tick = current_tick;
        output.entropy_pool_version = state.entropy_pool_version;

        output.total_revenue = state.total_revenue;
        output.pending_shareholder_distribution = state.pending_shareholder_distribution;
        output.lost_deposits_revenue = state.lost_deposits_revenue;

        output.miner_earnings_pool = state.miner_earnings_pool;
        output.shareholder_earnings_pool = state.shareholder_earnings_pool;
        output.recent_miner_count = state.recent_miner_count;

        for (uint32 i = 0; i < 16; i++)
            output.valid_deposit_amounts[i] = state.valid_deposit_amounts[i];

        uint32 active_count = 0;
        for (uint32 i = 0; i < state.commitment_count; i++)
            if (!state.commitments[i].has_revealed)
                active_count++;
        output.active_commitments = active_count;
    }

    PUBLIC_FUNCTION(GetUserCommitments)
    {
        const GetUserCommitments_input& input = qpi.input<GetUserCommitments_input>();

        qpi.setMem(&output, 0, sizeof(output));
        uint32 user_commitment_count = 0;
        for (uint32 i = 0; i < state.commitment_count && user_commitment_count < 32; i++)
        {
            if (is_equal_id(state.commitments[i].invocator_id, input.user_id))
            {
                output.commitments[user_commitment_count].digest = state.commitments[i].digest;
                output.commitments[user_commitment_count].amount = state.commitments[i].amount;
                output.commitments[user_commitment_count].commit_tick = state.commitments[i].commit_tick;
                output.commitments[user_commitment_count].reveal_deadline_tick = state.commitments[i].reveal_deadline_tick;
                output.commitments[user_commitment_count].has_revealed = state.commitments[i].has_revealed;
                user_commitment_count++;
            }
        }
        output.commitment_count = user_commitment_count;
    }

    PUBLIC_FUNCTION(QueryPrice)
    {
        const QueryPrice_input& input = qpi.input<QueryPrice_input>();
        output.price = state.price_per_byte
                    * input.number_of_bytes
                    * (input.min_miner_deposit / state.price_deposit_divisor + 1);
    }

    // --------------------------------------------------
    // Epoch End: Distribute pools
    void EndEpoch()
    {
        process_timeouts();

        // Distribute miner pool
        if(state.miner_earnings_pool > 0 && state.recent_miner_count > 0) {
            uint64 payout = state.miner_earnings_pool / state.recent_miner_count;
            for(uint32 i=0; i<state.recent_miner_count; ++i) {
                if(!is_zero_id(state.recent_miners[i].miner_id))
                    qpi.transfer(state.recent_miners[i].miner_id, payout);
            }
            state.miner_earnings_pool = 0;
            qpi.setMem(state.recent_miners, 0, sizeof(state.recent_miners));
            state.recent_miner_count = 0;
        }

        // Distribute to shareholders
        if(state.shareholder_earnings_pool > 0) {
            qpi.transferShareToShareOwners(state.shareholder_earnings_pool);
            state.shareholder_earnings_pool = 0;
        }

        // Continue current lost deposit distribution as before
        if (state.pending_shareholder_distribution > 0)
        {
            qpi.transferShareToShareOwners(state.pending_shareholder_distribution);
            state.pending_shareholder_distribution = 0;
        }
    }

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
        state.entropy_history_head = 0;
        for (uint32 i = 0; i < ENTROPY_HISTORY_LEN; ++i) {
            qpi.setMem(&state.entropy_history[i], 0, sizeof(m256i));
            state.entropy_pool_version_history[i] = 0;
        }
        qpi.setMem(&state.current_entropy_pool, 0, sizeof(m256i));
        state.entropy_pool_version = 0;
        state.total_commits = 0;
        state.total_reveals = 0;
        state.total_security_deposits_locked = 0;
        state.minimum_security_deposit = 1; // Now allow 1 QU
        state.reveal_timeout_ticks = 9;
        state.commitment_count = 0;
        state.total_revenue = 0;
        state.pending_shareholder_distribution = 0;
        state.lost_deposits_revenue = 0;
        state.miner_earnings_pool = 0;
        state.shareholder_earnings_pool = 0;
        state.recent_miner_count = 0;
        state.price_per_byte = 10;
        state.price_deposit_divisor = 1000;
        for (uint32 i = 0; i < 16; i++) {
            state.valid_deposit_amounts[i] = 1ULL;
            for (uint32 j = 0; j < i; j++)
                state.valid_deposit_amounts[i] *= 10;
        }
        qpi.setMem(state.commitments, 0, sizeof(state.commitments));
        qpi.setMem(state.recent_miners, 0, sizeof(state.recent_miners));
    }

private:
    // -- Helper functions

    void update_entropy_pool(const bit_4096& new_entropy)
    {
        // XOR new entropy into the global pool
        uint32 i;
        for (i = 0; i < 4; i++)
            state.current_entropy_pool.m256i_u64[i] ^= new_entropy.data[i];

        // Update entropy history (circular buffer)
        state.entropy_history_head = (state.entropy_history_head + 1U) % ENTROPY_HISTORY_LEN;
        state.entropy_history[state.entropy_history_head] = state.current_entropy_pool;
        state.entropy_pool_version++;
        state.entropy_pool_version_history[state.entropy_history_head] = state.entropy_pool_version;
    }

    void generate_random_bytes(uint8* output, uint32 num_bytes, uint32 history_idx)
    {
        const m256i selected_pool = state.entropy_history[(state.entropy_history_head + ENTROPY_HISTORY_LEN - history_idx) % ENTROPY_HISTORY_LEN];
        m256i tick_entropy = { qpi.tick(), qpi.tick() >> 32, 0, 0 };
        m256i combined_entropy;
        uint32 i;
        for (i = 0; i < 4; i++) combined_entropy.m256i_u64[i] = selected_pool.m256i_u64[i] ^ tick_entropy.m256i_u64[i];
        const id final_hash = compute_hash_from_m256i(combined_entropy);
        qpi.copyMem(output, final_hash.bytes, (num_bytes > 32U) ? 32U : num_bytes);
    }

    void process_timeouts() {
        uint32 current_tick = qpi.tick();
        for (uint32 i = 0; i < state.commitment_count; ) {
            if (!state.commitments[i].has_revealed &&
                current_tick > state.commitments[i].reveal_deadline_tick)
            {
                uint64 lost_deposit = state.commitments[i].amount;
                state.lost_deposits_revenue += lost_deposit;
                state.total_revenue += lost_deposit;
                state.pending_shareholder_distribution += lost_deposit;
                state.total_security_deposits_locked -= lost_deposit;
                if (i != state.commitment_count - 1)
                    state.commitments[i] = state.commitments[state.commitment_count - 1];
                state.commitment_count--;
            } else {
                i++;
            }
        }
    }

    bool is_valid_deposit_amount(uint64 amount)
    {
        uint32 i;
        for (i = 0; i < 16U; i++) if (amount == state.valid_deposit_amounts[i]) return true;
        return false;
    }

    id compute_hash(const bit_4096& data)
    {
        id result;
        qpi.computeHash(&data, sizeof(data), &result);
        return result;
    }

    id compute_hash_from_m256i(const m256i& data)
    {
        id result;
        qpi.computeHash(&data, sizeof(data), &result);
        return result;
    }

    bool is_equal_id(const id& a, const id& b)
    {
        uint32 i;
        for (i = 0; i < 32U; i++) if (a.bytes[i] != b.bytes[i]) return false;
        return true;
    }

    bool is_zero_id(const id& value)
    {
        uint32 i;
        for (i = 0; i < 32U; i++) if (value.bytes[i] != 0) return false;
        return true;
    }

    bool is_zero_bits(const bit_4096& value)
    {
        uint32 i;
        for (i = 0; i < 64U; i++) if (value.data[i] != 0) return false;
        return true;
    }
};
