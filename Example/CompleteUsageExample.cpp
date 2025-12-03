#include "SimpleRandomClient.cpp"
#include <iomanip>

// Example 1: Exact flow as specified - Tick 5 → 8 → 11
void demonstrateExactFlow() {
    SimpleRandomClient client;
    uint64 deposit = 10000; // 10K QU
    
    std::cout << "=== Exact 3-Tick Flow: 5 → 8 → 11 ===" << std::endl;
    
    // Tick 5: generate E1. Send zeros in revealed, send hash(E1) as committed
    std::cout << "\nTick 5: Generate E1, commit hash(E1)" << std::endl;
    client.waitUntilTick(5);
    
    bit_4096 E1 = client.generateEntropy();
    id digestE1 = client.hashEntropy(E1);
    bit_4096 zeroReveal = {};
    
    auto result = client.revealAndCommit(zeroReveal, digestE1, deposit);
    std::cout << "✓ Initial commit successful" << std::endl;
    std::cout << "Random bytes from Tick 5: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << "... (entropy v" << result.entropyVersion << ")" << std::endl;
    
    // Tick 8: generate E2. Send E1 as revealed, send hash(E2) as committed
    std::cout << "\nTick 8: Generate E2, reveal E1, commit hash(E2)" << std::endl;
    client.waitUntilTick(8);
    
    bit_4096 E2 = client.generateEntropy();
    id digestE2 = client.hashEntropy(E2);
    
    result = client.revealAndCommit(E1, digestE2, deposit);
    std::cout << "✓ Revealed E1 and committed E2" << std::endl;
    std::cout << "Random bytes from Tick 8: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << "... (entropy v" << result.entropyVersion << ")" << std::endl;
    std::cout << "Deposit returned: " << result.depositReturned << " QU" << std::endl;
    
    // Tick 11: generate nothing. Send E2 in revealed, send zeros in committed, use zero amount
    std::cout << "\nTick 11: Reveal E2, stop mining" << std::endl;
    client.waitUntilTick(11);
    
    id zeroCommit = {};
    result = client.revealAndCommit(E2, zeroCommit, 0);
    std::cout << "✓ Final reveal successful" << std::endl;
    std::cout << "Final random bytes: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << "... (entropy v" << result.entropyVersion << ")" << std::endl;
    std::cout << "Final deposit returned: " << result.depositReturned << " QU" << std::endl;
}

// Example 2: Extended mining with proper 3-tick intervals
void demonstrateExtendedMining() {
    SimpleRandomClient client;
    uint64 deposit = 50000; // 50K QU
    
    std::cout << "\n=== Extended Mining (Multiple 3-Tick Cycles) ===" << std::endl;
    
    uint32 startTick = 20; // Start at tick 20
    bit_4096 currentEntropy;
    
    // Tick 20: Initial commit
    client.waitUntilTick(startTick);
    currentEntropy = client.generateEntropy();
    id currentDigest = client.hashEntropy(currentEntropy);
    bit_4096 zeroReveal = {};
    
    auto result = client.revealAndCommit(zeroReveal, currentDigest, deposit);
    std::cout << "Tick " << startTick << ": Initial commit, random: ";
    for (int i = 0; i < 4; i++) printf("%02x", result.randomBytes[i]);
    std::cout << std::endl;
    
    // Continue for 5 cycles
    for (int cycle = 1; cycle <= 5; cycle++) {
        uint32 revealTick = startTick + (cycle * 3);
        
        client.waitUntilTick(revealTick);
        
        // Store current entropy to reveal
        bit_4096 entropyToReveal = currentEntropy;
        
        // Generate new entropy for next cycle
        currentEntropy = client.generateEntropy();
        currentDigest = client.hashEntropy(currentEntropy);
        
        result = client.revealAndCommit(entropyToReveal, currentDigest, deposit);
        
        std::cout << "Tick " << revealTick << ": Cycle " << cycle 
                  << ", random: ";
        for (int i = 0; i < 4; i++) printf("%02x", result.randomBytes[i]);
        std::cout << ", returned: " << result.depositReturned << " QU" << std::endl;
    }
    
    // Final reveal
    uint32 finalTick = startTick + 6 * 3;
    client.waitUntilTick(finalTick);
    
    id zeroCommit = {};
    result = client.revealAndCommit(currentEntropy, zeroCommit, 0);
    std::cout << "Tick " << finalTick << ": Final reveal, random: ";
    for (int i = 0; i < 4; i++) printf("%02x", result.randomBytes[i]);
    std::cout << ", returned: " << result.depositReturned << " QU" << std::endl;
}

