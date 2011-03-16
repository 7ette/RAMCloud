/* Copyright (c) 2009-2010 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RAMCLOUD_HASHTABLE_H
#define RAMCLOUD_HASHTABLE_H

#include <xmmintrin.h>

#include "Common.h"
#include "CycleCounter.h"
#include "LargeBlockOfMemory.h"

namespace RAMCloud {

/**
 * A map from (uint64_t, uint64_t) tuples to type T addresses. Effectively
 * this provides a 128-bit integer to pointer map. We will refer to these
 * 128-bit tuples as ``keys'' and the things, T, they point to as ``referants''.
 *
 * This is used, for instance, in resolving most object-level %RAMCloud
 * requests. I.e., to read and write a %RAMCloud object, this lets you find the
 * location of the the object: (key2, tableID) -> Object*.
 *
 * This code is not thread-safe.
 *
 * \section impl Implementation Details
 *
 * The HashTable is an array of #buckets, indexed by the hash of the two
 * uint64_t keys. Each bucket consists of one or more chained \link CacheLine
 * CacheLines\endlink, the first of which lives inline in the array of buckets.
 * Each cache line consists of several hash table \link Entry Entries\endlink
 * in no particular order.
 *
 * If there are too many hash table entries to fit in the bucket's first cache
 * line, additional overflow cache lines are allocated (outside of the array of
 * buckets). In this case, the last hash table entry in each of the
 * non-terminal cache lines has a pointer to the next cache line instead of a
 * pointer to a referant.
 */
template<typename T>
class HashTable {

  public:

    /**
     * Keeps track of statistics for a density distribution of frequencies.
     * See #HashTable::PerfCounters::lookupEntryDist for an example, where it
     * is used to keep track of the distribution of the number of cycles a
     * method takes.
     *
     * See HashTableBenchmark.cc for code to generate a histogram from this.
     *
     * TODO(ongaro): Generalize and move to a utils file.
     */
    struct PerfDistribution {
        /**
         * The number of bins in which to categorize samples.
         * See #bins
         */
        static const uint64_t NBINS = 5000;

        /**
         * The number of distinct integer values that are recorded in each bin.
         * See #bins.
         */
        static const uint64_t BIN_WIDTH = 10;

        /**
         * The frequencies of samples that fall into each bin.
         * The first bin will have the number of samples between 0 (inclusive)
         * and BIN_WIDTH (exclusive), the second between BIN_WIDTH and
         * BIN_WIDTH * 2, etc.
         * Bins is allocated on the heap because it ends up being several tens
         * of kilobytes in size. This in turn makes HashTable too big, which
         * makes Table too big, which makes Master too big, which causes a
         * segfault before the start of main.
         */
        uint64_t *bins;

        /**
         * The frequency of samples that exceeded the highest bin.
         * This is equivalent to the sum of the values in all bins beyond the
         * end of the \a bins array.
         */
        uint64_t binOverflows;

        /**
         * The minimum sample encountered.
         * This will be <tt>~OUL</tt> if no samples were stored.
         */
        uint64_t min;

        /**
         * The maximum sample.
         * This will be <tt>OUL</tt> if no samples were stored.
         */
        uint64_t max;

        /**
         * Constructor for HashTable::PerfDistribution.
         */
        PerfDistribution()
            : bins(NULL), binOverflows(0), min(~0UL), max(0UL)
        {
            bins = new uint64_t[NBINS];
            memset(bins, 0, sizeof(uint64_t) * NBINS);
        }

        ~PerfDistribution()
        {
            delete[] bins;
            bins = NULL;
        }

        /**
         * Record a sampled value by updating the distribution statistics.
         * \param[in] value
         *      The value sampled.
         */
        void
        storeSample(uint64_t value)
        {
            if (value / BIN_WIDTH < NBINS)
                bins[value / BIN_WIDTH]++;
            else
                binOverflows++;

            if (value < min)
                min = value;
            if (value > max)
                max = value;
        }

        void
        reset()
        {
            memset(bins, 0, sizeof(uint64_t) * NBINS);
            binOverflows = 0;
            min = ~0UL;
            max = 0;
        }

      private:
        DISALLOW_COPY_AND_ASSIGN(PerfDistribution);
    };

    /**
     * Performance counters for the HashTable.
     */
    struct PerfCounters {
        /**
         * The number of #replace() operations.
         */
        uint64_t replaceCalls;

