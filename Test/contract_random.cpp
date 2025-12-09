#define NO_UEFI

#include "contract_testing.h"
#include "Random.h"

static constexpr int RANDOM_CONTRACT_INDEX = 0; // Adjust as needed

class RandomChecker : public RANDOM
{
    // Add helper state queries or checks for contract invariants here if desired
};

class ContractTestingRandom : protected ContractTesting
{
public:
    ContractTestingRandom()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        INIT_CONTRACT(RANDOM);
        callSystemProcedure(RANDOM_CONTRACT_INDEX, INITIALIZE);
    }

    RandomChecker* getState()
    {
        return (RandomChecker*)contractStates[RANDOM_CONTRACT_INDEX];
    }

    void setRandomBalance(const id& user, int64_t amount)
    {
        setBalance(user, amount);
    }

    // Commit+reveal convenience
    void commit(const id& miner, const bit_4096& commitBits, uint64_t deposit)
    {
        setActiveUser(miner);
        setBalance(miner, getBalance(miner) + deposit * 2); // ensure enough QU
        RANDOM::RevealAndCommit_input inp{};
        inp.committedDigest = RANDOM().computeHash(commitBits);
        invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, inp, RANDOM::RevealAndCommit_output{}, miner, deposit);
    }

    void revealAndCommit(const id& miner, const bit_4096& revealBits, const bit_4096& newCommitBits, uint64_t deposit)
    {
        setActiveUser(miner);
        RANDOM::RevealAndCommit_input inp{};
        inp.revealedBits = revealBits;
        inp.committedDigest = RANDOM().computeHash(newCommitBits);
        invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, inp, RANDOM::RevealAndCommit_output{}, miner, deposit);
    }

    void stopMining(const id& miner, const bit_4096& revealBits)
    {
        setActiveUser(miner);
        RANDOM::RevealAndCommit_input inp{};
        inp.revealedBits = revealBits;
        inp.committedDigest = {};
        invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, inp, RANDOM::RevealAndCommit_output{}, miner, 0);
    }

    bool buyEntropy(const id& buyer, uint32_t numBytes, uint64_t minMinerDeposit, uint64_t suggestedFee, bool expectSuccess)
    {
        setActiveUser(buyer);
        setBalance(buyer, getBalance(buyer) + suggestedFee + 10000);

        RANDOM::BuyEntropy_input inp{};
        inp.numberOfBytes = numBytes;
        inp.minMinerDeposit = minMinerDeposit;
        RANDOM::BuyEntropy_output out{};
        invokeUserProcedure(RANDOM_CONTRACT_INDEX, 2, inp, out, buyer, suggestedFee);

        if (expectSuccess)
            EXPECT_TRUE(out.success);
        else
            EXPECT_FALSE(out.success);
        return out.success;
    }

    // Direct call to get price
    uint64_t queryPrice(uint32_t numBytes, uint64_t minMinerDeposit)
    {
        RANDOM::QueryPrice_input q{};
        q.numberOfBytes = numBytes;
        q.minMinerDeposit = minMinerDeposit;
        RANDOM::QueryPrice_output o{};
        callFunction(RANDOM_CONTRACT_INDEX, 3, q, o);
        return o.price;
    }

    // Helper entropy/id for test readability
    static bit_4096 testBits(uint64_t v) { bit_4096 b; for (int i = 0; i < 64; ++i) b.data[i] = v ^ (0xDEADBEEF12340000ULL | i); return b; }
    static id testId(uint64_t base)       { id d; for (int i = 0; i < 32; ++i) d.bytes[i] = uint8_t((base >> (i%8)) + i); return d; }
};


//------------------------------
//       TEST CASES
//------------------------------

