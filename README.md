# Qubic Random Smart Contract (Random SC)

---

## 🎲 High-Security, Decentralized Randomness & Entropy Market

Supports both **entropy mining** (commit–reveal protocol) and **secure BuyEntropy-based random number sales**. Pricing is transparent and on-chain, and revenue is fairly distributed to entropy miners and Qubic shareholders.

---

### Basic Mining Logic

- Call `RevealAndCommit()` to participate.
    - On your first call, send `committedDigest` with a hash of your random bits, plus a `deposit` as `amount`, and set `revealedBits` to zeros.
    - On the next call, send your previous entropy as `revealedBits` (to reveal and reclaim your deposit), and simultaneously commit to new entropy (hash as `committedDigest`). Repeat.
    - To stop mining, just reveal with `committedDigest` as zeros and deposit `0`.

C++ sample for entropy:
```cpp
uint64 val;
_rdseed64_step(&val); // Use hardware entropy!

bit_4096 entropy = ... // fill using 64 x _rdseed64_step()
id digest = hashEntropy(entropy);

// RevealAndCommit_input for C++
struct RevealAndCommit_input {
    bit_4096 revealedBits;
    id committedDigest;
};
```

- **Reveal must happen within 9 ticks** (configurable). Late = lose deposit.
- **Deposit is chosen by miner** (minimum: 1k QU), higher deposit increases miner's ranking and reward share.

---

### Entropy Buyers / Random Consumers

- Anyone can call `BuyEntropy` to buy random bytes from the current pool.
    - Parameters let you specify your security level:
        - `numberOfBytes` (1–32)
        - `minFreshReveals`: Require this many fresh (recent) entropy miner reveals
        - `minMinerDeposit`: Require each contributing miner to have at least this deposit
    - Contract **returns a minimum fee requirement** (use `QueryPrice`) so you always know what to pay!

- Revenue:
    - 50% of all `BuyEntropy` payment goes to recent miners (based on pool used for buyer’s request)
    - 50% goes to Qubic shareholders
    - Lost security deposits (from missed or late reveals) go 100% to Qubic shareholders.

**Querying price on-chain (C++/CLI):**
```cpp
uint64_t fee = query_price(numberOfBytes, minFreshReveals, minMinerDeposit);
// Then call buy_entropy_cli(numBytes, minFreshReveals, minMinerDeposit)
```

---

### Security & Economic Features

- **Economic Security:**
    - Miners must risk real deposits (1K QU+), with a strict 9-tick reveal deadline
    - Lost deposits and revenue are distributed fairly

- **Cryptographic Security:**
    - Commit–reveal, no front-running or grinding
    - True hardware entropy (`_rdseed64_step()`)
    - Global entropy pool: all miner inputs are XOR’d

- **No Attack Vectors:**
    - Buyers can't skip miner participation: randomness is only sold if enough honest, high-deposit miners participated recently
    - All randomness generation is public and fair

- **Decentralized Market:**
    - Any number of miners can participate
    - Anyone can buy secure random bytes, with transparent, on-chain configurable pricing
    - Three parallel flows recommended to maximize system entropy

- **Economic Incentives:**
    - Miners can earn not just their deposit back, but also a share of buyer revenue
    - Shareholders (QU holders) receive ongoing buy fees and lost deposits

---

### Mining / Buying Example Flow

**Commit:** (Tick 5)
```
RevealAndCommit(zero, hash(E1), deposit)
```
**Reveal + Commit:** (Tick 14)
```
RevealAndCommit(E1, hash(E2), deposit)
```
**Final Reveal / Stop Mining:** (Tick 23)
```
RevealAndCommit(E2, zero, 0)
```

**Buy entropy (as a user):**
```
fee = QueryPrice(numBytes, minFreshReveals, minMinerDeposit)
BuyEntropy(numBytes, minFreshReveals, minMinerDeposit, fee)
```

---

## Smart Contract API

- `RevealAndCommit`: For miners to commit/reveal entropy. Requires deposit.
- `BuyEntropy`: For anyone to purchase random bytes with on-chain proof of freshness/security. Requires on-chain price (use `QueryPrice` before sending).
- `QueryPrice`: Public function returning the exact fee for any BuyEntropy request.
- `GetContractInfo`, `GetUserCommitments`: Read-only status/info functions for UIs/wallets/bots.

---

#### All flows are cryptographically sound, economically secure, and truly decentralized!

- **Unpredictability:** Random bytes depend on unrevealed future entropy.
- **Fairness:** Honest participants always get fair access and payment.
- **Transparency:** All pricing formulas and contract parameters are publicly queryable.
- **Sustainability:** Economic model rewards both honest entropy contribution and Qubic shareholders.

**Any application needing cryptographically secure randomness can safely use this contract.**

---

## Qubic Random SC

You now have a secure, transparent, and incentive-aligned decentralized randomness engine with built-in market and fee revenue. For full code, see `Random.h`, `SimpleRandomClient_cli.cpp`, and demos.