// Example 3: Three parallel flows (3N, 3N+1, 3N+2)
void demonstrateThreeFlows() {
    SimpleRandomClient clientA, clientB, clientC;
    uint64 deposit = 100000; // 100K QU each
    
    std::cout << "\n=== Three Parallel Mining Flows ===" << std::endl;
    std::cout << "Flow A: ticks 3, 6, 9, 12, 15..." << std::endl;
    std::cout << "Flow B: ticks 4, 7, 10, 13, 16..." << std::endl;
    std::cout << "Flow C: ticks 5, 8, 11, 14, 17..." << std::endl;
    
    bit_4096 entropyA, entropyB, entropyC;
    bit_4096 zeroReveal = {};
    
    // Start Flow A at tick 3
    clientA.waitUntilTick(3);
    entropyA = clientA.generateEntropy();
    auto resultA = clientA.revealAndCommit(zeroReveal, clientA.hashEntropy(entropyA), deposit);
    std::cout << "Tick 3: Flow A started, random: ";
    for (int i = 0; i < 4; i++) printf("%02x", resultA.randomBytes[i]);
    std::cout << std::endl;
    
    // Start Flow B at tick 4
    clientB.waitUntilTick(4);
    entropyB = clientB.generateEntropy();
    auto resultB = clientB.revealAndCommit(zeroReveal, clientB.hashEntropy(entropyB), deposit);
    std::cout << "Tick 4: Flow B started, random: ";
    for (int i = 0; i < 4; i++) printf("%02x", resultB.randomBytes[i]);
    std::cout << std::endl;
    
    // Start Flow C at tick 5
    clientC.waitUntilTick(5);
    entropyC = clientC.generateEntropy();
    auto resultC = clientC.revealAndCommit(zeroReveal, clientC.hashEntropy(entropyC), deposit);
    std::cout << "Tick 5: Flow C started, random: ";
    for (int i = 0; i < 4; i++) printf("%02x", resultC.randomBytes[i]);
    std::cout << std::endl;
    
    // Continue flows for 3 cycles
    for (int cycle = 1; cycle <= 3; cycle++) {
        std::cout << "\n--- Cycle " << cycle << " ---" << std::endl;
        
        // Flow A (tick 3 + cycle*3)
        clientA.waitUntilTick(3 + cycle * 3);
        bit_4096 newEntropyA = clientA.generateEntropy();
        resultA = clientA.revealAndCommit(entropyA, clientA.hashEntropy(newEntropyA), deposit);
        std::cout << "Tick " << (3 + cycle * 3) << ": Flow A, random: ";
        for (int i = 0; i < 4; i++) printf("%02x", resultA.randomBytes[i]);
        std::cout << ", returned: " << resultA.depositReturned << " QU" << std::endl;
        entropyA = newEntropyA;
        
        // Flow B (tick 4 + cycle*3)
        clientB.waitUntilTick(4 + cycle * 3);
        bit_4096 newEntropyB = clientB.generateEntropy();
        resultB = clientB.revealAndCommit(entropyB, clientB.hashEntropy(newEntropyB), deposit);
        std::cout << "Tick " << (4 + cycle * 3) << ": Flow B, random: ";
        for (int i = 0; i < 4; i++) printf("%02x", resultB.randomBytes[i]);
        std::cout << ", returned: " << resultB.depositReturned << " QU" << std::endl;
        entropyB = newEntropyB;
        
        // Flow C (tick 5 + cycle*3)
        clientC.waitUntilTick(5 + cycle * 3);
        bit_4096 newEntropyC = clientC.generateEntropy();
        resultC = clientC.revealAndCommit(entropyC, clientC.hashEntropy(newEntropyC), deposit);
        std::cout << "Tick " << (5 + cycle * 3) << ": Flow C, random: ";
        for (int i = 0; i < 4; i++) printf("%02x", resultC.randomBytes[i]);
        std::cout << ", returned: " << resultC.depositReturned << " QU" << std::endl;
        entropyC = newEntropyC;
    }
    
    // Stop all flows
    std::cout << "\n--- Stopping All Flows ---" << std::endl;
    id zeroCommit = {};
    
    clientA.waitUntilTick(15);
    resultA = clientA.revealAndCommit(entropyA, zeroCommit, 0);
    std::cout << "Tick 15: Flow A stopped, final deposit: " << resultA.depositReturned << " QU" << std::endl;
    
    clientB.waitUntilTick(16);
    resultB = clientB.revealAndCommit(entropyB, zeroCommit, 0);
    std::cout << "Tick 16: Flow B stopped, final deposit: " << resultB.depositReturned << " QU" << std::endl;
    
    clientC.waitUntilTick(17);
    resultC = clientC.revealAndCommit(entropyC, zeroCommit, 0);
    std::cout << "Tick 17: Flow C stopped, final deposit: " << resultC.depositReturned << " QU" << std::endl;
}

