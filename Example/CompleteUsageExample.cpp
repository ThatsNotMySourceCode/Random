#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>
#include "SimpleRandomClient_cli.cpp"

// Remove main() from SimpleRandomClient_cli.cpp before using this as a test harness!

void waitUntilTick(int targetTick) {
    int cur = getCurrentTick();
    while (cur < targetTick) {
        std::cout << "Current Tick: " << cur << ", Waiting for Tick: " << targetTick << " (remaining " << targetTick-cur << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cur = getCurrentTick();
    }
}

void demonstrateExactFlow() {
    uint64 deposit = 10000;

    std::cout << "=== Exact 3-Tick Flow: 5 → 8 → 11 ===" << std::endl;

    std::cout << "\nTick 5: Generate E1, commit hash(E1)" << std::endl;
    waitUntilTick(5);

    Bit4096 entropy1 = generateEntropy();
    Id digest1 = hashEntropy(entropy1);
    Bit4096 zeroReveal = {};

    minerCommit(zeroReveal, digest1, deposit);

    std::cout << "\nTick 8: Generate E2, reveal E1, commit hash(E2)" << std::endl;
    waitUntilTick(8);

    Bit4096 entropy2 = generateEntropy();
    Id digest2 = hashEntropy(entropy2);

    minerCommit(entropy1, digest2, deposit);

    std::cout << "\nTick 11: Reveal E2, stop mining" << std::endl;
    waitUntilTick(11);

    Id zeroCommit = {};
    minerCommit(entropy2, zeroCommit, 0);
}

void demonstrateExtendedMining() {
    uint64 deposit = 50000;
    std::cout << "\n=== Extended Mining (Multiple 3-Tick Cycles) ===" << std::endl;

    uint32_t startTick = 20;
    Bit4096 currentEntropy;

    waitUntilTick(startTick);
    currentEntropy = generateEntropy();
    Id currentDigest = hashEntropy(currentEntropy);
    Bit4096 zeroReveal = {};

    minerCommit(zeroReveal, currentDigest, deposit);

    for (int cycle = 1; cycle <= 5; cycle++) {
        uint32_t revealTick = startTick + (cycle * 3);

        waitUntilTick(revealTick);

        Bit4096 entropyToReveal = currentEntropy;
        currentEntropy = generateEntropy();
        currentDigest = hashEntropy(currentEntropy);

        minerCommit(entropyToReveal, currentDigest, deposit);
    }

    uint32_t finalTick = startTick + 6 * 3;
    waitUntilTick(finalTick);

    Id zeroCommit = {};
    minerCommit(currentEntropy, zeroCommit, 0);
}

void demonstrateThreeFlows() {
    uint64 deposit = 100000;

    std::cout << "\n=== Three Parallel Mining Flows ===" << std::endl;
    std::cout << "Flow A: ticks 3, 6, 9, 12, 15..." << std::endl;
    std::cout << "Flow B: ticks 4, 7, 10, 13, 16..." << std::endl;
    std::cout << "Flow C: ticks 5, 8, 11, 14, 17..." << std::endl;

    Bit4096 entropyA, entropyB, entropyC;
    Bit4096 zeroReveal = {};

    waitUntilTick(3);
    entropyA = generateEntropy();
    minerCommit(zeroReveal, hashEntropy(entropyA), deposit);

    waitUntilTick(4);
    entropyB = generateEntropy();
    minerCommit(zeroReveal, hashEntropy(entropyB), deposit);

    waitUntilTick(5);
    entropyC = generateEntropy();
    minerCommit(zeroReveal, hashEntropy(entropyC), deposit);

    for (int cycle = 1; cycle <= 3; cycle++) {
        waitUntilTick(3 + cycle * 3);
        Bit4096 newEntropyA = generateEntropy();
        minerCommit(entropyA, hashEntropy(newEntropyA), deposit);
        entropyA = newEntropyA;

        waitUntilTick(4 + cycle * 3);
        Bit4096 newEntropyB = generateEntropy();
        minerCommit(entropyB, hashEntropy(newEntropyB), deposit);
        entropyB = newEntropyB;

        waitUntilTick(5 + cycle * 3);
        Bit4096 newEntropyC = generateEntropy();
        minerCommit(entropyC, hashEntropy(newEntropyC), deposit);
        entropyC = newEntropyC;
    }

    std::cout << "\n--- Stopping All Flows ---" << std::endl;
    Id zeroCommit = {};

    waitUntilTick(15);
    minerCommit(entropyA, zeroCommit, 0);

    waitUntilTick(16);
    minerCommit(entropyB, zeroCommit, 0);

    waitUntilTick(17);
    minerCommit(entropyC, zeroCommit, 0);
}

void demonstrateBuyEntropyOnce() {
    std::cout << "\n=== Buy Entropy as a Customer ===" << std::endl;
    uint32_t wants = 32;
    uint64 minDep = 100000;
    uint64 fee = queryPrice(wants, minDep);
    if(!fee) {
        std::cerr << "Failed to get fee quote from contract - skipping buy call." << std::endl;
        return;
    }
    std::cout << "[Demo] Fee required for buy call: " << fee << std::endl;
    buyEntropyCli(wants, minDep);
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