TEST(ContractRandom, BasicCommitRevealStop)
{
    ContractTestingRandom random;
    id miner = ContractTestingRandom::testId(10);
    bit_4096 E1 = ContractTestingRandom::testBits(101);
    bit_4096 E2 = ContractTestingRandom::testBits(202);

    random.commit(miner, E1, 1000);
    random.revealAndCommit(miner, E1, E2, 1000);
    random.stopMining(miner, E2);

    EXPECT_EQ(random.getState()->commitmentCount, 0);
}

TEST(ContractRandom, TimeoutsAndRefunds)
{
    ContractTestingRandom random;
    id miner = ContractTestingRandom::testId(11);
    bit_4096 bits = ContractTestingRandom::testBits(303);

    random.commit(miner, bits, 2000);

    // Timeout: Advance tick past deadline
    int timeoutTick = getTick() + random.getState()->revealTimeoutTicks + 1;
    setTick(timeoutTick);

    // Trigger timeout (choose any call, including another commit or dummy reveal)
    RANDOM::RevealAndCommit_input dummy = {};
    random.setActiveUser(miner);
    invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, dummy, RANDOM::RevealAndCommit_output{}, miner, 0);

    EXPECT_EQ(random.getState()->commitmentCount, 0);
    EXPECT_EQ(random.getState()->lostDepositsRevenue, 2000);
}

TEST(ContractRandom, EmptyTickRefund)
{
    ContractTestingRandom random;
    id miner = ContractTestingRandom::testId(12);
    bit_4096 bits = ContractTestingRandom::testBits(404);

    random.commit(miner, bits, 3000);

    int refundTick = getTick() + random.getState()->revealTimeoutTicks;
    setTick(refundTick);
    setTickIsEmpty(true);

    // All deadlines expire on an empty tick: refund
    RANDOM::RevealAndCommit_input dummy = {};
    random.setActiveUser(miner);
    invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, dummy, RANDOM::RevealAndCommit_output{}, miner, 0);
    EXPECT_EQ(random.getState()->commitmentCount, 0);
}

TEST(ContractRandom, BuyEntropyEligibility)
{
    ContractTestingRandom random;
    id miner = ContractTestingRandom::testId(13);
    id buyer = ContractTestingRandom::testId(14);
    bit_4096 bits = ContractTestingRandom::testBits(321);

    // No miners: should fail
    EXPECT_FALSE(random.buyEntropy(buyer, 8, 1000, 8000, false));

    // Commit/reveal
    random.commit(miner, bits, 1000);
    random.revealAndCommit(miner, bits, ContractTestingRandom::testBits(333), 1000);

    // Should now succeed
    EXPECT_TRUE(random.buyEntropy(buyer, 16, 1000, 16000, true));

    // Advance past freshness window, should fail again
    setTick(getTick() + random.getState()->revealTimeoutTicks + 1);
    EXPECT_FALSE(random.buyEntropy(buyer, 16, 1000, 16000, false));
}

TEST(ContractRandom, QueryPriceLogic)
{
    ContractTestingRandom random;
    uint64_t price = random.queryPrice(16, 1000);
    auto* state = random.getState();
    EXPECT_EQ(price, state->pricePerByte * 16 * (1000 / state->priceDepositDivisor + 1));
}

TEST(ContractRandom, CompactionBehavior)
{
    ContractTestingRandom random;
    for (int i = 0; i < 10; ++i) {
        id miner = ContractTestingRandom::testId(100 + i);
        bit_4096 bits = ContractTestingRandom::testBits(1001 + i);
        random.commit(miner, bits, 5000);
        random.revealAndCommit(miner, bits, ContractTestingRandom::testBits(2001 + i), 5000);
        random.stopMining(miner, ContractTestingRandom::testBits(2001 + i));
    }
    EXPECT_EQ(random.getState()->commitmentCount, 0);
}

// ----- Additional/extensive tests -----

