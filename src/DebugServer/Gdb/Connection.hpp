#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdint>
#include <utility>
#include <optional>
#include <vector>
#include <queue>
#include <array>
#include <chrono>

#include "src/Helpers/EventFdNotifier.hpp"
#include "src/Helpers/EpollInstance.hpp"

#include "src/DebugServer/Gdb/Packet.hpp"
#include "src/DebugServer/Gdb/ResponsePackets/ResponsePacket.hpp"

namespace Bloom::DebugServer::Gdb
{
    /**
     * The Connection class represents an active connection between the GDB RSP server and client.
     */
    class Connection
    {
    public:
        /*
         * GDB should never attempt to send more than this in a single instance.
         *
         * In the event that it does, we assume the worst and kill the connection.
         */
        static constexpr auto ABSOLUTE_MAXIMUM_PACKET_READ_SIZE = 2097000; // 2MiB

        explicit Connection(int serverSocketFileDescriptor, EventFdNotifier& interruptEventNotifier);

        Connection() = delete;
        Connection(const Connection&) = delete;
        Connection& operator = (Connection&) = delete;

        /*
         * TODO: Implement this. For now, use the move constructor.
         */
        Connection& operator = (Connection&&) = delete;

        Connection(Connection&& other) noexcept
            : interruptEventNotifier(other.interruptEventNotifier)
            , socketFileDescriptor(other.socketFileDescriptor)
            , epollInstance(std::move(other.epollInstance))
            , readInterruptEnabled(other.readInterruptEnabled)
        {
            other.socketFileDescriptor = std::nullopt;
        }

        ~Connection();

        /**
         * Obtains the human readable IP address of the connected client.
         *
         * @return
         */
        [[nodiscard]] std::string getIpAddress() const;

        /**
         * Waits for incoming data from the client and returns the raw GDB packets.
         *
         * @return
         */
        std::vector<RawPacket> readRawPackets();

        /**
         * Sends a response packet to the client.
         *
         * @param packet
         */
        void writePacket(const ResponsePackets::ResponsePacket& packet);

    private:
        std::optional<int> socketFileDescriptor;

        struct sockaddr_in socketAddress = {};

        /**
         * The interruptEventNotifier (instance of EventFdNotifier) allows us to interrupt blocking I/O calls on this
         * connection's socket. Under the hood, the EventFdNotifier class is just an RAII wrapper for a Linux eventfd
         * object.
         *
         * The file descriptors of the eventfd object and the socket are both added to an EpollInstance (which is just
         * an RAII wrapper for a Linux epoll instance). The EpollInstance object is then used to wait for events on
         * either of the two file descriptors. See any of the Connection I/O functions (e.g Connection::read()) for
         * more on this.
         *
         * See the EventFdNotifier and EpollInstance classes for more.
         */
        EventFdNotifier& interruptEventNotifier;
        EpollInstance epollInstance = EpollInstance();

        bool readInterruptEnabled = false;

        /**
         * Accepts a connection on serverSocketFileDescriptor.
         *
         * @param serverSocketFileDescriptor
         */
        void accept(int serverSocketFileDescriptor);

        /**
         * Closes the connection with the client.
         */
        void close() noexcept;

        /**
         * Reads data from the client into a raw buffer.
         *
         * @param bytes
         *  Number of bytes to read.
         *
         * @param interruptible
         *  If this flag is set to false, no other component within Bloom will be able to gracefully interrupt
         *  the read (via means of this->interruptEventNotifier). This flag has no effect if this->readInterruptEnabled
         *  is false.
         *
         * @param timeout
         *  The timeout in milliseconds. If not supplied, no timeout will be applied.
         *
         * @return
         */
        std::vector<unsigned char> read(
            std::optional<std::size_t> bytes = std::nullopt,
            bool interruptible = true,
            std::optional<std::chrono::milliseconds> timeout = std::nullopt
        );

        /**
         * Does the same as Connection::read(), but only reads a single byte.
         *
         * @param interruptible
         *  See Connection::read().
         *
         * @return
         */
        std::optional<unsigned char> readSingleByte(bool interruptible = true);

        /**
         * Writes data from a raw buffer to the client connection.
         *
         * @param buffer
         */
        void write(const std::vector<unsigned char>& buffer);

        /**
         * Removes this->interruptEventNotifier's file descriptor from the EpollInstance (this->epollInstance),
         * preventing subsequent I/O operations on this->socketFileDescriptor from being interrupted.
         */
        void disableReadInterrupts();

        /**
         * Inserts this->interruptEventNotifier's file descriptor into the EpollInstance (this->epollInstance),
         * allowing for subsequent I/O operations on this->socketFileDescriptor to be interrupted.
         */
        void enableReadInterrupts();
    };
}
