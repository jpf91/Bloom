#pragma once

#include <optional>

#include "CommandPacket.hpp"
#include "src/Targets/TargetRegister.hpp"

namespace Bloom::DebugServer::Gdb::CommandPackets
{
    /**
     * The WriteRegisters class implements the structure for "P" packets. Upon receiving this packet,
     * server is expected to update a register value to the target.
     */
    class WriteRegister: public CommandPacket
    {
    public:
        int registerNumber = 0;
        std::vector<unsigned char> registerValue;

        explicit WriteRegister(const RawPacket& rawPacket);

        void handle(
            DebugSession& debugSession,
            Services::TargetControllerService& targetControllerService
        ) override;
    };
}
