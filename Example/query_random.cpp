// Query random bytes from the contract
struct RandomQuery {
    static std::vector<uint8> getRandomBytes(uint32 numBytes, const m256i& nonce = {}) {
        GetRandomBytes_input input;
        input.numberOfBytes = numBytes;
        input.nonce = nonce;
        
        // Make RPC call to contract function
        auto response = makeContractQuery(
            RANDOM_CONTRACT_ID,
            GET_RANDOM_BYTES_FUNCTION,
            &input,
            sizeof(input)
        );
        
        if (response.success) {
            GetRandomBytes_output output;
            memcpy(&output, response.data, sizeof(output));
            
            std::vector<uint8> randomBytes(numBytes);
            memcpy(randomBytes.data(), output.randomBytes, numBytes);
            return randomBytes;
        }
        
        return {};
    }
    
    static ContractInfo getContractInfo() {
        GetContractInfo_input input; // Empty input
        
        auto response = makeContractQuery(
            RANDOM_CONTRACT_ID,
            GET_CONTRACT_INFO_FUNCTION,
            &input,
            sizeof(input)
        );
        
        ContractInfo info;
        if (response.success) {
            GetContractInfo_output output;
            memcpy(&output, response.data, sizeof(output));
            
            info.totalCommits = output.totalCommits;
            info.totalReveals = output.totalReveals;
            info.minimumDeposit = output.minimumSecurityDeposit;
            // ... copy other fields
        }
        
        return info;
    }
};

// Usage example
void demonstrateRandomQueries() {
    std::cout << "\n=== Querying Random Data ===" << std::endl;
    
    // Get 16 random bytes
    auto randomBytes = RandomQuery::getRandomBytes(16);
    std::cout << "Random bytes: ";
    for (uint8 byte : randomBytes) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    // Get contract statistics
    auto info = RandomQuery::getContractInfo();
    std::cout << "Contract Info:" << std::endl;
    std::cout << "  Total commits: " << info.totalCommits << std::endl;
    std::cout << "  Total reveals: " << info.totalReveals << std::endl;
    std::cout << "  Minimum deposit: " << info.minimumDeposit << " QU" << std::endl;
}