        /**
         * The number of #lookupEntry() operations.
         */
        uint64_t lookupEntryCalls;

        /**
         * The total number of CPU cycles spent across all #replace()
         * operations.
         */
        uint64_t replaceCycles;

        /**
         * The total number of CPU cycles spent across all #lookupEntry()
         * operations.
         */
        uint64_t lookupEntryCycles;

        /**
         * The total number of times a chain pointer was followed to another
         * CacheLine while trying to insert a new entry within #replace().
         */
        uint64_t insertChainsFollowed;

        /**
         * The total number of times a chain pointer was followed to another
         * CacheLine across all #lookupEntry() operations.
         */
        uint64_t lookupEntryChainsFollowed;

        /**
         * The total number of times there was an Entry collision across
         * all #lookupEntry() operations. This is when the buckets collide for
         * a key, and the extra disambiguation bits inside the Entry collide,
         * but the referant itself reveals that the entry does not correspond to
         * the given key.
         */
        uint64_t lookupEntryHashCollisions;

        /**
         * The distribution of CPU cycles spent for #lookupEntry() operations.
         */
        PerfDistribution lookupEntryDist;

        /**
         * Constructor for HashTable::PerfCounters.
         */
        PerfCounters()
            : replaceCalls(0), lookupEntryCalls(0), replaceCycles(0),
            lookupEntryCycles(0), insertChainsFollowed(0),
            lookupEntryChainsFollowed(0), lookupEntryHashCollisions(0),
            lookupEntryDist()
        {
        }

        /**
         * Reset all of the HashTable::PerfCounters statistics.
         */
        void
        reset()
        {
            replaceCalls = 0;
            lookupEntryCalls = 0;
            replaceCycles = 0;
            lookupEntryCycles = 0;
            insertChainsFollowed = 0;
            lookupEntryChainsFollowed = 0;
            lookupEntryHashCollisions = 0;
            lookupEntryDist.reset();
        }
    };

    /**
     * Constructor for HashTable.
     * \param[in] numBuckets
     *      The number of buckets in the new hash table. This should be a power
     *      of two.
     * \param[in] numTypes
     *      The number of different types of referants that can be put in this
     *      HashTable. If numTypes > 1, the HashTable will reserve
     *      ceil(log2(numTypes)) upper pointer bits to differentiate referants
     *      in the table. 
     * \throw Exception
     *      An exception is thrown is numBuckets is 0 or numTypes is
     *      equal to 0 or greater than MAX_NUMTYPES.
     */
    explicit HashTable(uint64_t numBuckets, uint32_t numTypes = 1)
        : numBuckets(nearestPowerOfTwo(numBuckets))
        , buckets(this->numBuckets * sizeof(CacheLine))
        , perfCounters()
        , numTypes(numTypes)
        , typeBits(0)
    {
        // HashTable<T> requires that T be a pointer. Assert that.
        {
            T assertion;
            (void)*assertion;
        }

        if (numBuckets != this->numBuckets) {
            LOG(DEBUG, "HashTable truncated to %lu buckets "
                        "(nearest power of two)", this->numBuckets);
        }

        if (numBuckets == 0)
            throw Exception(HERE, "HashTable numBuckets == 0?!");

        if (numTypes == 0 || numTypes > MAX_NUMTYPES)
            throw Exception(HERE, "HashTable numTypes > MAX_NUMTYPES");

        if (numTypes > 1) {
            uint64_t power2Types = nearestPowerOfTwo(numTypes);
            if (power2Types != numTypes) {
                power2Types <<= 1;
            }

            uint64_t typeMask = power2Types - 1;
            for (int i = 0; i < 64; i++) {
                if (typeMask & (1UL << i))
                    typeBits++;
            }
        }
    }

    /**
     * Destructor for HashTable.
     */
    ~HashTable()
    {
        // TODO(ongaro): free chained CacheLines that were allocated in insert()
    }

    /**
     * Find the address of a referant given the key.
     * \param[in] key1
     *      The first 64 bits of the key.
     * \param[in] key2
     *      The second 64 bits of the key.
     * \param[out] retType
     *      If not NULL, return the type of the referant looked
     *      up. This is only valid if the constructor was called
     *      with numTypes > 1.
     * \return
     *      The address of the referant, or \a NULL if one doesn't exist.
     */
    T
    lookup(uint64_t key1, uint64_t key2, uint8_t *retType = NULL)
    {
        uint64_t secondaryHash;
        Entry *entry;
        CacheLine *bucket = findBucket(key1, key2, &secondaryHash);
        entry = lookupEntry(bucket, secondaryHash, key1, key2);
        if (entry == NULL)
            return NULL;
        return entry->getReferant(typeBits, retType);
    }

