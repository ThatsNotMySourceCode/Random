#include "RandomContractClient.cpp"
#include <iostream>

int main() {
    RandomContractClient client;
    
    try {
        std::cout << "=== New 3-Tick Cycle Random Mining Examples ===" << std::endl;
        
        uint64 securityDeposit = 10000; // 10K QU (valid power of 10)
        uint32 targetCycle = getCurrentTick() % 3; // Choose current cycle
        
        // Example 1: Start mining on current cycle
        std::cout << "\n1. Starting mining on cycle " << targetCycle << "..." << std::endl;
        auto result = client.startMining(securityDeposit, targetCycle);
        if (result.success) {
            std::cout << "✓ Mining started: " << result.transactionId << std::endl;
        } else {
            std::cout << "✗ Mining start failed: " << result.errorMessage << std::endl;
            return 1;
        }
        
        // Example 2: Continue mining for several cycles
        std::cout << "\n2. Continue mining for 5 cycles..." << std::endl;
        for (int i = 0; i < 5; i++) {
            // Wait for next cycle tick (3 ticks later)
            waitForTicks(3);
            
            result = client.continueMining(securityDeposit);
            if (result.success) {
                std::cout << "✓ Cycle " << (i+1) << " mining: " << result.transactionId << std::endl;
            } else {
                std::cout << "✗ Cycle " << (i+1) << " failed: " << result.errorMessage << std::endl;
            }
            
            // Show mining stats
            auto minerInfo = client.getMinerInfo();
            std::cout << "  Pending rewards: " << minerInfo.totalPendingRewards << " QU" << std::endl;
        }
        
        // Example 3: Stop mining
        std::cout << "\n3. Stopping mining..." << std::endl;
        waitForTicks(3);
        result = client.stopMining();
        if (result.success) {
            std::cout << "✓ Mining stopped: " << result.transactionId << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
