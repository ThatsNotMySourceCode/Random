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