// Multi-miner pool splitting and buyer selection
TEST(ContractRandom, MultipleMinersAndBuyers)
{
    ContractTestingRandom random;
    id minerA = ContractTestingRandom::testId(1001);
    id minerB = ContractTestingRandom::testId(1002);
    id buyer1 = ContractTestingRandom::testId(1003);
    id buyer2 = ContractTestingRandom::testId(1004);
    bit_4096 entropyA = ContractTestingRandom::testBits(5678);
    bit_4096 entropyB = ContractTestingRandom::testBits(6789);

    // Both miners commit, same deposit
    random.commit(minerA, entropyA, 10000);
    random.commit(minerB, entropyB, 10000);
    random.revealAndCommit(minerA, entropyA, ContractTestingRandom::testBits(8888), 10000);
    random.revealAndCommit(minerB, entropyB, ContractTestingRandom::testBits(9999), 10000);

    // Buyer1 can purchase with either miner as eligible
    EXPECT_TRUE(random.buyEntropy(buyer1, 8, 10000, 20000, true));
    // Buyer2 requires more security than available
    EXPECT_FALSE(random.buyEntropy(buyer2, 16, 20000, 35000, false));
}

TEST(ContractRandom, MaxCommitmentsAndEviction)
{
    ContractTestingRandom random;
    // Fill the commitments array
    const int N = 32;
    std::vector<id> miners;
    for (int i = 0; i < N; ++i) {
        miners.push_back(ContractTestingRandom::testId(300 + i));
        random.commit(miners.back(), ContractTestingRandom::testBits(1234 + i), 5555);
    }
    EXPECT_EQ(random.getState()->commitmentCount, N);

    // Reveal all out-of-order, ensure compaction
    for (int i = N-1; i >= 0; --i) {
        random.revealAndCommit(miners[i], ContractTestingRandom::testBits(1234 + i), ContractTestingRandom::testBits(2000 + i), 5555);
        random.stopMining(miners[i], ContractTestingRandom::testBits(2000 + i));
    }
    EXPECT_EQ(random.getState()->commitmentCount, 0);
}

TEST(ContractRandom, EndEpochDistribution)
{
    ContractTestingRandom random;
    id miner1 = ContractTestingRandom::testId(99);
    id miner2 = ContractTestingRandom::testId(98);
    bit_4096 e1 = ContractTestingRandom::testBits(501);
    bit_4096 e2 = ContractTestingRandom::testBits(502);

    random.commit(miner1, e1, 10000);
    random.revealAndCommit(miner1, e1, ContractTestingRandom::testBits(601), 10000);
    random.commit(miner2, e2, 10000);
    random.revealAndCommit(miner2, e2, ContractTestingRandom::testBits(602), 10000);

    id buyer = ContractTestingRandom::testId(90);
    uint64_t price = random.queryPrice(16, 10000);
    random.buyEntropy(buyer, 16, 10000, price, true);

    // Simulate EndEpoch
    callSystemProcedure(RANDOM_CONTRACT_INDEX, END_EPOCH);

    // After epoch, earnings pools should be zeroed and recentMinerCount cleared
    EXPECT_EQ(random.getState()->minerEarningsPool, 0);
    EXPECT_EQ(random.getState()->shareholderEarningsPool, 0);
    EXPECT_EQ(random.getState()->recentMinerCount, 0);
}

TEST(ContractRandom, RecentMinerEvictionPolicy)
{
    ContractTestingRandom random;
    const int maxMiners = random.getState()->MAX_RECENT_MINERS;
    std::vector<id> miners;
    auto baseDeposit = 1000;

    // Fill up to MAX_RECENT_MINERS, all with same deposit
    for (int i = 0; i < maxMiners; ++i) {
        auto miner = ContractTestingRandom::testId(5000 + i);
        miners.push_back(miner);
        random.commit(miner, random.testBits(7000 + i), baseDeposit);
        random.revealAndCommit(miner, random.testBits(7000 + i), random.testBits(8000 + i), baseDeposit);
    }
    EXPECT_EQ(random.getState()->recentMinerCount, maxMiners);

    // Add new miner with higher deposit, should evict one of the previous (lowest deposit)
    id highMiner = ContractTestingRandom::testId(99999);
    random.commit(highMiner, random.testBits(55555), baseDeposit * 10);
    random.revealAndCommit(highMiner, random.testBits(55555), random.testBits(55566), baseDeposit * 10);

    EXPECT_EQ(random.getState()->recentMinerCount, maxMiners);

    // All lower deposit miners except one likely evicted, highMiner should be present with max deposit
    int foundHigh = 0;
    for (uint32_t i = 0; i < maxMiners; ++i) {
        if (random.getState()->recentMiners[i].deposit == baseDeposit * 10)
            foundHigh++;
    }
    EXPECT_EQ(foundHigh, 1);
}