    /**
     * Remove a referant from the hash table.
     * \param[in] key1
     *      The first 64 bits of the key.
     * \param[in] key2
     *      The second 64 bits of the key.
     * \param[out] retPtr
     *      If not NULL, return the address of the referant
     *      removed here.
     * \param[out] retType
     *      If not NULL, return the type of the referant
     *      removed here.
     * \return
     *      Whether the hash table contained the key specified.
     */
    bool
    remove(uint64_t key1, uint64_t key2, T* retPtr = NULL,
        uint8_t *retType = NULL)
    {
        uint64_t secondaryHash;
        Entry *entry;
        CacheLine *bucket = findBucket(key1, key2, &secondaryHash);
        entry = lookupEntry(bucket, secondaryHash, key1, key2);
        if (entry == NULL)
            return false;
        T p = entry->getReferant(typeBits, retType);
        if (retPtr != NULL)
            *retPtr = p;
        entry->clear();
        return true;
    }

    /**
     * Update the referant correspoding to a key in the hash table.
     * This is equivalent to, but faster than, #remove() followed by #replace().
     * \param[in] ptr
     *      The address of the new referant.
     * \param[in] type
     *      Type identifier for the object to insert. Only valid if the
     *      HashTable was constructed with numTypes > 1.
     * \param[out] retPtr
     *      If not NULL, return the address of the referant
     *      replaced here. If nothing was replaced, the pointer
     *      returned is undefined. 
     * \param[out] retType
     *      If not NULL, return the type of the referant
     *      removed here. If nothing was replaced, the value is
     *      undefined.
     * \retval true
     *      The hash table previously contained (key1, key2) and its entry has
     *      been updated to reflect the new location of the referant.
     * \retval false
     *      The hash table did not previously contain (key1, key2). An entry has
     *      been created to reflect the location of the referant.
     */
    bool
    replace(T ptr, uint8_t type = 0, T* retPtr = NULL, uint8_t *retType = NULL)
    {
        CycleCounter<> cycles(&perfCounters.replaceCycles);
        uint64_t secondaryHash;
        CacheLine *bucket;
        Entry *entry;
        unsigned int i;

        ++perfCounters.replaceCalls;

        uint64_t key1 = ptr->key1();
        uint64_t key2 = ptr->key2();

        bucket = findBucket(key1, key2, &secondaryHash);
        entry = lookupEntry(bucket, secondaryHash, key1, key2);
        if (entry != NULL) {
            T p = entry->getReferant(typeBits, retType);
            if (retPtr != NULL)
                *retPtr = p;
            entry->setReferant(secondaryHash, ptr, type, typeBits);
            return true;
        }

        CacheLine *cl = bucket;
        while (1) {
            entry = cl->entries;
            for (i = 0; i < ENTRIES_PER_CACHE_LINE; i++) {
                if (entry->isAvailable(typeBits)) {
                    entry->setReferant(secondaryHash, ptr, type, typeBits);
                    return false;
                }
                entry++;
            }

            Entry &last = cl->entries[ENTRIES_PER_CACHE_LINE - 1];
            cl = last.getChainPointer(typeBits);
            if (cl == NULL) {
                // no empty space found, allocate a new cache line
                void *buf = xmemalign(sizeof(CacheLine), sizeof(CacheLine));
                cl = static_cast<CacheLine *>(buf);
                cl->entries[0] = last;
                for (i = 1; i < ENTRIES_PER_CACHE_LINE; i++)
                    cl->entries[i].clear();
                last.setChainPointer(cl);
            }
            ++perfCounters.insertChainsFollowed;
        }
    }

