# Qubic Random Smart Contract (Random SC)

---

## 🎲 High-Security, Decentralized Randomness & Entropy Market

Supports both **entropy mining** (commit–reveal protocol) and **secure BuyEntropy-based random number sales**. Pricing is transparent and on-chain, and revenue is fairly distributed to entropy miners and Qubic shareholders.

---

### Basic Mining Logic

- Call `RevealAndCommit()` to participate.
    - On your first call, send `committedDigest` with a hash of your random bits, plus a `deposit` as `amount`, and set `revealedBits` to zeros.
    - On the next call, send your previous entropy as `revealedBits` (to reveal and reclaim your deposit), and simultaneously commit to new entropy (hash as `committedDigest`). Repeat this cycle.
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

- **Reveal must happen within 9 ticks** (configurable). Late = lose deposit (unless the tick is empty; then your deposit is refunded).
- **Deposit is chosen by miner** (minimum: 1 QU, then 10, 100, etc). Higher deposit increases miner's ranking and reward share.

---

### Entropy Buyers / Random Consumers

- Anyone can call `BuyEntropy` to buy random bytes from the pool.
    - Parameters let you specify your security level:
        - `numberOfBytes` (1–32)
        - `minMinerDeposit`: Require each contributing miner to have at least this deposit (set by the buyer for desired security).
    - Contract **returns a minimum fee requirement** (use `QueryPrice`) so you always know exactly what to pay!

- **Fairness and Security**:
    - Random bytes are always generated using the entropy pool as it existed **2 ticks ago** (ensures unpredictability even by the current tick leader).
    - If there was no eligible miner with sufficient deposit in recent history, the sale fails (no bytes sold, no fee taken).

- **Revenue:**
    - 50% of all `BuyEntropy` payment goes to recent miners (based on the pool used for buyer’s request)
    - 50% goes to Qubic shareholders
    - **Lost security deposits** (from missed or late reveals, except empty tick) go 100% to Qubic shareholders.

**Querying price on-chain (C++/CLI):**
```cpp
uint64_t fee = query_price(numberOfBytes, minMinerDeposit);
// Then call buy_entropy_cli(numberOfBytes, minMinerDeposit)
```

---

### Security & Economic Features

- **Economic Security:**
    - Miners must risk a security deposit (minimum: 1 QU, then 10, 100, 1000, ...). Any power-of-ten value is valid
    - Strict 9-tick reveal deadline: fail to reveal in time and your deposit is lost (unless the tick is empty; then it is refunded).
    - Lost deposits go to Qubic holders if miner fails to reveal (unless the tick is empty—then deposits are refunded)
    - All revenue is split fairly and transparently

- **Cryptographic Security:**
    - Commit–reveal, no front-running or grinding
    - True hardware entropy (`_rdseed64_step()`)
    - Global entropy pool: all miner inputs are XOR’d
    - All entropy sales use the pool "as of two ticks ago" for front-running resistance

- **No Attack Vectors:**
    - Buyers can't skip security: randomness is only sold if a qualifying honest, high-deposit miner participated recently
    - Fully public and fair randomness generation

- **Decentralized Market:**
    - Any number of miners can participate
    - Anyone can buy secure random bytes, with transparent, on-chain pricing
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
```cpp
uint64_t fee = query_price(numberOfBytes, minMinerDeposit);
buy_entropy_cli(numberOfBytes, minMinerDeposit, fee);
```

---

## Smart Contract API

- `RevealAndCommit`: For miners to commit/reveal entropy. Requires deposit.
- `BuyEntropy`: For anyone to purchase random bytes with on-chain proof of deposit level. Requires on-chain price (use `QueryPrice` before sending).
- `QueryPrice`: Public function returning the exact fee for any BuyEntropy request.
- `GetContractInfo`, `GetUserCommitments`: Read-only status/info functions for UIs/wallets/bots.

---

#### All flows are cryptographically sound, economically secure, and truly decentralized!

- **Unpredictability:** Random bytes depend on unrevealed future entropy and are based on the pool as of two ticks ago.
- **Fairness:** Honest participants always get fair access and payment.
- **Transparency:** All pricing formulas and contract parameters are publicly queryable.
- **Sustainability:** Economic model rewards both honest entropy contribution and Qubic shareholders.

**Any application needing cryptographically secure randomness can safely use this contract.**

---

## Qubic Random SC

You now have a secure, transparent, and incentive-aligned decentralized randomness engine with built-in market and fee revenue. For full code, see `Random.h`, `SimpleRandomClient_cli.cpp`, and demos.


## Contract configuration variables

- **minimumSecurityDeposit** (uint64):
The minimum allowed deposit for entropy miners. Must be at least 1 QU, and only powers of ten (1, 10, 100, 1000, ...) are valid. Miners can choose any allowed deposit amount as their security level.
- **revealTimeoutTicks** (uint32):
Number of ticks a miner has to reveal their previously committed entropy after committing. Default is 9. If a reveal is not submitted on time, the deposit is forfeited (unless the tick is empty, in which case it is refunded).
- **pricePerByte** (uint64):
The base fee (in QU) charged per random byte for the BuyEntropy procedure. Default is 10 QU per byte.
- **priceDepositDivisor** (uint64):
Used to scale BuyEntropy price based on the minimum miner deposit required by the buyer. The effective price =
`pricePerByte * numberOfBytes * (minMinerDeposit / priceDepositDivisor + 1)`
- **validDepositAmounts[16]** (uint64[]):
List of all allowed deposit amounts (powers of ten), used to validate miner deposits and enforce the security level spectrum.