// Example 4: One-time randomness with proper flow
void demonstrateOneTimeRandomness() {
    SimpleRandomClient client;
    
    std::cout << "\n=== One-Time Randomness (Proper Flow) ===" << std::endl;
    
    auto result = client.getRandomnessTemporarily(25000); // 25K QU
    
    if (result.success) {
        std::cout << "\nUsing randomness for applications:" << std::endl;
        
        // Use for applications
        uint32 diceRoll = (result.randomBytes[0] % 6) + 1;
        uint32 lottery = (result.randomBytes[1] % 1000) + 1;
        bool coinFlip = result.randomBytes[2] % 2;
        
        std::cout << "Dice roll (1-6): " << diceRoll << std::endl;
        std::cout << "Lottery number (1-1000): " << lottery << std::endl;
        std::cout << "Coin flip: " << (coinFlip ? "Heads" : "Tails") << std::endl;
    }
}

// Example 5: Contract statistics and monitoring
void demonstrateContractStats() {
    SimpleRandomClient client;
    
    std::cout << "\n=== Contract Statistics ===" << std::endl;
    
    auto info = client.getContractInfo();
    
    std::cout << "Contract Status:" << std::endl;
    std::cout << "  Total commits: " << info.totalCommits << std::endl;
    std::cout << "  Total reveals: " << info.totalReveals << std::endl;
    std::cout << "  Active commitments: " << info.activeCommitments << std::endl;
    std::cout << "  Security deposits locked: " << info.totalSecurityDepositsLocked << " QU" << std::endl;
    std::cout << "  Entropy pool version: " << info.entropyPoolVersion << std::endl;
    std::cout << "  Lost deposits revenue: " << info.lostDepositsRevenue << " QU" << std::endl;
    std::cout << "  Reveal timeout: " << info.revealTimeoutTicks << " ticks" << std::endl;
    std::cout << "  Minimum deposit: " << info.minimumSecurityDeposit << " QU" << std::endl;
    
    if (info.totalCommits > 0) {
        double successRate = (double)info.totalReveals / info.totalCommits * 100;
        std::cout << "  Success rate: " << std::fixed << std::setprecision(1) << successRate << "%" << std::endl;
    }
}

// Example 6: Lottery contract using miner randomness
void demonstrateLotteryUsage() {
    std::cout << "\n=== Lottery Using Miner-Provided Randomness ===" << std::endl;
    
    // Simulate lottery contract that needs randomness
    SimpleRandomClient miner;
    
    std::cout << "Lottery needs randomness - miner provides it..." << std::endl;
    
    auto result = miner.getRandomnessTemporarily(50000);
    
    if (result.success) {
        // Simulate lottery contract using the randomness
        struct LotteryDraw {
            uint8 randomBytes[32];
            uint64 entropyVersion;
            uint32 participantCount;
        };
        
        LotteryDraw draw;
        memcpy(draw.randomBytes, result.randomBytes, 32);
        draw.entropyVersion = result.entropyVersion;
        draw.participantCount = 500;
        
        // Draw winners
        uint32 winner1 = draw.randomBytes[0] % draw.participantCount;
        uint32 winner2 = (draw.randomBytes[1] + draw.randomBytes[2]) % draw.participantCount;
        uint32 winner3 = (draw.randomBytes[3] + draw.randomBytes[4]) % draw.participantCount;
        
        std::cout << "🎉 Lottery Results (Entropy v" << draw.entropyVersion << "):" << std::endl;
        std::cout << "   1st Place: Participant #" << winner1 << std::endl;
        std::cout << "   2nd Place: Participant #" << winner2 << std::endl;
        std::cout << "   3rd Place: Participant #" << winner3 << std::endl;
        std::cout << "   Miner compensation: " << result.depositReturned << " QU" << std::endl;
    }
}

int main() {
    try {
        // Run all examples demonstrating proper 3-tick flow
        demonstrateExactFlow();
        demonstrateExtendedMining();
        demonstrateThreeFlows();
        demonstrateOneTimeRandomness();
        demonstrateContractStats();
        demonstrateLotteryUsage();
        
        std::cout << "\n🎉 All examples completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
