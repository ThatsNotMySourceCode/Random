int main() {
    RandomContractClient client;
    
    try {
        std::cout << "=== Random Contract Usage Examples ===" << std::endl;
        
        // Example 1: Initial commitment
        std::cout << "\n1. Making initial commitment..." << std::endl;
        uint64 securityDeposit = 1000000; // 1 QU
        
        auto result = client.makeInitialCommit(securityDeposit);
        if (result.success) {
            std::cout << "✓ Initial commit successful: " << result.transactionId << std::endl;
        } else {
            std::cout << "✗ Initial commit failed: " << result.errorMessage << std::endl;
            return 1;
        }
        
        // Wait for confirmation and get the commitment digest
        id firstCommitmentDigest = getCommitmentDigestFromTransaction(result.transactionId);
        
        // Example 2: Wait some time, then reveal and commit again
        std::cout << "\n2. Waiting 100 ticks, then revealing and making new commitment..." << std::endl;
        waitForTicks(100);
        
        result = client.revealAndCommit(firstCommitmentDigest, securityDeposit);
        if (result.success) {
            std::cout << "✓ Reveal and commit successful: " << result.transactionId << std::endl;
        }
        
        id secondCommitmentDigest = getCommitmentDigestFromTransaction(result.transactionId);
        
        // Example 3: Continue the cycle
        std::cout << "\n3. Another reveal and commit cycle..." << std::endl;
        waitForTicks(150);
        
        result = client.revealAndCommit(secondCommitmentDigest, securityDeposit);
        id thirdCommitmentDigest = getCommitmentDigestFromTransaction(result.transactionId);
        
        // Example 4: Final reveal to get back deposit
        std::cout << "\n4. Making final reveal..." << std::endl;
        waitForTicks(200);
        
        result = client.finalReveal(thirdCommitmentDigest);
        if (result.success) {
            std::cout << "✓ Final reveal successful, security deposit returned" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}