#include "RandomContractClient.cpp"

// Updated query utilities
struct RandomQuery {
    static const char* RANDOM_CONTRACT_ID = "DAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    static const uint32 GET_RANDOM_BYTES_FUNCTION = 1;
    static const uint32 GET_CONTRACT_INFO_FUNCTION = 2;
    static const uint32 BUY_ENTROPY_PROCEDURE = 2;

    // Free random bytes (read-only function)
    static std::vector<uint8> getFreeRandomBytes(uint32 numBytes, const m256i& nonce = {}) {
        GetRandomBytes_input input;
        input.numberOfBytes = numBytes;
        input.nonce = nonce;
        
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

    // Buy premium random bytes (generates revenue)
    static std::vector<uint8> buyRandomBytes(uint32 numBytes, uint64 payment) {
        BuyEntropy_input input;
        input.numberOfBytes = numBytes;
        input.nonce = {};
        
        Transaction tx;
        tx.destinationPublicKey = parsePublicKey(RANDOM_CONTRACT_ID);
        tx.amount = payment;
        tx.inputType = BUY_ENTROPY_PROCEDURE;
        tx.inputSize = sizeof(BuyEntropy_input);
        memcpy(tx.input, &input, sizeof(input));
        
        auto result = broadcastTransaction(tx);
        
        if (result.success) {
            // Extract random bytes from transaction result
            BuyEntropy_output output;
            // Parse output from transaction result...
            
            std::vector<uint8> randomBytes(numBytes);
            memcpy(randomBytes.data(), output.randomBytes, numBytes);
            return randomBytes;
        }
        
        return {};
    }
    
    static ContractInfo getContractInfo() {
        GetContractInfo_input input;
        
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
            info.revealTimeoutTicks = output.revealTimeoutTicks;
            info.totalRevenue = output.totalRevenue;
            info.currentCycle = output.currentCycle;
            // Copy other fields...
        }
        
        return info;
    }
};

void demonstrateRandomQueries() {
    std::cout << "\n=== Querying Random Data (New System) ===" << std::endl;
    
    // Get contract info first
    auto info = RandomQuery::getContractInfo();
    std::cout << "Contract Info:" << std::endl;
    std::cout << "  Current tick: " << info.currentTick << std::endl;
    std::cout << "  Current cycle: " << info.currentCycle << " (3N+" << info.currentCycle << ")" << std::endl;
    std::cout << "  Reveal timeout: " << info.revealTimeoutTicks << " ticks" << std::endl;
    std::cout << "  Total revenue: " << info.totalRevenue << " QU" << std::endl;
    std::cout << "  Active miners: " << info.activeCommitments << std::endl;
    
    // Get free random bytes
    auto freeRandomBytes = RandomQuery::getFreeRandomBytes(16);
    std::cout << "\nFree random bytes: ";
    for (uint8 byte : freeRandomBytes) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    // Buy premium random bytes (generates revenue for miners/shareholders)
    std::cout << "\nBuying 8 premium random bytes (800 QU)..." << std::endl;
    auto premiumBytes = RandomQuery::buyRandomBytes(8, 800); // 100 QU per byte
    std::cout << "Premium random bytes: ";
    for (uint8 byte : premiumBytes) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
}

struct ContractInfo {
    uint64 totalCommits;
    uint64 totalReveals;
    uint64 minimumDeposit;
    uint32 revealTimeoutTicks;
    uint64 totalRevenue;
    uint32 currentTick;
    uint32 currentCycle;
    uint32 activeCommitments;
};
