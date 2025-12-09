#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>
#include "SimpleRandomClient_cli.cpp"

// Remove main() from SimpleRandomClient_cli.cpp before using this as a test harness!

// Use CLI-based tick polling from updated SimpleRandomClient_cli.cpp
void waitUntilTick(int targetTick) {
    int cur = get_current_tick();
    while(cur < targetTick) {
        std::cout << "Current Tick: " << cur << ", Waiting for Tick: " << targetTick << " (remaining " << targetTick-cur << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cur = get_current_tick();
    }
}

// Example 1: Exact single 3-tick flow
void demonstrateExactFlow() {
    uint64 deposit = 10000; // 10K QU

    std::cout << "=== Exact 3-Tick Flow: 5 → 8 → 11 ===" << std::endl;

    // Tick 5: generate E1. Send zeros in revealed, send hash(E1) as committed
    std::cout << "\nTick 5: Generate E1, commit hash(E1)" << std::endl;
    waitUntilTick(5);

    bit_4096 E1 = generateEntropy();
    id digestE1 = hashEntropy(E1);
    bit_4096 zeroReveal = {};

    miner_commit(zeroReveal, digestE1, deposit);

    // Tick 8: generate E2. Send E1 as revealed, send hash(E2) as committed
    std::cout << "\nTick 8: Generate E2, reveal E1, commit hash(E2)" << std::endl;
    waitUntilTick(8);

    bit_4096 E2 = generateEntropy();
    id digestE2 = hashEntropy(E2);

    miner_commit(E1, digestE2, deposit);

    // Tick 11: reveal E2, stop mining
    std::cout << "\nTick 11: Reveal E2, stop mining" << std::endl;
    waitUntilTick(11);

    id zeroCommit = {};
    miner_commit(E2, zeroCommit, 0);
}

// Example 2: Multiple cycles with fixed interval
void demonstrateExtendedMining() {
    uint64 deposit = 50000; // 50K QU
    std::cout << "\n=== Extended Mining (Multiple 3-Tick Cycles) ===" << std::endl;

    uint32_t startTick = 20; // Start at tick 20
    bit_4096 currentEntropy;

    // Tick 20: Initial commit
    waitUntilTick(startTick);
    currentEntropy = generateEntropy();
    id currentDigest = hashEntropy(currentEntropy);
    bit_4096 zeroReveal = {};

    miner_commit(zeroReveal, currentDigest, deposit);

    // Continue for 5 cycles
    for (int cycle = 1; cycle <= 5; cycle++) {
        uint32_t revealTick = startTick + (cycle * 3);

        waitUntilTick(revealTick);

        // Store current entropy to reveal
        bit_4096 entropyToReveal = currentEntropy;

        // Generate new entropy for next cycle
        currentEntropy = generateEntropy();
        currentDigest = hashEntropy(currentEntropy);

        miner_commit(entropyToReveal, currentDigest, deposit);
    }

    // Final reveal
    uint32_t finalTick = startTick + 6 * 3;
    waitUntilTick(finalTick);

    id zeroCommit = {};
    miner_commit(currentEntropy, zeroCommit, 0);
}

// Example 3: Three parallel mining flows
void demonstrateThreeFlows() {
    uint64 deposit = 100000; // 100K QU each

    std::cout << "\n=== Three Parallel Mining Flows ===" << std::endl;
    std::cout << "Flow A: ticks 3, 6, 9, 12, 15..." << std::endl;
    std::cout << "Flow B: ticks 4, 7, 10, 13, 16..." << std::endl;
    std::cout << "Flow C: ticks 5, 8, 11, 14, 17..." << std::endl;

    bit_4096 entropyA, entropyB, entropyC;
    bit_4096 zeroReveal = {};

    // Start Flow A at tick 3
    waitUntilTick(3);
    entropyA = generateEntropy();
    miner_commit(zeroReveal, hashEntropy(entropyA), deposit);

    // Start Flow B at tick 4
    waitUntilTick(4);
    entropyB = generateEntropy();
    miner_commit(zeroReveal, hashEntropy(entropyB), deposit);

    // Start Flow C at tick 5
    waitUntilTick(5);
    entropyC = generateEntropy();
    miner_commit(zeroReveal, hashEntropy(entropyC), deposit);

    // Continue flows for 3 cycles
    for (int cycle = 1; cycle <= 3; cycle++) {
        // Flow A (tick 3 + cycle*3)
        waitUntilTick(3 + cycle * 3);
        bit_4096 newEntropyA = generateEntropy();
        miner_commit(entropyA, hashEntropy(newEntropyA), deposit);
        entropyA = newEntropyA;

        // Flow B (tick 4 + cycle*3)
        waitUntilTick(4 + cycle * 3);
        bit_4096 newEntropyB = generateEntropy();
        miner_commit(entropyB, hashEntropy(newEntropyB), deposit);
        entropyB = newEntropyB;

        // Flow C (tick 5 + cycle*3)
        waitUntilTick(5 + cycle * 3);
        bit_4096 newEntropyC = generateEntropy();
        miner_commit(entropyC, hashEntropy(newEntropyC), deposit);
        entropyC = newEntropyC;
    }

    // Stop all flows
    std::cout << "\n--- Stopping All Flows ---" << std::endl;
    id zeroCommit = {};

    waitUntilTick(15);
    miner_commit(entropyA, zeroCommit, 0);

    waitUntilTick(16);
    miner_commit(entropyB, zeroCommit, 0);

    waitUntilTick(17);
    miner_commit(entropyC, zeroCommit, 0);
}

// Query the contract for price directly (only numberOfBytes and minMinerDeposit now)
uint64 query_price(uint32_t numBytes, uint64 minDeposit);

void demonstrateBuyEntropyOnce() {
    std::cout << "\n=== Buy Entropy as a Customer ===" << std::endl;
    uint32_t wants = 32;
    uint64 minDep = 100000;
    uint64 fee = query_price(wants, minDep);
    if(!fee) {
        std::cerr << "Failed to get fee quote from contract - skipping buy call." << std::endl;
        return;
    }
    std::cout << "[Demo] Fee required for buy call: " << fee << std::endl;
    buy_entropy_cli(wants, minDep);
}

int main() {
    try {
        demonstrateExactFlow();
        demonstrateExtendedMining();
        demonstrateThreeFlows();
        demonstrateBuyEntropyOnce();
        std::cout << "\nAll flows completed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