TEST(ContractRandom, BuyerPickinessHighRequirement)
{
    ContractTestingRandom random;
    id miner = random.testId(721);
    id buyer = random.testId(722);
    uint64_t lowDeposit = 1000, highDeposit = 100000;

    random.commit(miner, random.testBits(100), lowDeposit);
    random.revealAndCommit(miner, random.testBits(100), random.testBits(101), lowDeposit);

    // As buyer, require higher min deposit than any available miner supplied
    EXPECT_FALSE(random.buyEntropy(buyer, 8, highDeposit, 10000, false));
}

TEST(ContractRandom, MixedDepositLevels)
{
    ContractTestingRandom random;
    id lowMiner = random.testId(1001);
    id highMiner = random.testId(1002);
    id buyer = random.testId(1003);

    random.commit(lowMiner, random.testBits(88), 1000);
    random.commit(highMiner, random.testBits(89), 100000);
    random.revealAndCommit(lowMiner, random.testBits(88), random.testBits(188), 1000);
    random.revealAndCommit(highMiner, random.testBits(89), random.testBits(189), 100000);

    EXPECT_TRUE(random.buyEntropy(buyer, 8, 1000, 10000, true));
    EXPECT_TRUE(random.buyEntropy(buyer, 8, 100000, 100000, true));
    EXPECT_FALSE(random.buyEntropy(buyer, 8, 100001, 100000, false));
}

TEST(ContractRandom, EmptyTickRefund_MultiMiners)
{
    ContractTestingRandom random;
    id m1 = random.testId(931);
    id m2 = random.testId(932);
    random.commit(m1, random.testBits(401), 5000);
    random.commit(m2, random.testBits(402), 7000);

    int tick = getTick() + random.getState()->revealTimeoutTicks;
    setTick(tick);
    setTickIsEmpty(true);

    RANDOM::RevealAndCommit_input dummy = {};
    random.setActiveUser(m1);
    invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, dummy, RANDOM::RevealAndCommit_output{}, m1, 0);
    EXPECT_EQ(random.getState()->commitmentCount, 0);
    // test both miners' balances for refund if desired
}

TEST(ContractRandom, Timeout_MultiMiners)
{
    ContractTestingRandom random;
    id m1 = random.testId(7777);
    id m2 = random.testId(8888);
    random.commit(m1, random.testBits(111), 2000);
    random.commit(m2, random.testBits(112), 4000);
    int afterTimeout = getTick() + random.getState()->revealTimeoutTicks + 1;
    setTick(afterTimeout);

    RANDOM::RevealAndCommit_input dummy = {};
    random.setActiveUser(m2);
    invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, dummy, RANDOM::RevealAndCommit_output{}, m2, 0);

    EXPECT_EQ(random.getState()->commitmentCount, 0);
    EXPECT_EQ(random.getState()->lostDepositsRevenue, 6000);
}

