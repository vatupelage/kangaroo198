# Server Collision Detection Debug Guide

## Problem Summary

**Observed Behavior:**
- Server received 35 DPs (4.3x more than expected 8 DPs)
- Client performed 2^40.60 operations (11.5x more than needed 2^37)
- NO collision detected (statistically impossible - probability ~0.0001%)

**Expected Behavior:**
- With proper collision detection, key should be found at ~2^37 operations
- With 35 DPs, collision should have been detected long ago

## Comprehensive Debug Logging Added

### 1. Server DP Processing (Thread.cpp)

**Every DP logged (first 10 and every 100th):**
```
[Server DEBUG] DP #1: kIdx=12345, kType=0 (TAME) | Stats: TAME=1, WILD=0
[Server DEBUG] DP #2: kIdx=12346, kType=1 (WILD) | Stats: TAME=1, WILD=1
```

**Periodic statistics (every 10 seconds):**
```
[Server STATS] Total DPs processed: 35 (TAME: 17, WILD: 18)
[Server STATS] Hash table entries: 32, Same-herd collisions: 3
[Server STATS] Distribution: TAME=48.6%, WILD=51.4%
```

### 2. Hash Table Operations (HashTable.cpp)

**Every Add() call (first 20 and every 50th):**
```
[HashTable::Add #1] type=0 (TAME), hash=54321, x=0000000000004C5C...
[HashTable::Add #1] Bucket has 0 items currently
[HashTable::Add(h=54321)] First item in bucket, adding without check
[HashTable::Add #1] Result: ADD_OK
```

**Binary search through bucket:**
```
[HashTable::Add(h=54321)] Searching through 3 items in bucket
[HashTable::Add(h=54321)] Compare with item 1: comp=-1
[HashTable::Add(h=54321)] Compare with item 0: comp=0
```

**When match found:**
```
[HashTable] *** MATCH FOUND *** Same X coordinate!
[HashTable]   Existing entry: type=0 (TAME), x=0000000000004C5C...
[HashTable]   New entry:      type=1 (WILD), x=0000000000004C5C...
[HashTable]   Existing dist:  000000000000000000000000001A2B3C4D
[HashTable]   New dist:       000000000000000000000000005E6F7A8B
[HashTable] -> Different distance = COLLISION (cross-herd)!
[HashTable] -> Types: Existing=0 (TAME), New=1 (WILD)
```

### 3. Collision Validation (Kangaroo.cpp)

**When collision check runs:**
```
[CollisionCheck] Checking collision: type1=0 (TAME), type2=1 (WILD)
[CollisionCheck] -> Different herd collision (TAME vs WILD), checking key...
[CollisionCheck] KEY FOUND!
```

## Diagnostic Scenarios

### Scenario A: All DPs Are Same Type (Kangaroo Creation Bug)

**Symptoms:**
```
[Server DEBUG] DP #1: kIdx=0, kType=0 (TAME)
[Server DEBUG] DP #2: kIdx=2, kType=0 (TAME)
[Server DEBUG] DP #3: kIdx=4, kType=0 (TAME)
[Server STATS] Distribution: TAME=100.0%, WILD=0.0%
```

**Diagnosis:** Client is generating only TAME kangaroos (or only WILD).

**Root Cause:**
- `CreateHerd()` function not alternating types correctly
- `kIdx` values all even or all odd
- GPU kernel setting wrong kIdx values

**Fix:** Check `CreateHerd()` in Kangaroo.cpp:871-939 and GPU kernel kIdx calculation.

---

### Scenario B: Hash Function Broken (Never Hashes to Same Bucket)

**Symptoms:**
```
[HashTable::Add #1] hash=12345, Bucket has 0 items
[HashTable::Add #2] hash=67890, Bucket has 0 items
[HashTable::Add #3] hash=11111, Bucket has 0 items
(All different hashes, buckets never have >1 item)
```

**Diagnosis:** Hash function distributes too uniformly, never putting DPs in same bucket.

**Root Cause:**
- Hash function in HashTable.cpp:246 may be using wrong fields
- XOR of all limbs creates too much entropy

**Fix:** Simplify hash to use fewer bits (e.g., only x->i64[0] % HASH_SIZE).

---

### Scenario C: Comparison Function Broken (Never Finds Match)

**Symptoms:**
```
[HashTable::Add(h=54321)] Searching through 5 items in bucket
[HashTable::Add(h=54321)] Compare with item 2: comp=-1
[HashTable::Add(h=54321)] Compare with item 1: comp=1
[HashTable::Add(h=54321)] Compare with item 0: comp=-1
(Never sees comp==0 despite same points existing)
```

**Diagnosis:** Binary search comparison never returns 0 even for matching x-coordinates.

**Root Cause:**
- `compare()` function in HashTable.cpp:311-333 has bug
- int256_t comparison logic incorrect

**Fix:** Verify compare() uses correct limb order and comparison operators.

---

### Scenario D: Matches Found But Same Type (Type Calculation Bug)

**Symptoms:**
```
[HashTable] *** MATCH FOUND *** Same X coordinate!
[HashTable]   Existing entry: type=0 (TAME)
[HashTable]   New entry:      type=0 (TAME)
[HashTable] -> Same distance = DUPLICATE (same herd collision)

Or:

[HashTable] -> Different distance = COLLISION (cross-herd)!
[HashTable] -> Types: Existing=0 (TAME), New=0 (TAME)
[CollisionCheck] Same herd collision (both TAME), rejecting
```

