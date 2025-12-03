class RandomNumberGenerator {
public:
    // Method 1: Simple modulo (has slight bias for small ranges)
    static uint32 simpleRandom0to100(const uint8* randomBytes) {
        uint32 randomValue = 0;
        // Use first 4 bytes to create uint32
        randomValue = (randomBytes[0] << 24) | 
                     (randomBytes[1] << 16) | 
                     (randomBytes[2] << 8) | 
                      randomBytes[3];
        
        return randomValue % 101; // 0-100 inclusive
    }
    
    // Method 2: Rejection sampling (unbiased)
    static uint32 unbiasedRandom0to100(const uint8* randomBytes, uint32 bytesAvailable) {
        const uint32 range = 101; // 0-100 inclusive
        const uint32 maxValid = (UINT32_MAX / range) * range;
        
        for (uint32 i = 0; i + 3 < bytesAvailable; i += 4) {
            uint32 randomValue = (randomBytes[i] << 24) | 
                                (randomBytes[i+1] << 16) | 
                                (randomBytes[i+2] << 8) | 
                                 randomBytes[i+3];
            
            // Reject values that would cause bias
            if (randomValue < maxValid) {
                return randomValue % range;
            }
        }
        
        // Fallback to simple method if all values rejected
        return simpleRandom0to100(randomBytes);
    }
    
    // Method 3: Use multiple bytes for better distribution
    static uint32 enhancedRandom0to100(const uint8* randomBytes) {
        // Use 8 bytes to create uint64 for better distribution
        uint64 randomValue = 0;
        for (int i = 0; i < 8; i++) {
            randomValue = (randomValue << 8) | randomBytes[i];
        }
        
        return randomValue % 101;
    }
};