TEST(ContractRandom, MultipleBuyersEpochReset)
{
    ContractTestingRandom random;
    id miner = random.testId(1201);
    id buyer1 = random.testId(1301);
    id buyer2 = random.testId(1401);

    random.commit(miner, random.testBits(900), 8000);
    random.revealAndCommit(miner, random.testBits(900), random.testBits(901), 8000);

    EXPECT_TRUE(random.buyEntropy(buyer1, 8, 8000, 20000, true));
    EXPECT_TRUE(random.buyEntropy(buyer2, 16, 8000, 50000, true));

    callSystemProcedure(RANDOM_CONTRACT_INDEX, END_EPOCH);

    EXPECT_EQ(random.getState()->minerEarningsPool, 0);
    EXPECT_EQ(random.getState()->shareholderEarningsPool, 0);
    EXPECT_EQ(random.getState()->recentMinerCount, 0);
}

TEST(ContractRandom, QueryUserCommitmentsInfo)
{
    ContractTestingRandom random;
    id miner = random.testId(2001);

    random.commit(miner, random.testBits(1234), 10000);

    // Call GetUserCommitments for miner
    RANDOM::GetUserCommitments_input inp{};
    inp.userId = miner;
    RANDOM::GetUserCommitments_output out{};
    callFunction(RANDOM_CONTRACT_INDEX, 2, inp, out);
    EXPECT_GE(out.commitmentCount, 1);

    // Call GetContractInfo for global stats
    RANDOM::GetContractInfo_input ci{};
    RANDOM::GetContractInfo_output co{};
    callFunction(RANDOM_CONTRACT_INDEX, 1, ci, co);
    EXPECT_GE(co.totalCommits, 1);
}

TEST(ContractRandom, RejectInvalidDeposits)
{
    ContractTestingRandom random;
    id miner = random.testId(2012);

    // Try to commit invalid deposit (not a power of ten)
    setActiveUser(miner);
    RANDOM::RevealAndCommit_input inp{};
    inp.committedDigest = RANDOM().computeHash(random.testBits(66));
    setBalance(miner, getBalance(miner) + 7777);
    RANDOM::RevealAndCommit_output out{};
    // Use 7777 which is not a power of ten, should not register a commitment
    invokeUserProcedure(RANDOM_CONTRACT_INDEX, 1, inp, out, miner, 7777);

    EXPECT_EQ(random.getState()->commitmentCount, 0);
}

TEST(ContractRandom, BuyEntropyEdgeNumBytes)
{
    ContractTestingRandom random;
    id miner = random.testId(3031);
    id buyer = random.testId(3032);

    random.commit(miner, random.testBits(8888), 8000);
    random.revealAndCommit(miner, random.testBits(8888), random.testBits(8899), 8000);

    // 1 byte (minimum)
    EXPECT_TRUE(random.buyEntropy(buyer, 1, 8000, 10000, true));
    // 32 bytes (maximum)
    EXPECT_TRUE(random.buyEntropy(buyer, 32, 8000, 40000, true));
    // 33 bytes (over contract max, should clamp or fail)
    EXPECT_FALSE(random.buyEntropy(buyer, 33, 8000, 50000, false));
}

TEST(ContractRandom, OutOfOrderRevealAndCompaction)
{
    ContractTestingRandom random;
    std::vector<id> miners;
    for (int i = 0; i < 8; ++i) {
        miners.push_back(random.testId(5400 + i));
        random.commit(miners.back(), random.testBits(8500 + i), 6000);
    }
    // Reveal/stop in random order
    random.revealAndCommit(miners[3], random.testBits(8500 + 3), random.testBits(9500 + 3), 6000);
    random.revealAndCommit(miners[1], random.testBits(8500 + 1), random.testBits(9500 + 1), 6000);
    random.stopMining(miners[3], random.testBits(9500 + 3));
    random.stopMining(miners[1], random.testBits(9500 + 1));
    // Now reveal/stop remainder
    for (int i = 0; i < 8; ++i) {
        if (i == 1 || i == 3) continue;
        random.revealAndCommit(miners[i], random.testBits(8500 + i), random.testBits(9500 + i), 6000);
        random.stopMining(miners[i], random.testBits(9500 + i));
    }
    EXPECT_EQ(random.getState()->commitmentCount, 0);
}
