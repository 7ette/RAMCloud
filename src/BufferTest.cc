/* Copyright (c) 2010 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * \file
 * Unit tests for RAMCloud::Buffer.
 */

#include <string.h>
#include <strings.h>

#include <Buffer.h>
#include <TestUtil.h>

namespace RAMCloud {

class BufferAllocationTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BufferAllocationTest);

    CPPUNIT_TEST(test_constructor);
    CPPUNIT_TEST(test_destructor);

    CPPUNIT_TEST(test_canAllocateChunk);
    CPPUNIT_TEST(test_canAllocatePrepend);
    CPPUNIT_TEST(test_canAllocateAppend);

    CPPUNIT_TEST(test_allocateChunk);
    CPPUNIT_TEST(test_allocatePrepend);
    CPPUNIT_TEST(test_allocateAppend);

    CPPUNIT_TEST_SUITE_END();

  public:

    void test_constructor() {
        Buffer::Allocation a;
        CPPUNIT_ASSERT(a.next == NULL);
        CPPUNIT_ASSERT_EQUAL(Buffer::Allocation::APPEND_START, a.prependTop);
        CPPUNIT_ASSERT_EQUAL(Buffer::Allocation::APPEND_START, a.appendTop);
        CPPUNIT_ASSERT_EQUAL(Buffer::Allocation::TOTAL_SIZE, a.chunkTop);
    }

    void test_destructor() {
        Buffer::Allocation a;
        a.~Allocation();
        CPPUNIT_ASSERT(a.next == NULL);
        CPPUNIT_ASSERT_EQUAL(0, a.prependTop);
        CPPUNIT_ASSERT_EQUAL(Buffer::Allocation::TOTAL_SIZE, a.appendTop);
        CPPUNIT_ASSERT_EQUAL(Buffer::Allocation::APPEND_START, a.chunkTop);
    }

    void test_canAllocateChunk() {
        uint32_t size = (Buffer::Allocation::TOTAL_SIZE -
                         Buffer::Allocation::APPEND_START);
        CPPUNIT_ASSERT(Buffer::Allocation::canAllocateChunk(size));
        CPPUNIT_ASSERT(!Buffer::Allocation::canAllocateChunk(size) + 1);
    }

    void test_canAllocatePrepend() {
        uint32_t size = Buffer::Allocation::APPEND_START;
        CPPUNIT_ASSERT(Buffer::Allocation::canAllocatePrepend(size));
        CPPUNIT_ASSERT(!Buffer::Allocation::canAllocatePrepend(size) + 1);
    }

    void test_canAllocateAppend() {
        uint32_t size = (Buffer::Allocation::TOTAL_SIZE -
                         Buffer::Allocation::APPEND_START);
        CPPUNIT_ASSERT(Buffer::Allocation::canAllocateAppend(size));
        CPPUNIT_ASSERT(!Buffer::Allocation::canAllocateAppend(size) + 1);
    }

    void test_allocateChunk() {
        Buffer::Allocation a;
        uint32_t size = (Buffer::Allocation::TOTAL_SIZE -
                         Buffer::Allocation::APPEND_START);
        a.allocateChunk(0);
        CPPUNIT_ASSERT_EQUAL(&a.data[Buffer::Allocation::APPEND_START + 10],
                       a.allocateChunk(size - 10));
        CPPUNIT_ASSERT_EQUAL(&a.data[Buffer::Allocation::APPEND_START],
                       a.allocateChunk(10));
        CPPUNIT_ASSERT_EQUAL(NULL, a.allocateChunk(1));
        CPPUNIT_ASSERT_EQUAL(NULL, a.allocateAppend(1));
    }

    void test_allocatePrepend() {
        Buffer::Allocation a;
        uint32_t size = Buffer::Allocation::APPEND_START;
        a.allocatePrepend(0);
        CPPUNIT_ASSERT_EQUAL(&a.data[10], a.allocatePrepend(size - 10));
        CPPUNIT_ASSERT_EQUAL(&a.data[0], a.allocatePrepend(10));
        CPPUNIT_ASSERT_EQUAL(NULL, a.allocatePrepend(1));
    }

    void test_allocateAppend() {
        Buffer::Allocation a;
        uint32_t size = (Buffer::Allocation::TOTAL_SIZE -
                         Buffer::Allocation::APPEND_START);
        a.allocateAppend(0);
        CPPUNIT_ASSERT_EQUAL(&a.data[Buffer::Allocation::APPEND_START],
                       a.allocateAppend(size - 10));
        CPPUNIT_ASSERT_EQUAL(&a.data[Buffer::Allocation::TOTAL_SIZE - 10],
                       a.allocateAppend(10));
        CPPUNIT_ASSERT_EQUAL(NULL, a.allocateAppend(1));
        CPPUNIT_ASSERT_EQUAL(NULL, a.allocateChunk(1));
    }

};
CPPUNIT_TEST_SUITE_REGISTRATION(BufferAllocationTest);

