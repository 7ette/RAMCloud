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

#include "CoordinatorClient.h"
#include "ProtoBuf.h"

namespace RAMCloud {

/**
 * Create a new table.
 *
 * \param name
 *      Name for the new table (NULL-terminated string).
 *
 * \exception InternalError
 */
void
CoordinatorClient::createTable(const char* name)
{
    Buffer req;
    uint32_t length = strlen(name) + 1;
    CreateTableRpc::Request& reqHdr(allocHeader<CreateTableRpc>(req));
    reqHdr.nameLength = length;
    memcpy(new(&req, APPEND) char[length], name, length);
    while (true) {
        Buffer resp;
        sendRecv<CreateTableRpc>(session, req, resp);
        try {
            checkStatus(HERE);
            return;
        } catch (const RetryException& e) {
            LOG(DEBUG, "RETRY trying to create table");
            yield();
        }
    }
}

/**
 * Delete a table.
 *
 * All objects in the table are implicitly deleted, along with any
 * other information associated with the table (such as, someday,
 * indexes).  If the table does not currently exist than the operation
 * returns successfully without actually doing anything.
 *
 * \param name
 *      Name of the table to delete (NULL-terminated string).
 *  
 * \exception InternalError
 */
void
CoordinatorClient::dropTable(const char* name)
{
    Buffer req, resp;
    uint32_t length = strlen(name) + 1;
    DropTableRpc::Request& reqHdr(allocHeader<DropTableRpc>(req));
    reqHdr.nameLength = length;
    memcpy(new(&req, APPEND) char[length], name, length);
    sendRecv<DropTableRpc>(session, req, resp);
    checkStatus(HERE);
}

/**
 * Look up a table by name and return a small integer handle that
 * can be used to access the table.
 *
 * \param name
 *      Name of the desired table (NULL-terminated string).
 *      
 * \return
 *      The return value is an identifier for the table; this is used
 *      instead of the table's name for most RAMCloud operations
 *      involving the table.
 *
 * \exception TableDoesntExistException
 * \exception InternalError
 */
uint32_t
CoordinatorClient::openTable(const char* name)
{
    Buffer req, resp;
    uint32_t length = strlen(name) + 1;
    OpenTableRpc::Request& reqHdr(allocHeader<OpenTableRpc>(req));
    reqHdr.nameLength = length;
    memcpy(new(&req, APPEND) char[length], name, length);
    const OpenTableRpc::Response& respHdr(
        sendRecv<OpenTableRpc>(session, req, resp));
    checkStatus(HERE);
    return respHdr.tableId;
}

/**
 * Servers call this when they come online to beg for work.
 * \param serverType
 *      Master, etc.
 * \param localServiceLocator
 *      The service locator describing how other hosts can contact the server.
 * \return
 *      A server ID guaranteed never to have been used before.
 */
uint64_t
CoordinatorClient::enlistServer(ServerType serverType,
                                string localServiceLocator)
{
    while (true) {
        try {
            Buffer req;
            Buffer resp;
            EnlistServerRpc::Request& reqHdr(
                allocHeader<EnlistServerRpc>(req));
            reqHdr.serverType = serverType;
            reqHdr.serviceLocatorLength = localServiceLocator.length() + 1;
            strncpy(new(&req, APPEND) char[reqHdr.serviceLocatorLength],
                    localServiceLocator.c_str(),
                    reqHdr.serviceLocatorLength);
            const EnlistServerRpc::Response& respHdr(
                sendRecv<EnlistServerRpc>(session, req, resp));
            checkStatus(HERE);
            return respHdr.serverId;
        } catch (TransportException& e) {
            LOG(NOTICE,
                "TransportException trying to talk to coordinator: %s",
                e.str().c_str());
            LOG(NOTICE, "retrying");
            yield();
        }
    }
}

/**
 * List all live servers of the given type.
 * \param[in] type
 *      The type of server to get a list of. Presently either MASTER or BACKUP.
 * \param[out] serverList
 *      An empty ServerList that will be filled with current master servers.
 */
void
CoordinatorClient::getServerList(ServerType type,
                                 ProtoBuf::ServerList& serverList)
{
    Buffer req;
    Buffer resp;
    GetServerListRpc::Request& reqHdr(
        allocHeader<GetServerListRpc>(req));
    reqHdr.serverType = type;
    const GetServerListRpc::Response& respHdr(
        sendRecv<GetServerListRpc>(session, req, resp));
    checkStatus(HERE);
    ProtoBuf::parseFromResponse(resp, sizeof(respHdr),
                                respHdr.serverListLength, serverList);
}

/**
 * List all live servers.
 * Used in ensureHosts.
 * \param[out] serverList
 *      An empty ServerList that will be filled with current servers.
 */
void
CoordinatorClient::getServerList(ProtoBuf::ServerList& serverList)
{
    getServerList(MASTER, serverList);
    ProtoBuf::ServerList mergeList;
    getServerList(BACKUP, mergeList);
    serverList.MergeFrom(mergeList);
}

/**
 * List all live master servers.
 * The failure detector uses this to periodically probe for failed masters.
 * \param[out] serverList
 *      An empty ServerList that will be filled with current master servers.
 */
void
CoordinatorClient::getMasterList(ProtoBuf::ServerList& serverList)
{
    getServerList(MASTER, serverList);
}

/**
 * List all live backup servers.
 * Masters call and cache this periodically to find backups. The failure
 * detector also uses this to periodically probe for failed backups.
 * \param[out] serverList
 *      An empty ServerList that will be filled with current backup servers.
 */
void
CoordinatorClient::getBackupList(ProtoBuf::ServerList& serverList)
{
    getServerList(BACKUP, serverList);
}

/**
 * Return the entire tablet map.
 * Clients use this to find objects.
 * If the returned data becomes too big, we should add parameters to
 * specify a subrange.
 * \param[out] tabletMap
 *      An empty Tablets that will be filled with current tablets.
 *      Each tablet has a service locator string describing where to find
 *      its master.
 */
void
CoordinatorClient::getTabletMap(ProtoBuf::Tablets& tabletMap)
{
    Buffer req;
    Buffer resp;
    allocHeader<GetTabletMapRpc>(req);
    const GetTabletMapRpc::Response& respHdr(
        sendRecv<GetTabletMapRpc>(session, req, resp));
    checkStatus(HERE);
    ProtoBuf::parseFromResponse(resp, sizeof(respHdr),
                                respHdr.tabletMapLength, tabletMap);
}

/**
 * Report a slow or dead server.
 */
void
CoordinatorClient::hintServerDown(string serviceLocator)
{
    Buffer req;
    Buffer resp;
    HintServerDownRpc::Request& reqHdr(
        allocHeader<HintServerDownRpc>(req));
    reqHdr.serviceLocatorLength = serviceLocator.length() + 1;
    strncpy(new(&req, APPEND) char[reqHdr.serviceLocatorLength],
            serviceLocator.c_str(),
            reqHdr.serviceLocatorLength);
    sendRecv<HintServerDownRpc>(session, req, resp);
    checkStatus(HERE);
}

/**
 * \copydoc MasterClient::ping
 */
void
CoordinatorClient::ping()
{
    Buffer req;
    Buffer resp;
    allocHeader<PingRpc>(req);
    sendRecv<PingRpc>(session, req, resp);
    checkStatus(HERE);
}

/**
 * Have all backups flush their dirty segments to storage.
 * This is useful for measuring recovery performance accurately.
 */
void
CoordinatorClient::quiesce()
{
    Buffer req;
    Buffer resp;
    allocHeader<BackupQuiesceRpc>(req);
    sendRecv<BackupQuiesceRpc>(session, req, resp);
    checkStatus(HERE);
}

/**
 * Tell the coordinator that recovery of a particular tablets have
 * been recovered on the master who is calling.
 *
 * \param[in] masterId
 *      The masterId of the server invoking this method.
 * \param[in] tablets
 *      The tablets which form a partition of a will which are
 *      now done recovering.
 * \param[in] will
 *      The serialized ProtoBuf representation of the post-recovery
 *      Will to send to the Coordinator.
 */
void
CoordinatorClient::tabletsRecovered(uint64_t masterId,
    const ProtoBuf::Tablets& tablets, const ProtoBuf::Tablets& will)
{
    Buffer req, resp;
    TabletsRecoveredRpc::Request& reqHdr(allocHeader<TabletsRecoveredRpc>(req));
    reqHdr.masterId = masterId;
    reqHdr.tabletsLength = serializeToRequest(req, tablets);
    reqHdr.willLength = serializeToRequest(req, will);
    sendRecv<TabletsRecoveredRpc>(session, req, resp);
    checkStatus(HERE);
}

/**
 * Update a masterId's Will with the Coordinator.
 *
 * \param[in] masterId
 *      The masterId whose Will we're updating.
 * \param[in] will 
 *      The ProtoBuf serialised representation of the Will.
 */
void
CoordinatorClient::setWill(uint64_t masterId, const ProtoBuf::Tablets& will)
{
    Buffer req, resp;
    SetWillRpc::Request& reqHdr(allocHeader<SetWillRpc>(req));
    reqHdr.masterId = masterId;
    reqHdr.willLength = serializeToRequest(req, will);
    sendRecv<SetWillRpc>(session, req, resp);
    checkStatus(HERE);
}

} // namespace RAMCloud