    /**
     * Apply the given callback function to each referant of type T stored
     * in the HashTable in the specified bucket.
     * \param callback
     *      The callback to fire on each referant stored in the HashTable.
     * \param cookie
     *      An opaque parameter to pass to the callback function.
     * \param bucket
     *      An index into the HashTable's buckets.  Must be < #numBuckets.
     * \return
     *      The total number of callbacks fired (i.e. the number of referants
     *      in the HashTable).
     */
    uint64_t
    forEachInBucket(void (*callback)(T, uint8_t, void *),
                    void *cookie,
                    uint64_t bucket)
    {
        uint64_t numCalls = 0;
        CacheLine *cl = &buckets.get()[bucket];
        while (1) {
            for (uint32_t j = 0; j < ENTRIES_PER_CACHE_LINE; j++) {
                Entry *e = &cl->entries[j];
                if (!e->isAvailable(typeBits) &&
                    e->getChainPointer(typeBits) == NULL) {
                    uint8_t type;
                    T ptr = e->getReferant(typeBits, &type);
                    callback(ptr, type, cookie);
                    numCalls++;
                }
            }

            Entry *entry = &cl->entries[ENTRIES_PER_CACHE_LINE - 1];
            cl = entry->getChainPointer(typeBits);
            if (cl == NULL)
                break;
        }
        return numCalls;
    }

    /**
     * Apply the given callback function to each referant of type T stored
     * in the HashTable.
     * \param[in] callback
     *      The callback to fire on each referant stored in the HashTable.
     * \param[in] cookie
     *      An opaque parameter to pass to the callback function.
     * \return
     *      The total number of callbacks fired (i.e. the number of referants
     *      in the HashTable).
     */
    uint64_t
    forEach(void (*callback)(T, uint8_t, void *), void *cookie)
    {
        uint64_t numCalls = 0;

        for (uint64_t i = 0; i < numBuckets; i++)
            numCalls += forEachInBucket(callback, cookie, i);

        return numCalls;
    }

    /**
     * Prefetch the cacheline associated with the given key.
     */
    void
    prefetchBucket(uint64_t key1, uint64_t key2)
    {
        uint64_t dummy;
        _mm_prefetch(reinterpret_cast<char *>(findBucket(key1, key2, &dummy)),
            _MM_HINT_T0);
    }

    /**
     * Prefetch the referant associated with the given key.
     */
    void
    prefetchReferant(uint64_t key1, uint64_t key2)
    {
        uint64_t secondaryHash;
        CacheLine *cl = findBucket(key1, key2, &secondaryHash);

        // Scan this cache line. If the secondaryHash matches, prefetch.
        // If not, don't bother following any chain pointer.
        Entry *candidate = cl->entries;
        for (uint32_t i = 0; i < ENTRIES_PER_CACHE_LINE; i++, candidate++) {
            if (candidate->hashMatches(secondaryHash, typeBits)) {
                _mm_prefetch(reinterpret_cast<const char *>(
                    candidate->getReferant(typeBits, NULL)), _MM_HINT_T0);
                return;
            }
        }
    }

    /**
     * Return the number of bytes per cache line.
     */
    static uint32_t
    bytesPerCacheLine()
    {
        return BYTES_PER_CACHE_LINE;
    }

    /**
     * Return the number of entries, i.e. (key1, key2) -> T, each cacheline
     * holds.
     */
    static uint32_t
    entriesPerCacheLine()
    {
        return ENTRIES_PER_CACHE_LINE;
    }

    /**
     * Return a read-only view of the hash table's performance counters.
     * \return
     *      See above.
     */
    const PerfCounters&
    getPerfCounters() const
    {
        return perfCounters;
    }

    /**
     * Reset the hash table's performance counters.
     */
    void
    resetPerfCounters()
    {
        perfCounters.reset();
    }

    /**
     * Returns the number of buckets allocated to the table.
     */
    uint64_t
    getNumBuckets() const
    {
        return numBuckets;
    }

  private:

    // forward declarations
    class Entry;
    struct CacheLine;

    /**
     * Find the nearest power of 2 that is less than or equal to \a n.
     * \param n
     *      A maximum for the return value.
     * \return
     *      A power of two that is less than or equal to \a n.
     */
    uint64_t
    nearestPowerOfTwo(uint64_t n)
    {
        if ((n & (n - 1)) == 0)
            return n;

        for (int i = 63; i >= 0; i--) {
            if ((1UL << i) <= n)
                return (1 << i);
        }
        return 0;
    }

    /**
     * A 64-bit to 64-bit hash function.
     * This is a helper to #findBucket().
     */
    static uint64_t
    hash(uint64_t key)
    {
        // This appears to be hash64shift by Thomas Wang from
        // http://www.concentric.net/~Ttwang/tech/inthash.htm
        key = (~key) + (key << 21); // key = (key << 21) - key - 1;
        key = key ^ (key >> 24);
        key = (key + (key << 3)) + (key << 8); // key * 265
        key = key ^ (key >> 14);
        key = (key + (key << 2)) + (key << 4); // key * 21
        key = key ^ (key >> 28);
        key = key + (key << 31);
        return key;
    }