**Diagnosis:** Collisions are being detected, but both DPs have same type.

**Root Cause:**
- kIdx values don't alternate properly
- `kType = kIdx % 2` calculation gives same result for all kIdx
- kIdx not being transmitted correctly over network

**Fix:**
- Verify DP serialization includes kIdx in Network.cpp:1233-1249
- Verify kIdx is set correctly from GPU in GPUEngine.cu:684

---

### Scenario E: Matches Found, Types Correct, But Key Not Found

**Symptoms:**
```
[HashTable] *** MATCH FOUND *** Same X coordinate!
[HashTable]   Existing entry: type=0 (TAME)
[HashTable]   New entry:      type=1 (WILD)
[HashTable] -> Different distance = COLLISION (cross-herd)!
[CollisionCheck] Different herd collision (TAME vs WILD), checking key...
[CollisionCheck] Unexpected wrong collision, reset kangaroo!
```

**Diagnosis:** Collision detection working, but calculated private key is incorrect.

**Root Cause:**
- Distance calculation wrong
- Wild kangaroo offset not applied correctly
- Private key calculation formula wrong

**Fix:** Check `CheckKey()` function and wild offset handling.

---

## Expected Correct Behavior

With working collision detection, you should see:

**First 10 DPs:**
```
[Server DEBUG] DP #1: kIdx=0, kType=0 (TAME) | Stats: TAME=1, WILD=0
[Server DEBUG] DP #2: kIdx=1, kType=1 (WILD) | Stats: TAME=1, WILD=1
[Server DEBUG] DP #3: kIdx=2, kType=0 (TAME) | Stats: TAME=2, WILD=1
[Server DEBUG] DP #4: kIdx=3, kType=1 (WILD) | Stats: TAME=2, WILD=2
...
[Server DEBUG] DP #10: kIdx=9, kType=1 (WILD) | Stats: TAME=5, WILD=5
```

**Hash table growth:**
```
[HashTable::Add #1] Result: ADD_OK (table size: 1)
[HashTable::Add #2] Result: ADD_OK (table size: 2)
...
[HashTable::Add #15] Result: ADD_OK (table size: 14)
```

**Eventually, collision:**
```
[HashTable] *** MATCH FOUND *** Same X coordinate!
[HashTable]   Existing entry: type=0 (TAME), x=0000000000004C5C...
[HashTable]   New entry:      type=1 (WILD), x=0000000000004C5C...
[HashTable] -> Different distance = COLLISION (cross-herd)!
[CollisionCheck] Different herd collision (TAME vs WILD), checking key...
[CollisionCheck] KEY FOUND!

Key# 0 Pub: 0x03726B574F193E374686D8E12BC6E4142ADEB06770E0A2856F5E4AD89F66044755
     Priv: 0x4C5CE114686A1336E07
```

## Testing Instructions

1. **Run server with debug logging:**
   ```bash
   ./kangaroo-256 -s -d 35 -w save.work -wi 300 -o result.txt -sp 17403 75.txt 2>&1 | tee server-debug.log
   ```

2. **Run client:**
   ```bash
   ./kangaroo-256 -t 0 -gpu -gpuId 0 -w client.work -wi 600 -c <SERVER_IP> -sp 17403
   ```

3. **Monitor server output in real-time:**
   ```bash
   tail -f server-debug.log
   ```

4. **Look for patterns:**
   - Are both TAME and WILD appearing?
   - Are hash buckets being populated?
   - Is binary search finding matches?
   - Are collisions detected but rejected?

5. **After 1-2 minutes, check statistics:**
   ```bash
   grep "Server STATS" server-debug.log | tail -5
   ```

6. **Search for collision events:**
   ```bash
   grep "MATCH FOUND" server-debug.log
   grep "COLLISION" server-debug.log
   ```

## Analysis Steps

1. **Check DP type distribution:**
   ```bash
   grep "Server STATS" server-debug.log | grep "Distribution"
   ```
   - Should see ~50% TAME, ~50% WILD
   - If 100%/0%, kangaroo creation is broken

2. **Check hash table is populated:**
   ```bash
   grep "Hash table entries" server-debug.log | tail -1
   ```
   - Should grow continuously (not stay at 0)
   - With 35 DPs, should have 20-30 entries

3. **Check if matches are found:**
   ```bash
   grep "MATCH FOUND" server-debug.log | wc -l
   ```
   - Should be > 0 if collision detection working
   - If 0, hash or comparison broken

4. **Check collision types:**
   ```bash
   grep "Types: Existing" server-debug.log
   ```
   - Should see both (TAME, WILD) and (WILD, TAME)
   - If only same types, kType calculation broken

## Success Criteria

Working collision detection will show:
✓ Both TAME and WILD DPs (~50/50 distribution)
✓ Hash table grows with DPs
✓ Binary search finds matches (comp==0)
✓ Matches have different types (cross-herd)
✓ CollisionCheck validates and finds key

Broken collision detection will show one of:
✗ All DPs same type (100% TAME or 100% WILD)
✗ Hash buckets never have multiple items
✗ Binary search never finds matches
✗ Matches found but same type
✗ Collision detected but key calculation fails

The debug output will pinpoint the exact failure mode and allow targeted fixing.
