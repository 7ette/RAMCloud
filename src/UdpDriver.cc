/* Copyright (c) 2010-2011 Stanford University
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

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "Common.h"
#include "UdpDriver.h"
#include "ServiceLocator.h"

namespace RAMCloud {

/**
 * Default object used to make system calls.
 */
static Syscall defaultSyscall;

/**
 * Used by this class to make all system calls.  In normal production
 * use it points to defaultSyscall; for testing it points to a mock
 * object.
 */
Syscall* UdpDriver::sys = &defaultSyscall;

/**
 * Construct a UdpDriver.
 *
 * \param localServiceLocator
 *      Specifies a particular socket on which this driver will listen
 *      for incoming packets. Must include "host" and "port" options
 *      identifying the desired socket.  If NULL then a port will be
 *      chosen by system software. Typically the socket is specified
 *      explicitly for server-side drivers but not for client-side
 *      drivers.
 */
UdpDriver::UdpDriver(const ServiceLocator* localServiceLocator)
    : socketFd(-1), transport(NULL), readHandler(),
      packetBufPool(), packetBufsUtilized(0), locatorString()
{
    if (localServiceLocator != NULL)
        locatorString = localServiceLocator->getOriginalString();

    int fd = sys->socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        throw DriverException(HERE, "UdpDriver couldn't create socket",
                              errno);
    }

    if (localServiceLocator != NULL) {
        IpAddress ipAddress(*localServiceLocator);
        int r = sys->bind(fd, &ipAddress.address, sizeof(ipAddress.address));
        if (r == -1) {
            int e = errno;
            sys->close(fd);
            throw DriverException(HERE,
                    format("UdpDriver couldn't bind to locator '%s'",
                    localServiceLocator->getOriginalString().c_str()), e);
        }
    }

    socketFd = fd;
}

/**
 * Destroy a UdpDriver. The socket associated with this driver is
 * closed.
 */
UdpDriver::~UdpDriver()
{
    if (packetBufsUtilized != 0)
        LOG(ERROR, "UdpDriver deleted with %d packets still in use",
            packetBufsUtilized);
    sys->close(socketFd);
}

// See docs in Driver class.
void
UdpDriver::connect(FastTransport* transport) {
    this->transport = transport;
    readHandler.construct(socketFd, this);
}

// See docs in Driver class.
void
UdpDriver::disconnect() {
    readHandler.destroy();
    this->transport = NULL;
}

// See docs in Driver class.
uint32_t
UdpDriver::getMaxPacketSize()
{
    return MAX_PAYLOAD_SIZE;
}

// See docs in Driver class.
void
UdpDriver::release(char *payload)
{
    // Note: the payload is actually contained in a PacketBuf structure,
    // which we return to a pool for reuse later.
    packetBufsUtilized--;
    assert(packetBufsUtilized >= 0);
    packetBufPool.destroy(
        reinterpret_cast<PacketBuf*>(payload - OFFSET_OF(PacketBuf, payload)));
}

// See docs in Driver class.
void
UdpDriver::sendPacket(const Address *addr,
                      const void *header,
                      uint32_t headerLen,
                      Buffer::Iterator *payload)
{
    uint32_t totalLength = headerLen +
                           (payload ? payload->getTotalLength() : 0);
    assert(totalLength <= MAX_PAYLOAD_SIZE);

    // one for header, the rest for payload
    uint32_t iovecs = 1 + (payload ? payload->getNumberChunks() : 0);

    struct iovec iov[iovecs];
    iov[0].iov_base = const_cast<void*>(header);
    iov[0].iov_len = headerLen;

    uint32_t i = 1;
    while (payload && !payload->isDone()) {
        iov[i].iov_base = const_cast<void*>(payload->getData());
        iov[i].iov_len = payload->getLength();
        ++i;
        payload->next();
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iovecs;

    const sockaddr* a = &(static_cast<const IpAddress*>(addr)->address);
    msg.msg_name = const_cast<sockaddr *>(a);
    msg.msg_namelen = sizeof(*a);

    ssize_t r = sys->sendmsg(socketFd, &msg, 0);
    if (r == -1) {
        int e = errno;
        close(socketFd);
        socketFd = -1;
        throw DriverException(HERE, "UdpDriver error sending to socket", e);
    }
    assert(static_cast<size_t>(r) == totalLength);
}

/**
 * Invoked by the dispatcher when our socket becomes readable.
 * Reads a packet from the socket, if there is one, and passes it on
 * to the associated FastTransport instance.
 */
void
UdpDriver::ReadHandler::operator() ()
{
    PacketBuf* buffer;
    buffer = driver->packetBufPool.construct();
    socklen_t addrlen = sizeof(&buffer->ipAddress.address);
    int r = sys->recvfrom(driver->socketFd, buffer->payload, MAX_PAYLOAD_SIZE,
                     MSG_DONTWAIT,
                     &buffer->ipAddress.address, &addrlen);
    if (r == -1) {
        driver->packetBufPool.destroy(buffer);
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        // TODO(stutsman) We could probably recover from a lot of errors here.
        throw DriverException(HERE, "UdpDriver error receiving from socket",
                              errno);
    }
    Received received;
    received.len = r;

    driver->packetBufsUtilized++;
    received.payload = buffer->payload;
    received.sender = &buffer->ipAddress;
    received.driver = driver;
    driver->transport->handleIncomingPacket(&received);
}

// See docs in Driver class.
ServiceLocator
UdpDriver::getServiceLocator()
{
    return ServiceLocator(locatorString);
}

} // namespace RAMCloud
