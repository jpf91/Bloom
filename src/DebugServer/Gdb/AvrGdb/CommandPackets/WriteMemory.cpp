#include "WriteMemory.hpp"

#include "src/DebugServer/Gdb/ResponsePackets/ErrorResponsePacket.hpp"
#include "src/DebugServer/Gdb/ResponsePackets/OkResponsePacket.hpp"

#include "src/Logger/Logger.hpp"
#include "src/Exceptions/Exception.hpp"

namespace Bloom::DebugServer::Gdb::AvrGdb::CommandPackets
{
    using Services::TargetControllerService;

    using ResponsePackets::ErrorResponsePacket;
    using ResponsePackets::OkResponsePacket;

    using namespace Bloom::Exceptions;

    WriteMemory::WriteMemory(const RawPacket& rawPacket, const TargetDescriptor& gdbTargetDescriptor)
        : CommandPacket(rawPacket)
    {
        if (this->data.size() < 4) {
            throw Exception("Invalid packet length");
        }

        const auto packetString = QString::fromLocal8Bit(
            reinterpret_cast<const char*>(this->data.data() + 1),
            static_cast<int>(this->data.size() - 1)
        );

        /*
         * The write memory ('M') packet consists of three segments, an address, a length and a buffer.
         * The address and length are separated by a comma character, and the buffer proceeds a colon character.
         */
        const auto packetSegments = packetString.split(",");
        if (packetSegments.size() != 2) {
            throw Exception(
                "Unexpected number of segments in packet data: " + std::to_string(packetSegments.size())
            );
        }

        bool conversionStatus = false;
        const auto gdbStartAddress = packetSegments.at(0).toUInt(&conversionStatus, 16);

        if (!conversionStatus) {
            throw Exception("Failed to parse start address from write memory packet data");
        }

        this->memoryType = gdbTargetDescriptor.getMemoryTypeFromGdbAddress(gdbStartAddress);
        this->startAddress = gdbStartAddress & ~(gdbTargetDescriptor.getMemoryOffset(this->memoryType));

        const auto lengthAndBufferSegments = packetSegments.at(1).split(":");
        if (lengthAndBufferSegments.size() != 2) {
            throw Exception(
                "Unexpected number of segments in packet data: "
                    + std::to_string(lengthAndBufferSegments.size())
            );
        }

        const auto bufferSize = lengthAndBufferSegments.at(0).toUInt(&conversionStatus, 16);
        if (!conversionStatus) {
            throw Exception("Failed to parse write length from write memory packet data");
        }

        this->buffer = Packet::hexToData(lengthAndBufferSegments.at(1).toStdString());

        if (this->buffer.size() != bufferSize) {
            throw Exception("Buffer size does not match length value given in write memory packet");
        }
    }

    void WriteMemory::handle(DebugSession& debugSession, TargetControllerService& targetControllerService) {
        Logger::info("Handling WriteMemory packet");

        try {
            const auto& memoryDescriptorsByType = debugSession.gdbTargetDescriptor.targetDescriptor.memoryDescriptorsByType;
            const auto memoryDescriptorIt = memoryDescriptorsByType.find(this->memoryType);

            if (memoryDescriptorIt == memoryDescriptorsByType.end()) {
                throw Exception("Target does not support the requested memory type.");
            }

            if (this->memoryType == Targets::TargetMemoryType::FLASH) {
                /*
                 * This shouldn't happen - GDB should send the FlashWrite (vFlashWrite) packet to write to the target's
                 * program memory.
                 *
                 * A number of actions have to be taken before we can write to the target's program memory - this is
                 * all covered in the FlashWrite and FlashDone command classes. I don't want to cover it again in here,
                 * so just respond with an error and request that this issue be reported.
                 */
                throw Exception(
                    "GDB attempted to write to program memory via an \"M\" packet - this is not supported. Please "
                    "report this issue to Bloom developers with the full debug log."
                );
            }

            if (this->buffer.size() == 0) {
                debugSession.connection.writePacket(OkResponsePacket());
                return;
            }

            const auto& memoryDescriptor = memoryDescriptorIt->second;

            if (this->memoryType == Targets::TargetMemoryType::EEPROM) {
                // GDB sends EEPROM addresses in relative form - we convert them to absolute form, here.
                this->startAddress = memoryDescriptor.addressRange.startAddress + this->startAddress;
            }

            /*
             * In AVR targets, RAM is mapped to many registers and peripherals - we don't want to block GDB from
             * accessing them.
             */
            const auto memoryStartAddress = (this->memoryType == Targets::TargetMemoryType::RAM)
                ? 0x00
                : memoryDescriptor.addressRange.startAddress;

            if (
                this->startAddress < memoryStartAddress
                || (this->startAddress + (this->buffer.size() - 1)) > memoryDescriptor.addressRange.endAddress
            ) {
                throw Exception(
                    "GDB requested access to memory which is outside the target's memory range"
                );
            }

            targetControllerService.writeMemory(
                this->memoryType,
                this->startAddress,
                this->buffer
            );

            debugSession.connection.writePacket(OkResponsePacket());

        } catch (const Exception& exception) {
            Logger::error("Failed to write memory to target - " + exception.getMessage());
            debugSession.connection.writePacket(ErrorResponsePacket());
        }
    }
}
