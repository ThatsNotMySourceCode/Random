#include "RandomContractClient.cpp"

void demonstrateThreeFlowMining() {
    std::cout << "=== Three-Flow Entropy Mining Demo ===" << std::endl;
    
    // Create three clients for different cycles
    RandomContractClient client0, client1, client2;
    uint64 deposit = 100000; // 100K QU
    
    uint32 currentTick = getCurrentTick();
    std::cout << "Starting at tick " << currentTick << std::endl;
    
    // Start three parallel mining flows
    std::cout << "\n=== Starting Three Mining Flows ===" << std::endl;
    
    // Flow 1: 3N cycle
    std::cout << "Flow 1 (3N): ";
    auto result0 = client0.startMining(deposit, 0);
    std::cout << (result0.success ? "✓" : "✗") << std::endl;
    
    waitForTicks(1);
    
    // Flow 2: 3N+1 cycle  
    std::cout << "Flow 2 (3N+1): ";
    auto result1 = client1.startMining(deposit, 1);
    std::cout << (result1.success ? "✓" : "✗") << std::endl;
    
    waitForTicks(1);
    
    // Flow 3: 3N+2 cycle
    std::cout << "Flow 3 (3N+2): ";
    auto result2 = client2.startMining(deposit, 2);
    std::cout << (result2.success ? "✓" : "✗") << std::endl;
    
    // Run all three flows for several cycles
    std::cout << "\n=== Running Parallel Mining (10 cycles) ===" << std::endl;
    
    for (int cycle = 0; cycle < 10; cycle++) {
        waitForTicks(1);
        currentTick = getCurrentTick();
        uint32 activeCycle = currentTick % 3;
        
        std::cout << "Tick " << currentTick << " (cycle " << activeCycle << "): ";
        
        if (activeCycle == 0) {
            auto result = client0.continueMining(deposit);
            std::cout << "Flow 1 " << (result.success ? "✓" : "✗");
        } else if (activeCycle == 1) {
            auto result = client1.continueMining(deposit);
            std::cout << "Flow 2 " << (result.success ? "✓" : "✗");
        } else {
            auto result = client2.continueMining(deposit);
            std::cout << "Flow 3 " << (result.success ? "✗" : "✗");
        }
        
        // Show entropy being generated continuously
        auto randomBytes = RandomQuery::getFreeRandomBytes(4);
        printf(" | Random: %02x%02x%02x%02x", 
               randomBytes[0], randomBytes[1], randomBytes[2], randomBytes[3]);
        std::cout << std::endl;
    }
    
    // Show mining rewards
    std::cout << "\n=== Mining Rewards ===" << std::endl;
    auto info0 = client0.getMinerInfo();
    auto info1 = client1.getMinerInfo();
    auto info2 = client2.getMinerInfo();
    
    std::cout << "Flow 1 total rewards: " << info0.totalPendingRewards << " QU" << std::endl;
    std::cout << "Flow 2 total rewards: " << info1.totalPendingRewards << " QU" << std::endl;
    std::cout << "Flow 3 total rewards: " << info2.totalPendingRewards << " QU" << std::endl;
    
    // Stop all mining
    std::cout << "\n=== Stopping All Mining ===" << std::endl;
    client0.stopMining();
    waitForTicks(1);
    client1.stopMining();
    waitForTicks(1);
    client2.stopMining();
    
    std::cout << "All mining flows stopped. Final entropy pool established." << std::endl;
}

void demonstrateRevenueSplit() {
    std::cout << "\n=== Revenue Distribution Demo ===" << std::endl;
    
    // Buy some entropy to generate revenue
    std::cout << "Purchasing entropy to generate revenue..." << std::endl;
    
    RandomQuery::buyRandomBytes(32, 3200); // 32 bytes at 100 QU each = 3200 QU
    RandomQuery::buyRandomBytes(16, 1600); // 16 bytes at 100 QU each = 1600 QU
    RandomQuery::buyRandomBytes(8, 800);   // 8 bytes at 100 QU each = 800 QU
    
    // Total revenue: 5600 QU
    // 50% to miners: 2800 QU
    // 50% to shareholders: 2800 QU
    
    auto info = RandomQuery::getContractInfo();
    std::cout << "Total contract revenue: " << info.totalRevenue << " QU" << std::endl;
    std::cout << "Revenue split:" << std::endl;
    std::cout << "  Miners: " << (info.totalRevenue * 50) / 100 << " QU" << std::endl;
    std::cout << "  Shareholders: " << (info.totalRevenue * 50) / 100 << " QU" << std::endl;
    
    std::cout << "\nRevenue will be distributed in next EndEpoch() call." << std::endl;
}

void demonstrateCompleteWorkflow() {
    std::cout << "=== Complete New Random Contract Workflow ===" << std::endl;
    
    // Phase 1: Three-flow mining
    demonstrateThreeFlowMining();
    
    // Phase 2: Revenue generation and distribution
    demonstrateRevenueSplit();
    
    // Phase 3: Show final statistics
    auto info = RandomQuery::getContractInfo();
    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Total commits: " << info.totalCommits << std::endl;
    std::cout << "Total reveals: " << info.totalReveals << std::endl; 
    std::cout << "Total revenue: " << info.totalRevenue << " QU" << std::endl;
    std::cout << "Reveal timeout: " << info.revealTimeoutTicks << " ticks (3-tick system)" << std::endl;
    
    std::cout << "\nWorkflow complete! Continuous entropy generation established." << std::endl;
}
