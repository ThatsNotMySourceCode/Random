class TrueRandomGenerator {
public:
    // Combine multiple entropy sources
    static uint32 generateSecureRandom0to100() {
        // 1. Get random bytes from Random contract
        auto randomBytes = getRandomBytesFromContract(32);
        
        // 2. Add contract-specific entropy
        uint32 blockTick = qpi.tick();
        id contractId = qpi.contractId();
        
        // 3. Combine entropy sources
        uint8 combinedEntropy[40];
        qpi.copyMem(combinedEntropy, randomBytes.data(), 32);
        qpi.copyMem(combinedEntropy + 32, &blockTick, 4);
        qpi.copyMem(combinedEntropy + 36, &contractId, 4);
        
        // 4. Hash the combined entropy
        id finalHash;
        qpi.computeHash(combinedEntropy, 40, &finalHash);
        
        // 5. Convert to number 0-100
        uint32 result = 0;
        for (int i = 0; i < 4; i++) {
            result = (result << 8) | finalHash.bytes[i];
        }
        
        return result % 101;
    }
    
    // Prevent prediction attacks
    static bool validateRandomness(const uint8* randomBytes, uint64 entropyVersion) {
        // Check entropy version is recent
        static uint64 lastVersion = 0;
        if (entropyVersion <= lastVersion) {
            return false; // Old or reused entropy
        }
        lastVersion = entropyVersion;
        
        // Basic entropy check (not all zeros)
        bool hasEntropy = false;
        for (int i = 0; i < 32; i++) {
            if (randomBytes[i] != 0) {
                hasEntropy = true;
                break;
            }
        }
        
        return hasEntropy;
    }
};