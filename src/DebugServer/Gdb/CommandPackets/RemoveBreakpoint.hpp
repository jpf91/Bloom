#pragma once

#include <cstdint>
#include <string>
#include <set>

#include "CommandPacket.hpp"
#include "src/DebugServer/Gdb/BreakpointType.hpp"

#include "src/Targets/TargetMemory.hpp"

namespace Bloom::DebugServer::Gdb::CommandPackets
{
    /**
     * The RemoveBreakpoint class implements the structure for "z" command packets. Upon receiving this command, the
     * server is expected to remove a breakpoint at the specified address.
     */
    class RemoveBreakpoint: public CommandPacket
    {
    public:
        /**
         * Breakpoint type (Software or Hardware)
         */
        BreakpointType type = BreakpointType::UNKNOWN;

        /**
         * Address at which the breakpoint should be located.
         */
        Targets::TargetMemoryAddress address = 0;

        explicit RemoveBreakpoint(const RawPacket& rawPacket);

        void handle(
            DebugSession& debugSession,
            Services::TargetControllerService& targetControllerService
        ) override;
    };
}