/**
 * Helper for BufferChunkTest's test_NewChunk().
 */
class DestructorCounter {
  public:
    explicit DestructorCounter(uint32_t* counter) : destructed(counter) {
        *destructed = 0;
    }
    ~DestructorCounter() {
        ++(*destructed);
    }
  private:
    uint32_t* destructed;
    DISALLOW_COPY_AND_ASSIGN(DestructorCounter);
};

class BufferChunkTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BufferChunkTest);

    CPPUNIT_TEST(test_Chunk);
    CPPUNIT_TEST(test_HeapChunk);
    CPPUNIT_TEST(test_NewChunk);

    CPPUNIT_TEST_SUITE_END();

  public:

    void test_Chunk() {
        char data;
        Buffer::Chunk c(&data, sizeof(data));
        CPPUNIT_ASSERT_EQUAL(&data, c.data);
        CPPUNIT_ASSERT_EQUAL(sizeof(data), c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        c.~Chunk();
        CPPUNIT_ASSERT_EQUAL(NULL, c.data);
        CPPUNIT_ASSERT_EQUAL(0, c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        c.~Chunk();
    }

    void test_HeapChunk() {
        // TODO(ongaro): A counter on the number of times free is called would
        // be helpful.
        void* data = xmalloc(100);
        Buffer::HeapChunk c(data, 100);
        CPPUNIT_ASSERT_EQUAL(data, c.data);
        CPPUNIT_ASSERT_EQUAL(100, c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        ((Buffer::Chunk&) c).~Chunk();
        CPPUNIT_ASSERT_EQUAL(NULL, c.data);
        CPPUNIT_ASSERT_EQUAL(0, c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        ((Buffer::Chunk&) c).~Chunk();
    }

    void test_NewChunk() {
        static uint32_t destructed = 0;
        DestructorCounter* data = new DestructorCounter(&destructed);
        Buffer::NewChunk<DestructorCounter> c(data);
        CPPUNIT_ASSERT_EQUAL(data, c.data);
        CPPUNIT_ASSERT_EQUAL(sizeof(*data), c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        ((Buffer::Chunk&) c).~Chunk();
        CPPUNIT_ASSERT_EQUAL(NULL, c.data);
        CPPUNIT_ASSERT_EQUAL(0, c.length);
        CPPUNIT_ASSERT_EQUAL(NULL, c.next);
        ((Buffer::Chunk&) c).~Chunk();
        CPPUNIT_ASSERT_EQUAL(1U, destructed);
    }

};
CPPUNIT_TEST_SUITE_REGISTRATION(BufferChunkTest);

class BufferTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BufferTest);

    CPPUNIT_TEST(test_constructor);
    CPPUNIT_TEST(test_constructor_withParams);
    CPPUNIT_TEST(test_destructor);

    CPPUNIT_TEST(test_newAllocation);
    CPPUNIT_TEST(test_allocateChunk);
    CPPUNIT_TEST(test_allocatePrepend);
    CPPUNIT_TEST(test_allocateAppend);

    CPPUNIT_TEST(test_prepend);
    CPPUNIT_TEST(test_append);

    CPPUNIT_TEST(test_peek_normal);
    CPPUNIT_TEST(test_peek_offsetGreaterThanTotalLength);

    CPPUNIT_TEST(test_internal_copy);
    CPPUNIT_TEST(test_allocateScratchRange);

    CPPUNIT_TEST(test_getRange_inputEdgeCases);
    CPPUNIT_TEST(test_getRange_peek);
    CPPUNIT_TEST(test_getRange_copy);

    CPPUNIT_TEST(test_copy_noop);
    CPPUNIT_TEST(test_copy_normal);

    CPPUNIT_TEST(test_toString);

    CPPUNIT_TEST_SUITE_END();

    // I've inserted padding in between these arrays so that we don't get lucky
    // by going past end of testStr1 and hitting testStr2, etc.
    char testStr[30];
    char pad1[50];
    char testStr1[10];
    char pad2[50];
    char testStr2[10];
    char pad3[50];
    char testStr3[10];
    char pad4[50];
    char cmpBuf[30];    // To use for strcmp at the end of a test.
    Buffer *buf;

  public:
    BufferTest() : buf(NULL) {
        memcpy(testStr, "ABCDEFGHIJabcdefghijklmnopqrs\0", 30);
        memcpy(testStr1, "ABCDEFGHIJ", 10);
        memcpy(testStr2, "abcdefghij", 10);
        memcpy(testStr3, "klmnopqrs\0", 10);
        memset(pad1, 0xcc, sizeof(pad1));
        memset(pad2, 0xdd, sizeof(pad1));
        memset(pad3, 0xee, sizeof(pad1));
        memset(pad4, 0xff, sizeof(pad1));
    }

    void setUp() {
        // This uses prepend, so the tests for prepend
        // probably shouldn't use this.
        buf = new Buffer();
        buf->prepend(testStr3, 10);
        buf->prepend(testStr2, 10);
        buf->prepend(testStr1, 10);
    }

    void tearDown() { delete buf; }

    void test_constructor() {
        // Basic sanity checks for the constructor.
        Buffer b;
        CPPUNIT_ASSERT_EQUAL(0, b.totalLength);
        CPPUNIT_ASSERT_EQUAL(0, b.numberChunks);
        CPPUNIT_ASSERT_EQUAL(NULL, b.chunks);
        CPPUNIT_ASSERT_EQUAL(NULL, b.allocations);
        CPPUNIT_ASSERT_EQUAL(NULL, b.scratchRanges);
    }

    void test_constructor_withParams() {
        Buffer b(testStr1, 10);
        CPPUNIT_ASSERT_EQUAL("ABCDEFGHIJ", b.toString());
    }

    void test_destructor() {
        Buffer b(testStr1, 10);
        b.prepend(testStr1, 5);
        b.getRange(0, 15);
        b.~Buffer();
        CPPUNIT_ASSERT_EQUAL(0, b.totalLength);
        CPPUNIT_ASSERT_EQUAL(0, b.numberChunks);
        CPPUNIT_ASSERT_EQUAL(NULL, b.chunks);
        CPPUNIT_ASSERT_EQUAL(NULL, b.allocations);
        CPPUNIT_ASSERT_EQUAL(NULL, b.scratchRanges);
    }

    void test_newAllocation() {
        Buffer b;
        Buffer::Allocation* a2 = b.newAllocation();
        Buffer::Allocation* a1 = b.newAllocation();
        CPPUNIT_ASSERT_EQUAL(a1, b.allocations);
        CPPUNIT_ASSERT_EQUAL(a2, a1->next);
        CPPUNIT_ASSERT_EQUAL(NULL, a2->next);
    }

    bool allocationContains(Buffer::Allocation* allocation, void* p) {
        return (p >= &allocation->data[0] &&
                p < &allocation->data[Buffer::Allocation::TOTAL_SIZE]);
    }

    void test_allocateChunk() {
        uint32_t chunkTopStart;

        // allocations is not NULL and the chunk fits in the existing
        // allocation
        {
            Buffer b;
            chunkTopStart = b.newAllocation()->chunkTop;
            void* data = b.allocateChunk(1);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(chunkTopStart != b.allocations->chunkTop);
        }

        // allocations is NULL, but the chunk fits in a new allocation
        {
            Buffer b;
            void* data = b.allocateChunk(1);
            CPPUNIT_ASSERT(b.allocations != NULL);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(chunkTopStart != b.allocations->chunkTop);
        }

        // allocations is not NULL, the chunk doesn't fit in the current
        // allocation, and the chunk wouldn't fit in any allocation.
        {
            Buffer b;
            b.newAllocation();
            void* data = b.allocateChunk(Buffer::Allocation::TOTAL_SIZE + 10);
            CPPUNIT_ASSERT(b.scratchRanges != NULL);
            CPPUNIT_ASSERT(b.scratchRanges->data == data);
            CPPUNIT_ASSERT(chunkTopStart == b.allocations->chunkTop);
        }
    }

    void test_allocatePrepend() {
        uint32_t prependTopStart;

        // allocations is not NULL and the prepend fits in the existing
        // allocation
        {
            Buffer b;
            prependTopStart = b.newAllocation()->prependTop;
            void* data = b.allocatePrepend(1);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(prependTopStart != b.allocations->prependTop);
        }

        // allocations is NULL, but the prepend fits in a new allocation
        {
            Buffer b;
            void* data = b.allocatePrepend(1);
            CPPUNIT_ASSERT(b.allocations != NULL);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(prependTopStart != b.allocations->prependTop);
        }

        // allocations is not NULL, the prepend doesn't fit in the current
        // allocation, and the prepend wouldn't fit in any allocation.
        {
            Buffer b;
            b.newAllocation();
            void* data = b.allocatePrepend(Buffer::Allocation::TOTAL_SIZE + 10);
            CPPUNIT_ASSERT(b.scratchRanges != NULL);
            CPPUNIT_ASSERT(b.scratchRanges->data == data);
            CPPUNIT_ASSERT(prependTopStart == b.allocations->prependTop);
        }
    }

    void test_allocateAppend() {
        uint32_t appendTopStart;

        // allocations is not NULL and the append fits in the existing
        // allocation
        {
            Buffer b;
            appendTopStart = b.newAllocation()->appendTop;
            void* data = b.allocateAppend(1);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(appendTopStart != b.allocations->appendTop);
        }

        // allocations is NULL, but the append fits in a new allocation
        {
            Buffer b;
            void* data = b.allocateAppend(1);
            CPPUNIT_ASSERT(b.allocations != NULL);
            CPPUNIT_ASSERT(allocationContains(b.allocations, data));
            CPPUNIT_ASSERT(appendTopStart != b.allocations->appendTop);
        }

        // allocations is not NULL, the append doesn't fit in the current
        // allocation, and the append wouldn't fit in any allocation.
        {
            Buffer b;
            b.newAllocation();
            void* data = b.allocateAppend(Buffer::Allocation::TOTAL_SIZE + 10);
            CPPUNIT_ASSERT(b.scratchRanges != NULL);
            CPPUNIT_ASSERT(b.scratchRanges->data == data);
            CPPUNIT_ASSERT(appendTopStart == b.allocations->appendTop);
        }
    }

    void test_prepend() {
        Buffer b;
        b.prepend(NULL, 0);
        b.prepend(testStr3, 10);
        b.prepend(testStr2, 10);
        b.prepend(testStr1, 10);
        CPPUNIT_ASSERT_EQUAL("ABCDEFGHIJ | abcdefghij | klmnopqrs/0",
                b.toString());
    }

    void test_append() {
        Buffer b;
        b.append(NULL, 0);
        b.append(testStr1, 10);
        b.append(testStr2, 10);
        b.append(testStr3, 10);
        CPPUNIT_ASSERT_EQUAL("ABCDEFGHIJ | abcdefghij | klmnopqrs/0",
                b.toString());
    }

    void test_peek_normal() {
        void *ret_val;
        CPPUNIT_ASSERT_EQUAL(10, buf->peek(0, &ret_val));
        CPPUNIT_ASSERT_EQUAL(testStr1, ret_val);
        CPPUNIT_ASSERT_EQUAL(1, buf->peek(9, &ret_val));
        CPPUNIT_ASSERT_EQUAL(testStr1 + 9, ret_val);
        CPPUNIT_ASSERT_EQUAL(10, buf->peek(10, &ret_val));
        CPPUNIT_ASSERT_EQUAL(testStr2, ret_val);
        CPPUNIT_ASSERT_EQUAL(5, buf->peek(25, &ret_val));
        CPPUNIT_ASSERT_EQUAL(testStr3 + 5, ret_val);
    }

    void test_peek_offsetGreaterThanTotalLength() {
        void *ret_val;
        CPPUNIT_ASSERT_EQUAL(0, buf->peek(30, &ret_val));
        CPPUNIT_ASSERT_EQUAL(NULL, ret_val);
        CPPUNIT_ASSERT_EQUAL(0, buf->peek(31, &ret_val));
        CPPUNIT_ASSERT_EQUAL(NULL, ret_val);
    }

    void test_internal_copy() {
        Buffer b;
        char scratch[50];

        // skip while loop
        strncpy(scratch, "0123456789", 11);
        buf->copy(buf->chunks, 0, 0, scratch + 1);
        CPPUNIT_ASSERT_EQUAL("0123456789", scratch);

        // nonzero offset in first chunk, partial chunk
        strncpy(scratch, "01234567890123456789", 21);
        buf->copy(buf->chunks, 5, 3, scratch + 1);
        CPPUNIT_ASSERT_EQUAL("0FGH4567890123456789", scratch);

        // spans chunks, ends at exactly the end of the buffer
        strncpy(scratch, "0123456789012345678901234567890123456789", 41);
        buf->copy(buf->chunks, 0, 30, scratch + 1);
        // The data contains a null character, so check it in two
        // pieces (one up through the null, one after).
        CPPUNIT_ASSERT_EQUAL("0ABCDEFGHIJabcdefghijklmnopqrs", scratch);
        CPPUNIT_ASSERT_EQUAL("123456789", scratch+31);
    }

    void test_allocateScratchRange() {
        typedef Buffer::ScratchRange ScratchRange;
        Buffer b;

        void* r2 = b.allocateScratchRange(3);
        ScratchRange* cr2 = static_cast<ScratchRange*>(r2) - 1;
        CPPUNIT_ASSERT_EQUAL(b.scratchRanges, cr2);

        void* r1 = b.allocateScratchRange(4);
        ScratchRange* cr1 = static_cast<ScratchRange*>(r1) - 1;
        CPPUNIT_ASSERT_EQUAL(b.scratchRanges, cr1);

        CPPUNIT_ASSERT_EQUAL(cr2, cr1->next);
        CPPUNIT_ASSERT_EQUAL(NULL, cr2->next);
    }

    void test_getRange_inputEdgeCases() {
        CPPUNIT_ASSERT_EQUAL(NULL, buf->getRange(0, 0));
        CPPUNIT_ASSERT_EQUAL(NULL, buf->getRange(30, 1));
        CPPUNIT_ASSERT_EQUAL(NULL, buf->getRange(29, 2));
    }

    void test_getRange_peek() {
        CPPUNIT_ASSERT_EQUAL(testStr1, buf->getRange(0, 10));
        CPPUNIT_ASSERT_EQUAL(testStr1 + 3, buf->getRange(3, 2));
        CPPUNIT_ASSERT_EQUAL(testStr2, buf->getRange(10, 10));
        CPPUNIT_ASSERT_EQUAL(testStr2 + 1,  buf->getRange(11, 5));
        CPPUNIT_ASSERT_EQUAL(testStr3, buf->getRange(20, 1));
        CPPUNIT_ASSERT_EQUAL(testStr3 + 9, buf->getRange(29, 1));
        CPPUNIT_ASSERT_EQUAL(NULL, buf->scratchRanges);
    }

    void test_getRange_copy() {
        char out[10];
        strncpy(out, static_cast<char*>(buf->getRange(9, 2)), 2);
        out[2] = 0;
        CPPUNIT_ASSERT_EQUAL("Ja", out);
        CPPUNIT_ASSERT(NULL != buf->scratchRanges);
    }

    void test_copy_noop() {
        Buffer b;
        CPPUNIT_ASSERT_EQUAL(0, b.copy(0, 0, cmpBuf));
        CPPUNIT_ASSERT_EQUAL(0, b.copy(1, 0, cmpBuf));
        CPPUNIT_ASSERT_EQUAL(0, b.copy(1, 1, cmpBuf));
        CPPUNIT_ASSERT_EQUAL(0, buf->copy(30, 0, cmpBuf));
        CPPUNIT_ASSERT_EQUAL(0, buf->copy(30, 1, cmpBuf));
        CPPUNIT_ASSERT_EQUAL(0, buf->copy(31, 1, cmpBuf));
    }

    void test_copy_normal() {
        char scratch[50];

        // truncate transfer length
        CPPUNIT_ASSERT_EQUAL(5, buf->copy(25, 6, scratch + 1));

        // skip while loop (start in first chunk)
        strncpy(scratch, "01234567890123456789", 21);
        CPPUNIT_ASSERT_EQUAL(5, buf->copy(0, 5, scratch + 1));
        CPPUNIT_ASSERT_EQUAL("0ABCDE67890123456789", scratch);

        // starting point not in first chunk
        strncpy(scratch, "012345678901234567890123456789", 31);
        CPPUNIT_ASSERT_EQUAL(6, buf->copy(20, 6, scratch + 1));
        CPPUNIT_ASSERT_EQUAL("0klmnop78901234567890123456789", scratch);
    }

    void test_toString() {
        Buffer b;
        b.append(const_cast<char *>("abc\n\x1f \x7e\x7f\xf4zzz"), 9);
        b.append(const_cast<char *>("012\0z\x05z78901234567890"
                                    "1234567890abcdefg"),
                 37);
        b.append(const_cast<char *>("xyz"), 3);
        CPPUNIT_ASSERT_EQUAL("abc/n/x1f ~/x7f/xf4 | "
                             "012/0z/x05z7890123456789(+17 chars) | xyz",
                             b.toString());
    }

    DISALLOW_COPY_AND_ASSIGN(BufferTest);
};
CPPUNIT_TEST_SUITE_REGISTRATION(BufferTest);

class BufferIteratorTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BufferIteratorTest);
    CPPUNIT_TEST(test_normal);
    CPPUNIT_TEST(test_isDone);
    CPPUNIT_TEST(test_next);
    CPPUNIT_TEST(test_getData);
    CPPUNIT_TEST(test_getLength);
    CPPUNIT_TEST_SUITE_END();
    char x[30];

  public:
    void test_normal() {
        Buffer b;
        b.append(&x[0], 10);
        b.append(&x[10], 20);

        Buffer::Iterator iter(b);
        CPPUNIT_ASSERT(!iter.isDone());
        CPPUNIT_ASSERT_EQUAL(&x[0], iter.getData());
        CPPUNIT_ASSERT_EQUAL(10, iter.getLength());
        iter.next();
        CPPUNIT_ASSERT(!iter.isDone());
        CPPUNIT_ASSERT_EQUAL(&x[10], iter.getData());
        CPPUNIT_ASSERT_EQUAL(20U, iter.getLength());
        iter.next();
        CPPUNIT_ASSERT(iter.isDone());
    }

    void test_isDone() {
        Buffer b;

        { // empty Buffer
            Buffer::Iterator iter(b);
            CPPUNIT_ASSERT(iter.isDone());
        }

        b.append(&x[0], 10);
        b.append(&x[10], 20);

        { // nonempty buffer
            Buffer::Iterator iter(b);
            CPPUNIT_ASSERT(!iter.isDone());
            iter.next();
            CPPUNIT_ASSERT(!iter.isDone());
            iter.next();
            CPPUNIT_ASSERT(iter.isDone());
        }
    }

    void test_next() {
        Buffer b;
        b.append(&x[0], 10);
        Buffer::Iterator iter(b);
        CPPUNIT_ASSERT_EQUAL(iter.current, b.chunks);
        iter.next();
        CPPUNIT_ASSERT_EQUAL(iter.current, b.chunks->next);
    }

    void test_getData() {
        Buffer b;
        b.append(&x[0], 10);
        Buffer::Iterator iter(b);
        CPPUNIT_ASSERT_EQUAL(iter.getData(), &x[0]);
    }

    void test_getLength() {
        Buffer b;
        b.append(&x[0], 10);
        Buffer::Iterator iter(b);
        CPPUNIT_ASSERT_EQUAL(iter.getLength(), 10);
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(BufferIteratorTest);

}  // namespace RAMCloud
