#include "SimpleRandomClient.cpp"

// Example 1: Basic mining workflow
void demonstrateBasicMining() {
    SimpleRandomClient client;
    uint64 deposit = 10000; // 10K QU
    
    std::cout << "=== Basic Mining Workflow ===" << std::endl;
    
    // Tick 5: Start mining
    waitUntilTick(5);
    auto result = client.startMining(deposit);
    std::cout << "Tick 5 - Got random bytes: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << std::endl;
    
    // Tick 8: Continue mining
    waitUntilTick(8);
    result = client.continueMining(deposit);
    std::cout << "Tick 8 - Got random bytes: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << " (Deposit returned: " << result.depositReturned << " QU)" << std::endl;
    
    // Tick 11: Stop mining
    waitUntilTick(11);
    result = client.stopMining();
    std::cout << "Tick 11 - Final random bytes: ";
    for (int i = 0; i < 8; i++) printf("%02x ", result.randomBytes[i]);
    std::cout << " (Final deposit: " << result.depositReturned << " QU)" << std::endl;
}

// Example 2: Temporary mining for one-time randomness
void demonstrateTemporaryMining() {
    SimpleRandomClient client;
    
    std::cout << "\n=== Temporary Mining for Randomness ===" << std::endl;
    
    // Just need randomness once? Mine temporarily
    auto result = client.getRandomnessTemporarily(50000); // 50K QU
    
    if (result.success) {
        // Use the random bytes immediately
        uint32 diceRoll = (result.randomBytes[0] % 6) + 1;
        uint32 randomNumber = result.randomBytes[1] % 101;
        uint32 coinFlip = result.randomBytes[2] % 2;
        
        std::cout << "Random dice roll: " << diceRoll << std::endl;
        std::cout << "Random 0-100: " << randomNumber << std::endl;
        std::cout << "Coin flip: " << (coinFlip ? "Heads" : "Tails") << std::endl;
    }
}

// Example 3: How other contracts get randomness
void demonstrateLotteryContract() {
    std::cout << "\n=== Lottery Using Miner-Provided Randomness ===" << std::endl;
    
    // Step 1: Someone mines randomness
    SimpleRandomClient miner;
    auto miningResult = miner.getRandomnessTemporarily(100000);
    
    if (miningResult.success) {
        std::cout << "Miner provided random bytes for lottery" << std::endl;
        
        // Step 2: Use randomness in lottery contract
        struct LotteryInput {
            uint8 minerRandomBytes[32];
            uint64 entropyVersion;
        };
        
        LotteryInput lotteryInput;
        memcpy(lotteryInput.minerRandomBytes, miningResult.randomBytes, 32);
        lotteryInput.entropyVersion = miningResult.entropyVersion;
        
        // Simulate lottery draw
        uint32 participantCount = 1000;
        uint32 winnerIndex = lotteryInput.minerRandomBytes[0] % participantCount;
        
        std::cout << "Lottery winner: Participant #" << winnerIndex << std::endl;
        std::cout << "Based on entropy version: " << lotteryInput.entropyVersion << std::endl;
    }
}

// Example 4: Continuous mining for profit
void demonstrateContinuousMining() {
    SimpleRandomClient client;
    uint64 deposit = 1000000; // 1M QU for serious mining
    
    std::cout << "\n=== Continuous Mining for Profit ===" << std::endl;
    
    // Start mining
    auto result = client.startMining(deposit);
    std::cout << "Started continuous mining" << std::endl;
    
    // Mine for 10 cycles
    for (int cycle = 1; cycle <= 10; cycle++) {
        waitForTicks(3);
        result = client.continueMining(deposit);
        
        if (result.success) {
            std::cout << "Cycle " << cycle << ": Got random bytes, deposit returned: " 
                      << result.depositReturned << " QU" << std::endl;
            
            // Could sell these random bytes to other contracts/users
            // or use them for your own applications
        }
    }
    
    // Stop mining
    waitForTicks(3);
    result = client.stopMining();
    std::cout << "Stopped mining, final deposit: " << result.depositReturned << " QU" << std::endl;
}

// Example 5: Check contract statistics
void demonstrateContractStats() {
    SimpleRandomClient client;
    
    std::cout << "\n=== Contract Statistics ===" << std::endl;
    
    auto info = client.getContractInfo();
    
    std::cout << "Total commits: " << info.totalCommits << std::endl;
    std::cout << "Total reveals: " << info.totalReveals << std::endl;
    std::cout << "Active commitments: " << info.activeCommitments << std::endl;
    std::cout << "Entropy pool version: " << info.entropyPoolVersion << std::endl;
    std::cout << "Total revenue (lost deposits): " << info.totalRevenue << " QU" << std::endl;
    
    double successRate = info.totalCommits > 0 ? 
        (double)info.totalReveals / info.totalCommits * 100 : 0;
    std::cout << "Success rate: " << successRate << "%" << std::endl;
}

int main() {
    try {
        // Run all examples
        demonstrateBasicMining();
        demonstrateTemporaryMining();
        demonstrateLotteryContract();
        demonstrateContinuousMining();
        demonstrateContractStats();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}