    /**
     * Find the bucket corresponding to a particular key.
     * This also calculates the secondary hash bits used to disambiguate entries
     * in the same bucket.
     * \param[in] key1
     *      The first 64 bits of the key.
     * \param[in] key2
     *      The second 64 bits of the key.
     * \param[out] secondaryHash
     *      The secondary hash bits (16 bits).
     * \return
     *      The bucket corresponding to the given referant ID.
     */
    CacheLine *
    findBucket(uint64_t key1, uint64_t key2,
               uint64_t *secondaryHash) //const
    {
        uint64_t hashValue = hash(key1) ^ hash(key2);
        uint64_t bucketHash = hashValue & 0x0000ffffffffffffUL;
        *secondaryHash = hashValue >> 48;
        return &buckets.get()[bucketHash & (numBuckets - 1)];
        // This is equivalent to:
        //     &buckets.get()[bucketHash % numBuckets]
        // since numBuckets is a power of two, and this saves about 14 cycles on
        // an Intel Core 2 (see src/misc/modulus.cc).
    }

    /**
     * Find a hash table entry for a given key.
     * This is used in #lookup(), #remove(), and #replace() to find the hash
     * table entry to operate on.
     * \param[in] bucket
     *      The bucket corresponding to (key1, key2).
     * \param[in] secondaryHash
     *      Secondary hash bits for (key1, key2) as returned from #findBucket()
     *      (16 bits).
     * \param[in] key1
     *      The first 64 bits of the key.
     * \param[in] key2
     *      The second 64 bits of the key.
     * \return
     *      The pointer to the hash table entry, or \a NULL if there is no such
     *      hash table entry.
     */
    Entry *
    lookupEntry(CacheLine *bucket, uint64_t secondaryHash,
                uint64_t key1, uint64_t key2)
    {
        CycleCounter<> cycles(&perfCounters.lookupEntryCycles);
        unsigned int i;

        ++perfCounters.lookupEntryCalls;

        CacheLine *cl = bucket;

        while (1) {

            // Try this cache line.
            Entry *candidate = cl->entries;
            for (i = 0; i < ENTRIES_PER_CACHE_LINE; i++, candidate++) {

                if (candidate->hashMatches(secondaryHash, typeBits)) {
                    // The hash within the hash table entry matches, so with
                    // high probability this is the pointer we're looking for.
                    // To check, we must go to the object.
                    T c = candidate->getReferant(typeBits, NULL);
                    if (c->key1() == key1 && c->key2() == key2) {
                        perfCounters.lookupEntryDist.storeSample(cycles.stop());
                        return candidate;
                    } else {
                        ++perfCounters.lookupEntryHashCollisions;
                    }
                }
            }

            // Not found in this cache line, see if there's a chain to another
            // cache line.
            Entry *entry = &cl->entries[ENTRIES_PER_CACHE_LINE - 1];
            cl = entry->getChainPointer(typeBits);
            if (cl == NULL) {
                perfCounters.lookupEntryDist.storeSample(cycles.stop());
                return NULL;
            }
            ++perfCounters.lookupEntryChainsFollowed;
        }
    }

    /**
     * A hash table entry.
     *
     * Hash table entries live on \link CacheLine CacheLines\endlink.
     *
     * A normal hash table entry (see #setReferant(), #getReferant(), and
     * #hashMatches()) consists of secondary bits from the #hash() function on
     * the key to disambiguate most bucket collisions and the address of the
     * referant. In this case, its chain bit will not be set and its pointer
     * will not be \c NULL.
     *
     * A chaining hash table entry (see #setChainPointer(), #getChainPointer())
     * instead consists of a pointer to another cache line where additional
     * entries can be found. In this case, its chain bit will be set.
     *
     * A hash table entry can also be unused (see #clear() and #isAvailable()).
     * In this case, its pointer will be set to \c NULL.
     */
    class Entry {

      public:

        /**
         * Reinitialize a hash table entry as unused.
         */
        void
        clear()
        {
            // the raw bytes of this Entry must be zero for memset, etc to work
            value = 0;
        }

