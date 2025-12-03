void demonstrateCompleteWorkflow() {
    RandomContractClient client;
    
    std::cout << "=== Complete Random Contract Workflow ===" << std::endl;
    
    // Phase 1: Commit
    std::cout << "\nPhase 1: Initial Commitment" << std::endl;
    auto commitResult = client.makeInitialCommit(5000000); // 5 QU deposit
    std::cout << "Commitment TX: " << commitResult.transactionId << std::endl;
    
    // Phase 2: Wait for others to participate
    std::cout << "\nPhase 2: Waiting for other participants..." << std::endl;
    waitForTicks(500); // Wait 500 ticks
    
    // Phase 3: Reveal and commit cycle
    std::cout << "\nPhase 3: Reveal and new commit" << std::endl;
    id commitment1 = getCommitmentFromTx(commitResult.transactionId);
    auto revealResult = client.revealAndCommit(commitment1, 5000000);
    
    // Phase 4: Get random data
    std::cout << "\nPhase 4: Retrieving random data" << std::endl;
    m256i nonce = {}; // Could add user-specific nonce
    auto randomData = RandomQuery::getRandomBytes(32, nonce);
    
    std::cout << "Generated random data: ";
    for (int i = 0; i < 32; i++) {
        printf("%02x", randomData[i]);
    }
    std::cout << std::endl;
    
    // Phase 5: Final reveal to get deposit back
    std::cout << "\nPhase 5: Final reveal" << std::endl;
    id commitment2 = getCommitmentFromTx(revealResult.transactionId);
    waitForTicks(200);
    auto finalResult = client.finalReveal(commitment2);
    
    std::cout << "Workflow complete! Security deposits returned." << std::endl;
}