/* Copyright (c) 2010 Stanford University
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

#ifndef RAMCLOUD_RAMCLOUD_H
#define RAMCLOUD_RAMCLOUD_H

#include "Common.h"
#include "CoordinatorClient.h"
#include "MasterClient.h"
#include "ObjectFinder.h"

namespace RAMCloud {

/**
 * The RamCloud class provides the primary interface used by applications to
 * access a RAMCloud cluster.
 *
 * Each RamCloud object provides access to a particular RAMCloud cluster;
 * all of the RAMCloud RPC requests appear as methods on this object.
 */
class RamCloud {
  public:
    /// An asynchronous version of #create().
    class Create {
      public:
        /// Start a create RPC. See RamCloud::create.
        Create(RamCloud& ramCloud,
               uint32_t tableId, const void* buf, uint32_t length,
               uint64_t* version = NULL, bool async = false)
            : master(ramCloud.objectFinder.lookupHead(tableId))
            , masterCreate(master, tableId, buf, length, version, async)
        {
        }
        bool isReady() { return masterCreate.isReady(); }
        /// Wait for the create RPC to complete.
        uint64_t operator()() { return masterCreate(); }
      private:
        MasterClient master;
        MasterClient::Create masterCreate;
        DISALLOW_COPY_AND_ASSIGN(Create);
    };

    /// An asynchronous version of #write().
    class Write {
      public:
        /// Start a write RPC. See RamCloud::write.
        Write(RamCloud& ramCloud,
              uint32_t tableId, uint64_t id, const void* buf,
              uint32_t length, const RejectRules* rejectRules = NULL,
              uint64_t* version = NULL, bool async = false)
            : master(ramCloud.objectFinder.lookup(tableId, id))
            , masterWrite(master, tableId, id, buf, length,
                          rejectRules, version, async)
        {
        }
        bool isReady() { return masterWrite.isReady(); }
        /// Wait for the write RPC to complete.
        void operator()() { masterWrite(); }
      private:
        MasterClient master;
        MasterClient::Write masterWrite;
        DISALLOW_COPY_AND_ASSIGN(Write);
    };

    explicit RamCloud(const char* serviceLocator);
    void createTable(const char* name);
    void dropTable(const char* name);
    uint32_t openTable(const char* name);
    uint64_t create(uint32_t tableId, const void* buf, uint32_t length,
                    uint64_t* version = NULL, bool async = false);
    void ping();
    void read(uint32_t tableId, uint64_t id, Buffer* value,
              const RejectRules* rejectRules = NULL,
              uint64_t* version = NULL);
    void remove(uint32_t tableId, uint64_t id,
                const RejectRules* rejectRules = NULL,
                uint64_t* version = NULL);
    void write(uint32_t tableId, uint64_t id, const void* buf,
               uint32_t length, const RejectRules* rejectRules = NULL,
               uint64_t* version = NULL, bool async = false);

    /// \copydoc Client::status
    Status status;

  public: // public for now to make administrative calls from clients
    CoordinatorClient coordinator;
    ObjectFinder objectFinder;

    DISALLOW_COPY_AND_ASSIGN(RamCloud);
};
} // namespace RAMCloud

#endif // RAMCLOUD_RAMCLOUD_H
