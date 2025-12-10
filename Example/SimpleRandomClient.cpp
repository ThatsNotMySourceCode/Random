#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <x86intrin.h>
extern "C" {
    #include "KangarooTwelve.h"
}

// ---- Config ----
#define NODE_IP "00.00.00.000"
#define NODE_PORT 21841
#define SC_ID "DAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAANMIG"
#define TX_TYPE_MINER 1
#define TX_TYPE_BUY   2
#define TX_TYPE_QUERYPRICE 3

#define EXTRA_DATA_SIZE_MINER 544
#define EXTRA_DATA_SIZE_BUY   16
#define EXTRA_DATA_SIZE_PRICE 12
#define SEED "yourminerseedhere"
#define REVEAL_TICKS 9

typedef unsigned char uint8;
typedef unsigned long long uint64;
typedef uint64 Bit4096Data[64];

struct Bit4096 {
    Bit4096Data data;
};

struct Id {
    uint8 bytes[32];
    Id() { std::memset(bytes, 0, 32); }
};

Bit4096 generateEntropy() {
    Bit4096 entropy;
    for (int i = 0; i < 64; ++i) {
        uint64 val;
        int success = 0;
        for (int tries = 0; tries < 10 && !success; ++tries) {
            success = _rdseed64_step(&val);
        }
        if (!success) {
            std::cerr << "RDSEED failure after 10 tries\n";
            val = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        }
        entropy.data[i] = val;
    }
    return entropy;
}

Id hashEntropy(const Bit4096& entropy) {
    Id result;
    KangarooTwelve(reinterpret_cast<const unsigned char*>(&entropy), sizeof(entropy), result.bytes, 32, NULL, 0);
    return result;
}

int getCurrentTick() {
    std::ostringstream cmd;
    cmd << "./qubic-cli -nodeip " << NODE_IP << " -getcurrenttick";
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return -1;
    char buffer[128];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    pclose(pipe);
    size_t pos = output.find("Tick:");
    if (pos != std::string::npos) return std::stoi(output.substr(pos + 5));
    return -1;
}

std::string toHex(const uint8* data, size_t sz) {
    std::ostringstream oss;
    for (size_t i = 0; i < sz; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}
std::string bit4096ToHex(const Bit4096& b) {
    return toHex(reinterpret_cast<const uint8*>(&b), sizeof(Bit4096));
}

uint64 queryPrice(uint32_t numBytes, uint64 minDeposit) {
    std::ostringstream extra;
    extra << std::hex
          << std::setw(8) << std::setfill('0') << numBytes
          << std::setw(16) << minDeposit;

    std::ostringstream cmd;
    cmd << "./qubic-cli"
        << " -nodeip " << NODE_IP
        << " -nodeport " << NODE_PORT
        << " -sendcustomfunction " << SC_ID
        << " " << TX_TYPE_QUERYPRICE << " " << EXTRA_DATA_SIZE_PRICE
        << " " << extra.str();

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return 0;
    char buffer[128];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    pclose(pipe);

    size_t pos = output.find("price:");
    if (pos != std::string::npos)
        return std::stoull(output.substr(pos + 6));
    std::cout << "Unable to parse QueryPrice output, got: " << output << std::endl;
    return 0;
}

void minerCommit(const Bit4096& revealBits, const Id& commitDigest, uint64 deposit) {
    std::ostringstream extra;
    extra << bit4096ToHex(revealBits);
    extra << toHex(commitDigest.bytes, 32);
    std::ostringstream cmd;
    cmd << "./qubic-cli"
        << " -nodeip " << NODE_IP
        << " -nodeport " << NODE_PORT
        << " -seed " << SEED
        << " -sendcustomtransaction " << SC_ID
        << " " << TX_TYPE_MINER << " " << deposit << " " << EXTRA_DATA_SIZE_MINER
        << " " << extra.str();
    std::cout << "[Miner] Commit: " << cmd.str() << std::endl;
    int r = system(cmd.str().c_str());
    if (r == 0) std::cout << "Commit TX sent\n";
    else std::cerr << "Commit TX failed\n";
}

void buyEntropyCli(uint32_t numBytes, uint64 minMinerDeposit) {
    uint64 fee = queryPrice(numBytes, minMinerDeposit);
    if (!fee) {
        std::cerr << "Could not get price from contract--aborting buy tx!" << std::endl;
        return;
    }
    std::cout << "[Buyer] Required fee for this buy: " << fee << std::endl;

    std::ostringstream extra;
    extra << std::hex
          << std::setw(8) << std::setfill('0') << numBytes
          << std::setw(16) << minMinerDeposit
          << std::string((EXTRA_DATA_SIZE_BUY-4-8)*2, '0'); // Pad to 16 bytes

    std::ostringstream cmd;
    cmd << "./qubic-cli"
        << " -nodeip " << NODE_IP
        << " -nodeport " << NODE_PORT
        << " -seed " << SEED
        << " -sendcustomtransaction " << SC_ID
        << " " << TX_TYPE_BUY << " " << fee << " " << EXTRA_DATA_SIZE_BUY
        << " " << extra.str();
    std::cout << "[Buyer] BuyEntropy: " << cmd.str() << std::endl;
    int r = system(cmd.str().c_str());
    if (r == 0) std::cout << "BuyEntropy TX sent\n";
    else std::cerr << "BuyEntropy TX failed\n";
}

void printMyCommitments(const std::string& myHexId) {
    std::ostringstream extra;
    extra << myHexId;
    std::ostringstream cmd;
    cmd << "./qubic-cli"
        << " -nodeip " << NODE_IP
        << " -nodeport " << NODE_PORT
        << " -sendcustomfunction " << SC_ID
        << " 2 32 "
        << extra.str();
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return;
    char buffer[128];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    pclose(pipe);
    std::cout << "My commitments:\n" << output << std::endl;
}

void waitForTick(int targetTick) {
    int cur = getCurrentTick();
    while (cur < targetTick) {
        std::cout << "Current Tick: " << cur << ", Waiting for Tick: " << targetTick << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cur = getCurrentTick();
    }
}

int main() {
    uint64 deposit = 100000; // 100K QU
    int cycle = 0;

    while (true) {
        // --- Commit phase ---
        Bit4096 commitEntropy = generateEntropy();
        Id nextDigest = hashEntropy(commitEntropy);
        Bit4096 zeroReveal = {};
        minerCommit(zeroReveal, nextDigest, deposit);
        int commitTick = getCurrentTick();
        int revealTick = commitTick + REVEAL_TICKS;
        std::cout << "Committed at tick: " << commitTick << ", will reveal at tick: " << revealTick << std::endl;

        // --- Wait and Reveal phase ---
        waitForTick(revealTick);
        minerCommit(commitEntropy, Id{}, 0); // reveal previous entropy, no new commit

        std::cout << "Mining cycle " << (++cycle) << " complete.\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // --- (Buy demo: request secure randomness occasionally) ---
        if (cycle % 5 == 0) {
            uint32_t wants = 32;
            uint64 minDep = 100000;
            buyEntropyCli(wants, minDep);
        }
    }
    return 0;
}