        /**
         * Reinitialize a regular hash table entry.
         * \param[in] hash
         *      The secondary hash bits computed from the key (16 bits).
         * \param[in] ptr
         *      The address of the referant. Must not be \c NULL.
         * \param[in] type
         *      Optional type of the referant.
         * \param[in] typeBits
         *      Number of bits consumed by the type parameter.
         */
        void
        setReferant(uint64_t hash, T ptr, uint8_t type, uint8_t typeBits)
        {
            assert(ptr != NULL);
            pack(hash, false, reinterpret_cast<uint64_t>(ptr), type, typeBits);
        }

        /**
         * Reinitialize a hash table entry as a chain link.
         * \param[in] ptr
         *      The pointer to the next cache line. Must not be \c NULL.
         */
        void
        setChainPointer(CacheLine *ptr)
        {
            assert(ptr != NULL);
            pack(0, true, reinterpret_cast<uint64_t>(ptr));
        }

        /**
         * Return whether a hash table entry is unused.
         * \param[in] typeBits
         *      Number of pointer bits used to differentiate types.
         * \return
         *      See above.
         */
        bool
        isAvailable(uint8_t typeBits) const
        {
            UnpackedEntry ue = unpack(typeBits);
            return (ue.ptr == 0);
        }

        /**
         * Extract the referant's address from a hash table entry.
         * The caller must first verify that the hash table entry indeed stores
         * a referant address with #hashMatches().
         * \param[in] typeBits
         *      Number of pointer bits used to differentiate types.
         * \param[out] type
         *      NULL, or a pointer to where the type should be returned.
         * \return
         *      The address of the referant stored.
         */
        T
        getReferant(uint8_t typeBits, uint8_t *type) const
        {
            UnpackedEntry ue = unpack(typeBits);
            assert(!ue.chain && ue.ptr != 0);
            if (type != NULL)
                *type = ue.type;
            return reinterpret_cast<T>(ue.ptr);
        }

        /**
         * Extract the chain pointer to another cache line.
         * \param[in] typeBits
         *      Number of pointer bits used to differentiate types.
         * \return
         *      The chain pointer to another cache line. If this entry does not 
         *      store a chain pointer, returns \c NULL instead.
         */
        CacheLine *
        getChainPointer(uint8_t typeBits) const
        {
            UnpackedEntry ue = unpack(typeBits);
            if (!ue.chain)
                return NULL;
            return reinterpret_cast<CacheLine*>(ue.ptr);
        }

        /**
         * Check whether the secondary hash bits stored match those given.
         * \param[in] hash
         *      The secondary hash bits computed from the key to test (16 bits).
         * \param[in] typeBits
         *      Number of pointer bits used to differentiate types.
         * \return
         *      Whether the hash table entry holds the address to a referant and
         *      the secondary hash bits for that referant point to \a hash.
         */
        bool
        hashMatches(uint64_t hash, uint8_t typeBits) const
        {
            UnpackedEntry ue = unpack(typeBits);
            return (!ue.chain && ue.ptr != 0 && ue.hash == hash);
        }

      private:
        /**
         * The packed value stored in the entry.
         *
         * The exact bits are, from MSB to LSB:
         * \li 16 bits for the secondary hash
         * \li 1 bit for whether the pointer is a chain
         * \li ``typeBits'' bits for the type of the referant
         * \li (47 - typeBits) bits for the pointer
         *
         * The main reason why it's not a struct with bit fields is that we'll
         * probably want to use atomic operations to set it eventually.
         *
         * Because the exact format is subject to change, you should always set
         * this using #pack() and access its contained fields using #unpack().
         */
        uint64_t value;

        /**
         * Replace this hash table entry.
         * \param[in] hash
         *      The secondary hash bits (16 bits) computed from the key.
         *      Irrelevant if \a chain is true.
         * \param[in] chain
         *      Whether \a ptr is a chain pointer as opposed to a referant
         *      pointer.
         * \param[in] ptr
         *      The chain pointer to the next cache line or the referant pointer
         *      (determined by \a chain).
         * \param[in] type
         *      Type identifier for the pointer being added. Only valid if the
         *      HashTable constructor was called with numTypes > 1. 
         * \param[in] typeBits
         *      Number of bits used by the type parameter, if it's valid.
         *      Otherwise, 0.
         * \throw Exception
         *      An exception is thrown if typeBits > MAX_TYPEBITS, or type is
         *      too large to fit in typeBits, or the pointer cannot fix in
         *      the number of bits we have.
         */
        void
        pack(uint64_t hash, bool chain, uint64_t ptr, uint8_t type = 0,
            uint8_t typeBits = 0)
        {
            if (ptr == 0)
                assert(hash == 0 && !chain);

            uint64_t typeField = 0;
            if (typeBits > 0) {
                typeField = type;
                if (typeBits > MAX_TYPEBITS)
                    throw Exception(HERE, "too many typeBits");
                if ((typeField & ~((1UL << typeBits) - 1)) != 0)
                    throw Exception(HERE, "type needs more than typeBits");
                typeField <<= (47 - typeBits);
            }

            if ((ptr  & ~(0x00007fffffffffffUL >> typeBits)) != 0)
                throw Exception(HERE, "pointer cannot fit! stack addr used?");

            uint64_t c = chain ? 1 : 0;
            assert((hash & ~(0x000000000000ffffUL)) == 0);
            this->value = ((hash << 48)  | (c << 47) | typeField | ptr);
        }

