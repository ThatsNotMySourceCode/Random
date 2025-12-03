struct DICE_GAME : public ContractBase
{
public:
    struct RollDice_input {
        uint8 randomBytes[4];  // Only need 4 bytes for dice
        uint32 prediction;     // Player's prediction (1-6)
    };
    
    struct RollDice_output {
        uint32 diceResult;
        bool playerWon;
        uint64 payout;
    };

private:
    PUBLIC_PROCEDURE(RollDice) {
        const auto& input = qpi.input<RollDice_input>();
        auto player = qpi.invocator();
        auto bet = qpi.invocationReward();
        
        // Validate prediction
        if (input.prediction < 1 || input.prediction > 6) {
            return;
        }
        
        // Generate dice roll (1-6)
        uint32 diceRoll = generateDiceRoll(input.randomBytes);
        
        // Check if player won
        bool won = (diceRoll == input.prediction);
        
        if (won) {
            // Pay out 5x bet (house edge)
            uint64 payout = bet * 5;
            qpi.transfer(player, payout);
        }
        // If lost, contract keeps the bet
    }

private:
    uint32 generateDiceRoll(const uint8* randomBytes) {
        // Convert 4 bytes to uint32
        uint32 randomValue = (randomBytes[0] << 24) | 
                            (randomBytes[1] << 16) | 
                            (randomBytes[2] << 8) | 
                             randomBytes[3];
        
        // Map to 1-6 range
        return (randomValue % 6) + 1;
    }
    
    // More fair dice roll using rejection sampling
    uint32 generateFairDiceRoll(const uint8* randomBytes) {
        // Use rejection sampling for perfect fairness
        const uint32 maxValidValue = 252; // Largest multiple of 6 ≤ 255
        
        for (int i = 0; i < 4; i++) {
            if (randomBytes[i] < maxValidValue) {
                return (randomBytes[i] % 6) + 1;
            }
        }
        
        // Fallback if all bytes rejected
        return generateDiceRoll(randomBytes);
    }
};