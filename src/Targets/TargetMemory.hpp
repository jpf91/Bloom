#pragma once

#include <cstdint>
#include <vector>
#include <optional>

namespace Bloom::Targets
{
    using TargetMemoryAddress = std::uint32_t;
    using TargetMemorySize = std::uint32_t;
    using TargetProgramCounter = TargetMemoryAddress;
    using TargetStackPointer = TargetMemoryAddress;
    using TargetMemoryBuffer = std::vector<unsigned char>;

    enum class TargetMemoryEndianness: std::uint8_t
    {
        BIG,
        LITTLE,
    };

    enum class TargetMemoryType: std::uint8_t
    {
        FLASH,
        RAM,
        EEPROM,
        FUSES,
        OTHER,
    };

    struct TargetMemoryAddressRange
    {
        TargetMemoryAddress startAddress = 0;
        TargetMemoryAddress endAddress = 0;

        TargetMemoryAddressRange() = default;
        TargetMemoryAddressRange(TargetMemoryAddress startAddress, TargetMemoryAddress endAddress)
            : startAddress(startAddress)
            , endAddress(endAddress)
        {};

        bool operator == (const TargetMemoryAddressRange& rhs) const {
            return this->startAddress == rhs.startAddress && this->endAddress == rhs.endAddress;
        }

        bool operator < (const TargetMemoryAddressRange& rhs) const {
            return this->startAddress < rhs.startAddress;
        }

        [[nodiscard]] bool intersectsWith(const TargetMemoryAddressRange& other) const {
            return
                (other.startAddress <= this->startAddress && other.endAddress >= this->startAddress)
                || (other.startAddress >= this->startAddress && other.startAddress <= this->endAddress)
            ;
        }

        [[nodiscard]] bool contains(TargetMemoryAddress address) const {
            return address >= this->startAddress && address <= this->endAddress;
        }

        [[nodiscard]] bool contains(const TargetMemoryAddressRange& addressRange) const {
            return this->startAddress <= addressRange.startAddress && this->endAddress >= addressRange.endAddress;
        }
    };

    struct TargetMemoryAccess
    {
        bool readable = false;
        bool writeable = false;
        bool writeableDuringDebugSession = false;

        TargetMemoryAccess(
            bool readable,
            bool writeable,
            bool writeableDuringDebugSession
        )
            : readable(readable)
            , writeable(writeable)
            , writeableDuringDebugSession(writeableDuringDebugSession)
        {}
    };

    struct TargetMemoryDescriptor
    {
        TargetMemoryType type;
        TargetMemoryAddressRange addressRange;
        TargetMemoryAccess access;
        std::optional<TargetMemorySize> pageSize;

        TargetMemoryDescriptor(
            TargetMemoryType type,
            TargetMemoryAddressRange addressRange,
            TargetMemoryAccess access,
            std::optional<TargetMemorySize> pageSize = std::nullopt
        )
            : type(type)
            , addressRange(addressRange)
            , access(access)
            , pageSize(pageSize)
        {};

        bool operator == (const TargetMemoryDescriptor& rhs) const {
            return this->type == rhs.type && this->addressRange == rhs.addressRange;
        }

        [[nodiscard]] TargetMemorySize size() const {
            return (this->addressRange.endAddress - this->addressRange.startAddress) + 1;
        }
    };
}
