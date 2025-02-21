target_sources(
    Bloom
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/DebugServerComponent.cpp

        # GDB RSP Server
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/GdbRspDebugServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/GdbDebugServerConfig.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/Connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/DebugSession.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/ResponsePackets/SupportedFeaturesResponse.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/CommandPacket.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/SupportedFeaturesQuery.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/ReadRegisters.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/WriteRegister.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/ContinueExecution.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/StepExecution.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/InterruptExecution.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/SetBreakpoint.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/RemoveBreakpoint.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/Monitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/ResetTarget.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/HelpMonitorInfo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/BloomVersion.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/BloomVersionMachine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/GenerateSvd.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/Detach.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/CommandPackets/EepromFill.cpp

        # AVR GDB RSP Server
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/AvrGdbRsp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/TargetDescriptor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/ReadMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/WriteMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/ReadMemoryMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/FlashErase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/FlashWrite.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Gdb/AvrGdb/CommandPackets/FlashDone.cpp
)

# DebugServer resources
qt_add_resources(
    Bloom
    "DebugServerResources"
    PREFIX
        "/compiled/src/DebugServer"
    FILES
        # GDB RSP Server
        "./Gdb/Resources/GdbHelpMonitorInfo.txt"
)