        /**
         * This is the return type of #unpack().
         * See the parameters of #pack() for an explanation.
         */
        struct UnpackedEntry {
            uint64_t hash;
            bool chain;
            uint8_t type;
            uint64_t ptr;
        };

        /**
         * Read the contents of this hash table entry.
         * \return
         *      The extracted values. See UnpackedEntry.
         */
        UnpackedEntry
        unpack(uint8_t typeBits) const
        {
            UnpackedEntry ue;
            ue.hash  = (this->value >> 48) &  0x000000000000ffffUL;
            ue.chain = (this->value >> 47) &  0x0000000000000001UL;
            ue.ptr   = this->value         & (0x00007fffffffffffUL >> typeBits);
            ue.type  = 0;
            if (typeBits != 0) {
                ue.type = downCast<uint8_t>((this->value >> (47 - typeBits)) &
                                            ((1UL << typeBits) - 1));
            }
            return ue;
        }

        friend class HashTableEntryTest;
    };
    static_assert(sizeof(Entry) == 8, "HashTable::Entry is not 8 bytes");

    /**
     * The number of bytes per cache line in this machine.
     */
    static const uint32_t BYTES_PER_CACHE_LINE = 64;

    /**
     * The number of hash table Entry objects in a CacheLine. This directly
     * corresponds to the number of referants each cacheline may contain.
     */
    static const uint32_t ENTRIES_PER_CACHE_LINE = (BYTES_PER_CACHE_LINE /
                                                    sizeof(Entry));
    static_assert(BYTES_PER_CACHE_LINE % sizeof(Entry) == 0,
                  "BYTES_PER_CACHE_LINE not a multiple of sizeof(Entry)");


    /**
     * A linked list of cache lines composes a bucket within the HashTable.
     *
     * Each cache line is composed of several hash table \link Entry
     * Entries\endlink, the last of which may be a link to another CacheLine.
     * See HashTable for more info.
     *
     * A CacheLine is meant to fit on a single L2 cache line on the CPU.
     * Different processors may require tweaking ENTRIES_PER_CACHE_LINE to
     * achieve this.
     */
    struct CacheLine {
        /**
         * See CacheLine.
         */
        Entry entries[ENTRIES_PER_CACHE_LINE];
    };
    static_assert(sizeof(CacheLine) == sizeof(Entry) * ENTRIES_PER_CACHE_LINE,
                  "HashTable entries don't fit evenly into a cacheline");

    /**
     * The number of buckets allocated to the table.
     */
    const uint64_t numBuckets;

    /**
     * The array of buckets.
     * See HashTable.
     */
    LargeBlockOfMemory<CacheLine> buckets;

    /**
     * The performance counters for the HashTable.
     * See #getPerfCounters().
     */
    PerfCounters perfCounters;

    /**
     * Number of types of referants we're tracking.
     */
    uint32_t numTypes;

    /**
     * Number of pointer bits reserved for the referant type. 
     */
    uint8_t typeBits;

    /**
     * Maximum value of typeBits.
     */
    static const uint8_t MAX_TYPEBITS = 8;

    /**
     * Maxmimum value of numTypes.
     */
    static const uint32_t MAX_NUMTYPES = (1 << MAX_TYPEBITS);

    friend class HashTableEntryTest;
    friend class HashTableTest;
    friend void hashTableBenchmark(uint64_t nkeys, uint64_t nlines);
    DISALLOW_COPY_AND_ASSIGN(HashTable);
};


} // namespace RAMCloud

#endif
