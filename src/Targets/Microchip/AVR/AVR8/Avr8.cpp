#include "Avr8.hpp"

#include <cassert>
#include <bitset>
#include <limits>
#include <thread>
#include <algorithm>

#include "src/Logger/Logger.hpp"
#include "src/Services/PathService.hpp"
#include "src/Services/StringService.hpp"

#include "src/Exceptions/InvalidConfig.hpp"
#include "Exceptions/DebugWirePhysicalInterfaceError.hpp"
#include "src/Targets/TargetRegister.hpp"

#include "src/Targets/Microchip/AVR/Fuse.hpp"

// Derived AVR8 targets
#include "XMega/XMega.hpp"
#include "Mega/Mega.hpp"
#include "Tiny/Tiny.hpp"

namespace Bloom::Targets::Microchip::Avr::Avr8Bit
{
    using namespace Exceptions;

    void Avr8::preActivationConfigure(const TargetConfig& targetConfig) {
        Target::preActivationConfigure(targetConfig);

        // Extract AVR8 specific target config
        this->targetConfig = Avr8TargetConfig(targetConfig);

        if (this->targetConfig->name == "avr8") {
            Logger::warning("The \"avr8\" target name is deprecated and will be removed in a later version.");
        }

        if (this->family.has_value()) {
            this->avr8DebugInterface->setFamily(this->family.value());

            if (!this->supportedPhysicalInterfaces.contains(this->targetConfig->physicalInterface)) {
                /*
                 * The user has selected a physical interface that does not appear to be supported by the selected
                 * target.
                 *
                 * Bloom's target description files provide a list of supported physical interfaces for each target
                 * (which is how this->supportedPhysicalInterfaces is populated), but it's possible that this list may
                 * be wrong/incomplete. For this reason, we don't throw an exception here. Instead, we just present the
                 * user with a warning and a list of physical interfaces known to be supported by their selected target.
                 */
                const auto physicalInterfaceNames = getPhysicalInterfaceNames();

                std::string supportedPhysicalInterfaceList = std::accumulate(
                    this->supportedPhysicalInterfaces.begin(),
                    this->supportedPhysicalInterfaces.end(),
                    std::string(),
                    [&physicalInterfaceNames] (const std::string& string, PhysicalInterface physicalInterface) {
                        if (physicalInterface == PhysicalInterface::ISP) {
                            /*
                             * Don't include the ISP interface in the list of supported interfaces, as doing so may
                             * mislead the user into thinking the ISP interface can be used for debugging operations.
                             */
                            return string;
                        }

                        return string + "\n - " + physicalInterfaceNames.at(physicalInterface);
                    }
                );

                Logger::warning(
                    "\nThe selected target (" + this->name + ") does not support the selected physical interface ("
                        + physicalInterfaceNames.at(this->targetConfig->physicalInterface) + "). Target activation "
                        "will likely fail. The target supports the following physical interfaces: \n"
                        + supportedPhysicalInterfaceList + "\n\nFor physical interface configuration values, see "
                        + Services::PathService::homeDomainName() + "/docs/configuration/avr8-physical-interfaces. \n\nIf this "
                        "information is incorrect, please report this to Bloom developers via "
                        + Services::PathService::homeDomainName() + "/report-issue.\n"
                );
            }

        } else {
            if (this->targetConfig->physicalInterface == PhysicalInterface::JTAG) {
                throw InvalidConfig(
                    "The JTAG physical interface cannot be used with an ambiguous target name"
                        " - please specify the exact name of the target in your configuration file. "
                        "See " + Services::PathService::homeDomainName() + "/docs/supported-targets"
                );
            }

            if (this->targetConfig->physicalInterface == PhysicalInterface::UPDI) {
                throw InvalidConfig(
                    "The UPDI physical interface cannot be used with an ambiguous target name"
                        " - please specify the exact name of the target in your configuration file. "
                        "See " + Services::PathService::homeDomainName() + "/docs/supported-targets"
                );
            }
        }

        if (
            this->targetConfig->manageDwenFuseBit
            && this->avrIspInterface == nullptr
            && this->targetConfig->physicalInterface == PhysicalInterface::DEBUG_WIRE
        ) {
            Logger::warning(
                "The connected debug tool (or associated driver) does not provide any ISP interface. "
                    "Bloom will be unable to update the DWEN fuse bit in the event of a debugWire activation failure."
            );
        }

        if (
            this->targetConfig->manageOcdenFuseBit
            && this->targetConfig->physicalInterface != PhysicalInterface::JTAG
        ) {
            Logger::warning(
                "The 'manageOcdenFuseBit' parameter only applies to JTAG targets. It will be ignored in this session."
            );
        }

        this->avr8DebugInterface->configure(*(this->targetConfig));

        if (this->avrIspInterface != nullptr) {
            this->avrIspInterface->configure(*(this->targetConfig));
        }
    }

    void Avr8::postActivationConfigure() {
        if (!this->targetDescriptionFile.has_value()) {
            this->initFromTargetDescriptionFile();
        }

        /*
         * The signature obtained from the device should match what is in the target description file
         *
         * We don't use this->getId() here as that could return the ID that was extracted from the target description
         * file (which it would, if the user specified the exact target name in their project config - see
         * Avr8::getId() and TargetControllerComponent::getSupportedTargets() for more).
         */
        const auto targetSignature = this->avr8DebugInterface->getDeviceId();
        const auto tdSignature = this->targetDescriptionFile->getTargetSignature();

        if (targetSignature != tdSignature) {
            throw Exception(
                "Failed to validate connected target - target signature mismatch.\nThe target signature"
                    " (\"" + targetSignature.toHex() + "\") does not match the AVR8 target description signature (\""
                    + tdSignature.toHex() + "\"). This will likely be due to an incorrect target name in the "
                    + "configuration file (bloom.yaml)."
            );
        }
    }

    void Avr8::postPromotionConfigure() {
        if (!this->family.has_value()) {
            throw Exception("Failed to resolve AVR8 family");
        }

        this->avr8DebugInterface->setFamily(this->family.value());
        this->avr8DebugInterface->setTargetParameters(this->targetParameters.value());
    }

    void Avr8::activate() {
        if (this->isActivated()) {
            return;
        }

        this->avr8DebugInterface->init();

        if (this->targetParameters.has_value()) {
            this->avr8DebugInterface->setTargetParameters(this->targetParameters.value());
        }

        try {
            this->avr8DebugInterface->activate();

        } catch (const Exceptions::DebugWirePhysicalInterfaceError& debugWireException) {
            // We failed to activate the debugWire physical interface. DWEN fuse bit may need updating.

            if (!this->targetConfig->manageDwenFuseBit) {
                throw TargetOperationFailure(
                    "Failed to activate debugWire physical interface - check target connection and DWEN fuse "
                        "bit. Bloom can manage the DWEN fuse bit automatically. For instructions on enabling this "
                        "function, see " + Services::PathService::homeDomainName() + "/docs/debugging-avr-debugwire"
                );
            }

            try {
                Logger::warning(
                    "Failed to activate the debugWire physical interface - attempting to access target via "
                        "the ISP interface, for DWEN fuse bit inspection."
                );
                this->updateDwenFuseBit(true);

                // If the debug tool provides a TargetPowerManagementInterface, attempt to cycle the target power
                if (
                    this->targetPowerManagementInterface != nullptr
                    && this->targetConfig->cycleTargetPowerPostDwenUpdate
                ) {
                    Logger::info("Cycling target power");

                    Logger::debug("Disabling target power");
                    this->targetPowerManagementInterface->disableTargetPower();

                    Logger::debug(
                        "Holding power off for ~"
                            + std::to_string(this->targetConfig->targetPowerCycleDelay.count())
                            + " ms"
                    );
                    std::this_thread::sleep_for(this->targetConfig->targetPowerCycleDelay);

                    Logger::debug("Enabling target power");
                    this->targetPowerManagementInterface->enableTargetPower();

                    Logger::debug(
                        "Waiting ~" + std::to_string(this->targetConfig->targetPowerCycleDelay.count())
                            + " ms for target power-up"
                    );
                    std::this_thread::sleep_for(this->targetConfig->targetPowerCycleDelay);
                }

            } catch (const Exception& exception) {
                throw Exception(
                    "Failed to access/update DWEN fuse bit via ISP interface - " + exception.getMessage()
                );
            }

            Logger::info("Retrying debugWire physical interface activation");
            this->avr8DebugInterface->activate();
        }

        if (
            this->targetConfig->physicalInterface == PhysicalInterface::JTAG
            && this->targetConfig->manageOcdenFuseBit
        ) {
            Logger::debug("Attempting OCDEN fuse bit management");
            this->updateOcdenFuseBit(true);
        }

        this->activated = true;
        this->avr8DebugInterface->reset();
    }

    void Avr8::deactivate() {
        try {
            this->stop();
            this->clearAllBreakpoints();

            if (
                this->targetConfig->physicalInterface == PhysicalInterface::JTAG
                && this->targetConfig->manageOcdenFuseBit
            ) {
                Logger::debug("Attempting OCDEN fuse bit management");
                this->updateOcdenFuseBit(false);

            } else {
                this->avr8DebugInterface->deactivate();
            }

            this->activated = false;

        } catch (const Exception& exception) {
            Logger::error("Failed to deactivate AVR8 target - " + exception.getMessage());
        }
    }

    std::unique_ptr<Targets::Target> Avr8::promote() {
        std::unique_ptr<Targets::Target> promoted = nullptr;

        if (this->family.has_value()) {
            // Promote generic AVR8 target to correct family.
            switch (this->family.value()) {
                case Family::XMEGA: {
                    Logger::info("AVR8 target promoted to XMega target");
                    promoted = std::make_unique<XMega>(*this);
                    break;
                }
                case Family::MEGA: {
                    Logger::info("AVR8 target promoted to megaAVR target");
                    promoted = std::make_unique<Mega>(*this);
                    break;
                }
                case Family::TINY: {
                    Logger::info("AVR8 target promoted to tinyAVR target");
                    promoted = std::make_unique<Tiny>(*this);
                    break;
                }
                default: {
                    break;
                }
            }
        }

        return promoted;
    }

    TargetDescriptor Avr8Bit::Avr8::getDescriptor() {
        auto descriptor = TargetDescriptor();
        descriptor.id = this->getHumanReadableId();
        descriptor.name = this->getName();
        descriptor.vendorName = std::string("Microchip");
        descriptor.programMemoryType = Targets::TargetMemoryType::FLASH;
        descriptor.registerDescriptorsByType = this->targetRegisterDescriptorsByType;
        descriptor.memoryDescriptorsByType = this->targetMemoryDescriptorsByType;

        std::transform(
            this->targetVariantsById.begin(),
            this->targetVariantsById.end(),
            std::back_inserter(descriptor.variants),
            [] (auto& variantToIdPair) {
                return variantToIdPair.second;
            }
        );

        return descriptor;
    }

    void Avr8::run(std::optional<TargetMemoryAddress> toAddress) {
        if (toAddress.has_value()) {
            return this->avr8DebugInterface->runTo(*toAddress);
        }

        this->avr8DebugInterface->run();
    }

    void Avr8::stop() {
        this->avr8DebugInterface->stop();
    }

    void Avr8::step() {
        this->avr8DebugInterface->step();
    }

    void Avr8::reset() {
        this->avr8DebugInterface->reset();
    }

    void Avr8::setBreakpoint(std::uint32_t address) {
        this->avr8DebugInterface->setBreakpoint(address);
    }

    void Avr8::removeBreakpoint(std::uint32_t address) {
        this->avr8DebugInterface->clearBreakpoint(address);
    }

    void Avr8::clearAllBreakpoints() {
        this->avr8DebugInterface->clearAllBreakpoints();
    }

    void Avr8::writeRegisters(TargetRegisters registers) {
        for (auto registerIt = registers.begin(); registerIt != registers.end();) {
            if (registerIt->descriptor.type == TargetRegisterType::PROGRAM_COUNTER) {
                auto programCounterBytes = registerIt->value;

                if (programCounterBytes.size() < 4) {
                    // All PC register values should be at least 4 bytes in size
                    programCounterBytes.insert(programCounterBytes.begin(), 4 - programCounterBytes.size(), 0x00);
                }

                this->setProgramCounter(static_cast<std::uint32_t>(
                    programCounterBytes[0] << 24
                    | programCounterBytes[1] << 16
                    | programCounterBytes[2] << 8
                    | programCounterBytes[3]
                ));

                registerIt = registers.erase(registerIt);

            } else {
                registerIt++;
            }
        }

        if (!registers.empty()) {
            this->avr8DebugInterface->writeRegisters(registers);
        }
    }

    TargetRegisters Avr8::readRegisters(TargetRegisterDescriptors descriptors) {
        TargetRegisters registers;

        for (auto registerDescriptorIt = descriptors.begin(); registerDescriptorIt != descriptors.end();) {
            const auto& descriptor = *registerDescriptorIt;

            if (descriptor.type == TargetRegisterType::PROGRAM_COUNTER) {
                registers.push_back(this->getProgramCounterRegister());

                registerDescriptorIt = descriptors.erase(registerDescriptorIt);

            } else {
                registerDescriptorIt++;
            }
        }

        if (!descriptors.empty()) {
            auto otherRegisters = this->avr8DebugInterface->readRegisters(descriptors);
            registers.insert(registers.end(), otherRegisters.begin(), otherRegisters.end());
        }

        return registers;
    }

    TargetMemoryBuffer Avr8::readMemory(
        TargetMemoryType memoryType,
        std::uint32_t startAddress,
        std::uint32_t bytes,
        const std::set<Targets::TargetMemoryAddressRange>& excludedAddressRanges
    ) {
        return this->avr8DebugInterface->readMemory(memoryType, startAddress, bytes, excludedAddressRanges);
    }

    void Avr8::writeMemory(TargetMemoryType memoryType, std::uint32_t startAddress, const TargetMemoryBuffer& buffer) {
        if (memoryType == TargetMemoryType::FLASH && !this->programmingModeEnabled()) {
            throw Exception("Attempted FLASH memory write with no active programming session.");
        }

        this->avr8DebugInterface->writeMemory(memoryType, startAddress, buffer);
    }

    void Avr8::eraseMemory(TargetMemoryType memoryType) {
        if (memoryType == TargetMemoryType::FLASH) {
            if (this->targetConfig->physicalInterface == PhysicalInterface::DEBUG_WIRE) {
                // debugWire targets do not need to be erased
                return;
            }

            return this->avr8DebugInterface->eraseProgramMemory();
        }

        /*
         * Debug tools do not have to support the erasing of RAM or EEPROM memory. We just implement this as a
         * write operation.
         */
        this->writeMemory(
            memoryType,
            memoryType == TargetMemoryType::RAM
                ? this->targetParameters->ramStartAddress.value()
                : this->targetParameters->eepromStartAddress.value(),
            TargetMemoryBuffer(
                memoryType == TargetMemoryType::RAM
                    ? this->targetParameters->ramSize.value()
                    : this->targetParameters->eepromSize.value(),
                0xFF
            )
        );
    }

    TargetState Avr8::getState() {
        return this->avr8DebugInterface->getTargetState();
    }

    std::uint32_t Avr8::getProgramCounter() {
        return this->avr8DebugInterface->getProgramCounter();
    }

    TargetRegister Avr8::getProgramCounterRegister() {
        auto programCounter = this->getProgramCounter();

        return TargetRegister(TargetRegisterDescriptor(TargetRegisterType::PROGRAM_COUNTER), {
            static_cast<unsigned char>(programCounter >> 24),
            static_cast<unsigned char>(programCounter >> 16),
            static_cast<unsigned char>(programCounter >> 8),
            static_cast<unsigned char>(programCounter),
        });
    }

    void Avr8::setProgramCounter(std::uint32_t programCounter) {
        this->avr8DebugInterface->setProgramCounter(programCounter);
    }

    std::uint32_t Avr8::getStackPointer() {
        const auto stackPointerRegister = this->readRegisters(
            {this->targetRegisterDescriptorsByType.at(TargetRegisterType::STACK_POINTER)}
        ).front();

        std::uint32_t stackPointer = 0;
        for (std::size_t i = 0; i < stackPointerRegister.size() && i < 4; i++) {
            stackPointer = (stackPointer << (8 * i)) | stackPointerRegister.value[i];
        }

        return stackPointer;
    }

    std::map<int, TargetPinState> Avr8::getPinStates(int variantId) {
        const auto targetVariantIt = this->targetVariantsById.find(variantId);

        if (targetVariantIt == this->targetVariantsById.end()) {
            throw Exception("Invalid target variant ID");
        }

        std::map<int, TargetPinState> output;
        const auto& variant = targetVariantIt->second;

        /*
         * To prevent the number of memory reads we perform here, we cache the data and map it by start address.
         *
         * This way, we only perform 3 memory reads for a target variant with 3 ports - one per port (instead of one
         * per pin).
         *
         * We may be able to make this more efficient by combining reads for ports with aligned memory addresses. This
         * will be considered when the need for it becomes apparent.
         */
        std::map<std::uint16_t, TargetMemoryBuffer> cachedMemoryByStartAddress;
        const auto readMemoryBitset = [this, &cachedMemoryByStartAddress] (std::uint16_t startAddress) {
            auto cachedByteIt = cachedMemoryByStartAddress.find(startAddress);

            if (cachedByteIt == cachedMemoryByStartAddress.end()) {
                cachedByteIt = cachedMemoryByStartAddress.insert(
                    std::pair(
                        startAddress,
                        this->readMemory(TargetMemoryType::RAM, startAddress, 1)
                    )
                ).first;
            }

            return std::bitset<std::numeric_limits<unsigned char>::digits>(
                cachedByteIt->second.at(0)
            );
        };

        for (const auto& [pinNumber, pinDescriptor] : variant.pinDescriptorsByNumber) {
            const auto padIt = this->padDescriptorsByName.find(pinDescriptor.padName);

            if (padIt != this->padDescriptorsByName.end()) {
                const auto& pad = padIt->second;

                if (!pad.gpioPinNumber.has_value()) {
                    continue;
                }

                auto pinState = TargetPinState();

                if (pad.gpioDdrAddress.has_value()) {
                    const auto ddrValue = readMemoryBitset(pad.gpioDdrAddress.value());

                    pinState.ioDirection = ddrValue.test(pad.gpioPinNumber.value()) ?
                        TargetPinState::IoDirection::OUTPUT : TargetPinState::IoDirection::INPUT;

                    if (pinState.ioDirection == TargetPinState::IoDirection::OUTPUT
                        && pad.gpioPortAddress.has_value()
                    ) {
                        const auto portRegisterValueBitset = readMemoryBitset(pad.gpioPortAddress.value());
                        pinState.ioState = portRegisterValueBitset.test(pad.gpioPinNumber.value()) ?
                            TargetPinState::IoState::HIGH : TargetPinState::IoState::LOW;

                    } else if (pinState.ioDirection == TargetPinState::IoDirection::INPUT
                        && pad.gpioPortInputAddress.has_value()
                    ) {
                        const auto portInputRegisterValue = readMemoryBitset(pad.gpioPortInputAddress.value());
                        pinState.ioState = portInputRegisterValue.test(pad.gpioPinNumber.value()) ?
                            TargetPinState::IoState::HIGH : TargetPinState::IoState::LOW;
                    }
                }

                output.insert(std::pair(pinNumber, pinState));
            }
        }

        return output;
    }

    void Avr8::setPinState(const TargetPinDescriptor& pinDescriptor, const TargetPinState& state) {
        const auto targetVariantIt = this->targetVariantsById.find(pinDescriptor.variantId);

        if (targetVariantIt == this->targetVariantsById.end()) {
            throw Exception("Invalid target variant ID");
        }

        const auto padDescriptorIt = this->padDescriptorsByName.find(pinDescriptor.padName);

        if (padDescriptorIt == this->padDescriptorsByName.end()) {
            throw Exception("Unknown pad");
        }

        if (!state.ioDirection.has_value()) {
            throw Exception("Missing IO direction state");
        }

        const auto& variant = targetVariantIt->second;
        const auto& padDescriptor = padDescriptorIt->second;
        auto ioState = state.ioState;

        if (state.ioDirection == TargetPinState::IoDirection::INPUT) {
            // When setting the direction to INPUT, we must always set the IO pin state to LOW
            ioState = TargetPinState::IoState::LOW;
        }

        if (
            !padDescriptor.gpioDdrAddress.has_value()
            || !padDescriptor.gpioPortAddress.has_value()
            || !padDescriptor.gpioPinNumber.has_value()
        ) {
            throw Exception("Inadequate pad descriptor");
        }

        const auto pinNumber = padDescriptor.gpioPinNumber.value();
        const auto ddrAddress = padDescriptor.gpioDdrAddress.value();
        const auto ddrValue = this->readMemory(TargetMemoryType::RAM, ddrAddress, 1);

        if (ddrValue.empty()) {
            throw Exception("Failed to read DDR value");
        }

        auto ddrValueBitset = std::bitset<std::numeric_limits<unsigned char>::digits>(ddrValue.front());
        if (ddrValueBitset.test(pinNumber) != (state.ioDirection == TargetPinState::IoDirection::OUTPUT)) {
            // DDR needs updating
            ddrValueBitset.set(pinNumber, (state.ioDirection == TargetPinState::IoDirection::OUTPUT));

            this->writeMemory(
                TargetMemoryType::RAM,
                ddrAddress,
                {static_cast<unsigned char>(ddrValueBitset.to_ulong())}
            );
        }

        if (ioState.has_value()) {
            const auto portRegisterAddress = padDescriptor.gpioPortAddress.value();
            const auto portRegisterValue = this->readMemory(TargetMemoryType::RAM, portRegisterAddress, 1);

            if (portRegisterValue.empty()) {
                throw Exception("Failed to read PORT register value");
            }

            auto portRegisterValueBitset = std::bitset<std::numeric_limits<unsigned char>::digits>(
                portRegisterValue.front()
            );

            if (portRegisterValueBitset.test(pinNumber) != (ioState == TargetPinState::IoState::HIGH)) {
                // PORT set register needs updating
                portRegisterValueBitset.set(pinNumber, (ioState == TargetPinState::IoState::HIGH));

                this->writeMemory(
                    TargetMemoryType::RAM,
                    portRegisterAddress,
                    {static_cast<unsigned char>(portRegisterValueBitset.to_ulong())}
                );
            }
        }
    }

    void Avr8::enableProgrammingMode() {
        this->avr8DebugInterface->enableProgrammingMode();
        this->progModeEnabled = true;
    }

    void Avr8::disableProgrammingMode() {
        this->avr8DebugInterface->disableProgrammingMode();
        this->progModeEnabled = false;
    }

    bool Avr8::programmingModeEnabled() {
        return this->progModeEnabled;
    }

    void Avr8::initFromTargetDescriptionFile() {
        this->targetDescriptionFile = TargetDescription::TargetDescriptionFile(
            this->getId(),
            (!this->name.empty()) ? std::optional(this->name) : std::nullopt
        );

        this->name = this->targetDescriptionFile->getTargetName();
        this->family = this->targetDescriptionFile->getFamily();
        this->supportedPhysicalInterfaces = this->targetDescriptionFile->getSupportedPhysicalInterfaces();

        this->targetParameters = this->targetDescriptionFile->getTargetParameters();
        this->padDescriptorsByName = this->targetDescriptionFile->getPadDescriptorsMappedByName();
        this->targetVariantsById = this->targetDescriptionFile->getVariantsMappedById();

        if (!this->targetParameters->stackPointerRegisterLowAddress.has_value()) {
            throw Exception(
                "Failed to load sufficient AVR8 target parameters - missing stack pointer start address"
            );
        }

        if (!this->targetParameters->statusRegisterStartAddress.has_value()) {
            throw Exception(
                "Failed to load sufficient AVR8 target parameters - missing status register start address"
            );
        }

        this->loadTargetRegisterDescriptors();
        this->loadTargetMemoryDescriptors();
    }

    void Avr8::loadTargetRegisterDescriptors() {
        this->targetRegisterDescriptorsByType = this->targetDescriptionFile->getRegisterDescriptorsMappedByType();

        /*
         * All AVR8 targets possess 32 general purpose CPU registers. These are not described in the TDF, so we
         * construct the descriptors for them here.
         */
        auto gpRegisterStartAddress = this->targetParameters->gpRegisterStartAddress.value_or(0);
        for (std::uint8_t i = 0; i <= 31; i++) {
            auto generalPurposeRegisterDescriptor = TargetRegisterDescriptor();
            generalPurposeRegisterDescriptor.startAddress = gpRegisterStartAddress + i;
            generalPurposeRegisterDescriptor.size = 1;
            generalPurposeRegisterDescriptor.type = TargetRegisterType::GENERAL_PURPOSE_REGISTER;
            generalPurposeRegisterDescriptor.name = "r" + std::to_string(i);
            generalPurposeRegisterDescriptor.groupName = "general purpose cpu";
            generalPurposeRegisterDescriptor.readable = true;
            generalPurposeRegisterDescriptor.writable = true;

            this->targetRegisterDescriptorsByType[generalPurposeRegisterDescriptor.type].insert(
                generalPurposeRegisterDescriptor
            );
        }

        /*
         * The SP and SREG registers are described in the TDF, so we could just use the descriptors extracted from the
         * TDF. The problem with that is, sometimes the SP register consists of two bytes; an SPL and an SPH. These need
         * to be combined into one register descriptor. This is why we just use what we already have in
         * this->targetParameters.
         */
        auto stackPointerRegisterDescriptor = TargetRegisterDescriptor();
        stackPointerRegisterDescriptor.type = TargetRegisterType::STACK_POINTER;
        stackPointerRegisterDescriptor.startAddress = this->targetParameters->stackPointerRegisterLowAddress.value();
        stackPointerRegisterDescriptor.size = this->targetParameters->stackPointerRegisterSize.value();
        stackPointerRegisterDescriptor.name = "SP";
        stackPointerRegisterDescriptor.groupName = "CPU";
        stackPointerRegisterDescriptor.description = "Stack Pointer Register";
        stackPointerRegisterDescriptor.readable = true;
        stackPointerRegisterDescriptor.writable = true;

        auto statusRegisterDescriptor = TargetRegisterDescriptor();
        statusRegisterDescriptor.type = TargetRegisterType::STATUS_REGISTER;
        statusRegisterDescriptor.startAddress = this->targetParameters->statusRegisterStartAddress.value();
        statusRegisterDescriptor.size = this->targetParameters->statusRegisterSize.value();
        statusRegisterDescriptor.name = "SREG";
        statusRegisterDescriptor.groupName = "CPU";
        statusRegisterDescriptor.description = "Status Register";
        statusRegisterDescriptor.readable = true;
        statusRegisterDescriptor.writable = true;

        auto programCounterRegisterDescriptor = TargetRegisterDescriptor();
        programCounterRegisterDescriptor.type = TargetRegisterType::PROGRAM_COUNTER;
        programCounterRegisterDescriptor.size = 4;
        programCounterRegisterDescriptor.name = "PC";
        programCounterRegisterDescriptor.groupName = "CPU";
        programCounterRegisterDescriptor.description = "Program Counter";
        programCounterRegisterDescriptor.readable = true;
        programCounterRegisterDescriptor.writable = true;

        this->targetRegisterDescriptorsByType[stackPointerRegisterDescriptor.type].insert(
            stackPointerRegisterDescriptor
        );
        this->targetRegisterDescriptorsByType[statusRegisterDescriptor.type].insert(
            statusRegisterDescriptor
        );
        this->targetRegisterDescriptorsByType[programCounterRegisterDescriptor.type].insert(
            programCounterRegisterDescriptor
        );
    }

    void Avr8::loadTargetMemoryDescriptors() {
        const auto ramStartAddress = this->targetParameters->ramStartAddress.value();
        const auto flashStartAddress = this->targetParameters->flashStartAddress.value();

        this->targetMemoryDescriptorsByType.insert(std::pair(
            TargetMemoryType::RAM,
            TargetMemoryDescriptor(
                TargetMemoryType::RAM,
                TargetMemoryAddressRange(
                    ramStartAddress,
                    ramStartAddress + this->targetParameters->ramSize.value() - 1
                ),
                TargetMemoryAccess(true, true, true)
            )
        ));

        this->targetMemoryDescriptorsByType.insert(std::pair(
            TargetMemoryType::FLASH,
            TargetMemoryDescriptor(
                TargetMemoryType::FLASH,
                TargetMemoryAddressRange(
                    flashStartAddress,
                    flashStartAddress + this->targetParameters->flashSize.value() - 1
                ),
                TargetMemoryAccess(true, true, false),
                this->targetParameters->flashPageSize
            )
        ));

        if (this->targetParameters->eepromStartAddress.has_value() && this->targetParameters->eepromSize.has_value()) {
            const auto eepromStartAddress = this->targetParameters->eepromStartAddress.value();

            this->targetMemoryDescriptorsByType.insert(std::pair(
                TargetMemoryType::EEPROM,
                TargetMemoryDescriptor(
                    TargetMemoryType::EEPROM,
                    TargetMemoryAddressRange(
                        eepromStartAddress,
                        eepromStartAddress + this->targetParameters->eepromSize.value() - 1
                    ),
                    TargetMemoryAccess(true, true, true)
                )
            ));
        }
    }

    TargetSignature Avr8::getId() {
        if (!this->id.has_value()) {
            this->id = this->avr8DebugInterface->getDeviceId();
        }

        return this->id.value();
    }

    void Avr8::updateDwenFuseBit(bool enable) {
        if (this->avrIspInterface == nullptr) {
            throw Exception(
                "Debug tool or driver does not provide access to an ISP interface - please confirm that the "
                    "debug tool supports ISP and then report this issue via " + Services::PathService::homeDomainName()
                    + "/report-issue"
            );
        }

        if (!this->targetDescriptionFile.has_value() || !this->id.has_value()) {
            throw Exception(
                "Insufficient target information for ISP interface - do not use the generic \"avr8\" "
                    "target name in conjunction with the ISP interface. Please update your target configuration."
            );
        }

        if (!this->supportedPhysicalInterfaces.contains(PhysicalInterface::DEBUG_WIRE)) {
            throw Exception(
                "Target does not support debugWire physical interface - check target configuration or "
                    "report this issue via " + Services::PathService::homeDomainName() + "/report-issue"
            );
        }

        const auto dwenFuseBitsDescriptor = this->targetDescriptionFile->getDwenFuseBitsDescriptor();
        const auto spienFuseBitsDescriptor = this->targetDescriptionFile->getSpienFuseBitsDescriptor();

        if (!dwenFuseBitsDescriptor.has_value()) {
            throw Exception("Could not find DWEN bit field in TDF.");
        }

        if (!spienFuseBitsDescriptor.has_value()) {
            throw Exception("Could not find SPIEN bit field in TDF.");
        }

        Logger::debug("Extracting ISP parameters from TDF");
        this->avrIspInterface->setIspParameters(this->targetDescriptionFile->getIspParameters());

        Logger::info("Initiating ISP interface");
        this->avrIspInterface->activate();

        /*
         * It is crucial that we understand the potential consequences of this operation.
         *
         * AVR fuses are used to control certain functions within the AVR (including the debugWire interface). Care
         * must be taken when updating these fuse bytes, as an incorrect value could render the AVR inaccessible to
         * standard programmers.
         *
         * For example, consider the SPI enable (SPIEN) fuse bit. This fuse bit is used to enable/disable the SPI for
         * serial programming. If the SPIEN fuse bit is cleared, most programming tools will not be able to gain access
         * to the target via the SPI. This isn't too bad, if there is some other way for the programming tool to gain
         * access (such as the debugWire interface). But now consider the DWEN fuse bit (which is used to enable/disable
         * the debugWire interface). What if both the SPIEN *and* the DWEN fuse bits are cleared? Both interfaces will
         * be disabled. Effectively, the AVR will be bricked, and the only course for recovery would be to use
         * high-voltage programming.
         *
         * When updating the DWEN fuse, Bloom relies on data from the target description file (TDF). But there is no
         * guarantee that this data is correct. For this reason, we perform additional checks in an attempt to reduce
         * the likelihood of bricking the target:
         *
         *  - Confirm target signature match - We read the AVR signature from the connected target and compare it to
         *    what we have in the TDF. The operation will be aborted if the signatures do not match.
         *
         *  - SPIEN fuse bit check - we can be certain that the SPIEN fuse bit is set, because we couldn't have gotten
         *    this far (post ISP activation) if it wasn't. We use this axiom to verify the validity of the data in the
         *    TDF. If the SPIEN fuse bit appears to be cleared, we can be fairly certain that the data we have on the
         *    SPIEN fuse bit is incorrect. From this, we assume that the data for the DWEN fuse bit is also incorrect,
         *    and abort the operation.
         *
         *  - Lock bits check - we read the lock bit byte from the target and confirm that all lock bits are cleared.
         *    If any lock bits are set, we abort the operation.
         *
         *  - DWEN fuse bit check - if the DWEN fuse bit is already set to the desired value, then there is no need
         *    to update it. But we may be checking the wrong bit (if the TDF data is incorrect) - either way, we will
         *    abort the operation.
         *
         * The precautions described above may reduce the likelihood of Bloom bricking the connected target, but there
         * is still a chance that all of the checks pass, and we still brick the device. Now would be a good time to
         * remind the user of liabilities in regard to Bloom and its contributors.
         */
        Logger::warning(
            "Updating the DWEN fuse bit is a potentially dangerous operation. Bloom is provided \"AS IS\", "
                "without warranty of any kind. You are using Bloom at your own risk. In no event shall the copyright "
                "owner or contributors be liable for any damage caused as a result of using Bloom. For more details, "
                "see the Bloom license at " + Services::PathService::homeDomainName() + "/license"
        );

        try {
            Logger::info("Reading target signature via ISP");
            const auto ispDeviceId = this->avrIspInterface->getDeviceId();

            if (ispDeviceId != this->id) {
                throw Exception(
                    "AVR target signature mismatch - expected signature \"" + this->id->toHex()
                        + "\" but got \"" + ispDeviceId.toHex() + "\". Please check target configuration."
                );
            }

            Logger::info("Target signature confirmed: " + ispDeviceId.toHex());

            const auto dwenFuseByte = this->avrIspInterface->readFuse(dwenFuseBitsDescriptor->fuseType).value;
            const auto spienFuseByte = (spienFuseBitsDescriptor->fuseType == dwenFuseBitsDescriptor->fuseType)
                ? dwenFuseByte
                : this->avrIspInterface->readFuse(spienFuseBitsDescriptor->fuseType).value;

            /*
             * Keep in mind that, for AVR fuses and lock bits, a set bit (0b1) means the fuse/lock is cleared, and a
             * cleared bit (0b0), means the fuse/lock is set.
             */

            if ((spienFuseByte & spienFuseBitsDescriptor->bitMask) != 0) {
                /*
                 * If we get here, something is very wrong. The SPIEN (SPI enable) fuse bit appears to be cleared, but
                 * this is not possible because we're connected to the target via the SPI (the ISP interface uses a
                 * physical SPI between the debug tool and the target).
                 *
                 * This could be (and likely is) caused by incorrect data for the SPIEN fuse bit, in the TDF (which was
                 * used to construct the spienFuseBitsDescriptor). And if the data for the SPIEN fuse bit is incorrect,
                 * then what's to say the data for the DWEN fuse bit (dwenFuseBitsDescriptor) is any better?
                 *
                 * We must assume the worst and abort the operation. Otherwise, we risk bricking the user's hardware.
                 */
                throw Exception(
                    "Invalid SPIEN fuse bit value - suspected inaccuracies in TDF data. Please report this to "
                        "Bloom developers as a matter of urgency, via " + Services::PathService::homeDomainName() + "/report-issue"
                );
            }

            Logger::info("Current SPIEN fuse bit value confirmed");

            if (!static_cast<bool>(dwenFuseByte & dwenFuseBitsDescriptor->bitMask) == enable) {
                /*
                 * The DWEN fuse appears to already be set to the desired value. This may be a result of incorrect data
                 * in the TDF, but we're not taking any chances.
                 *
                 * We don't throw an exception here, because we don't know if this is due to an error, or if the fuse
                 * bit is simply already set to the desired value.
                 */
                Logger::debug("DWEN fuse bit already set to desired value - aborting update operation");

                this->avrIspInterface->deactivate();
                return;
            }

            const auto lockBitByte = this->avrIspInterface->readLockBitByte();
            if (lockBitByte != 0xFF) {
                /*
                 * There is at least one lock bit that is set. Setting the DWEN fuse bit with the lock bits set may
                 * brick the device. We must abort.
                 */
                throw Exception(
                    "At least one lock bit has been set - updating the DWEN fuse bit could potentially brick "
                        "the target."
                );
            }

            Logger::info("Cleared lock bits confirmed");

            const auto newFuse = Fuse(
                dwenFuseBitsDescriptor->fuseType,
                (enable) ? static_cast<unsigned char>(dwenFuseByte & ~(dwenFuseBitsDescriptor->bitMask))
                    : static_cast<unsigned char>(dwenFuseByte | dwenFuseBitsDescriptor->bitMask)
            );

            Logger::warning("Updating DWEN fuse bit");
            this->avrIspInterface->programFuse(newFuse);

            Logger::debug("Verifying DWEN fuse bit");
            if (this->avrIspInterface->readFuse(dwenFuseBitsDescriptor->fuseType).value != newFuse.value) {
                throw Exception("Failed to update DWEN fuse bit - post-update verification failed");
            }

            Logger::info("DWEN fuse bit successfully updated");

            this->avrIspInterface->deactivate();

        } catch (const Exception& exception) {
            this->avrIspInterface->deactivate();
            throw exception;
        }
    }

    void Avr8::updateOcdenFuseBit(bool enable) {
        using Services::PathService;
        using Services::StringService;

        if (!this->targetDescriptionFile.has_value() || !this->id.has_value()) {
            throw Exception(
                "Insufficient target information for managing OCDEN fuse bit - do not use the generic \"avr8\" "
                    "target name in conjunction with the \"manageOcdenFuseBit\" function. Please update your target "
                    "configuration."
            );
        }

        if (!this->supportedPhysicalInterfaces.contains(PhysicalInterface::JTAG)) {
            throw Exception(
                "Target does not support JTAG physical interface - check target configuration or "
                    "report this issue via " + PathService::homeDomainName() + "/report-issue"
            );
        }

        const auto targetSignature = this->avr8DebugInterface->getDeviceId();
        const auto tdSignature = this->targetDescriptionFile->getTargetSignature();

        if (targetSignature != tdSignature) {
            throw Exception(
                "Failed to validate connected target - target signature mismatch.\nThe target signature"
                    " (\"" + targetSignature.toHex() + "\") does not match the AVR8 target description signature (\""
                    + tdSignature.toHex() + "\"). This will likely be due to an incorrect target name in the "
                    + "configuration file (bloom.yaml)."
            );
        }

        const auto ocdenFuseBitsDescriptor = this->targetDescriptionFile->getOcdenFuseBitsDescriptor();
        const auto jtagenFuseBitsDescriptor = this->targetDescriptionFile->getJtagenFuseBitsDescriptor();

        if (!ocdenFuseBitsDescriptor.has_value()) {
            throw Exception("Could not find OCDEN bit field in TDF.");
        }

        if (!jtagenFuseBitsDescriptor.has_value()) {
            throw Exception("Could not find JTAGEN bit field in TDF.");
        }

        try {
            this->enableProgrammingMode();

            const auto ocdenFuseByteValue = this->avr8DebugInterface->readMemory(
                TargetMemoryType::FUSES,
                ocdenFuseBitsDescriptor->byteAddress,
                1
            ).at(0);
            const auto jtagenFuseByteValue = jtagenFuseBitsDescriptor->byteAddress == ocdenFuseBitsDescriptor->byteAddress
                ? ocdenFuseByteValue
                : this->avr8DebugInterface->readMemory(
                    TargetMemoryType::FUSES,
                    jtagenFuseBitsDescriptor->byteAddress,
                    1
                ).at(0)
            ;

            Logger::debug("OCDEN fuse byte value (before update): 0x" + StringService::toHex(ocdenFuseByteValue));

            if ((jtagenFuseByteValue & jtagenFuseBitsDescriptor->bitMask) != 0) {
                /*
                 * If we get here, something has gone wrong. The JTAGEN fuse should always be programmed by this point.
                 * We wouldn't have been able to activate the JTAG physical interface if the fuse wasn't programmed.
                 *
                 * This means the data we have on the JTAGEN fuse bit, from the TDF, is likely incorrect. And if that's
                 * the case, we cannot rely on the data for the OCDEN fuse bit being any better.
                 */
                throw Exception(
                    "Invalid JTAGEN fuse bit value - suspected inaccuracies in TDF data. Please report this to "
                    "Bloom developers as a matter of urgency, via " + PathService::homeDomainName() + "/report-issue"
                );
            }

            if (!static_cast<bool>(ocdenFuseByteValue & ocdenFuseBitsDescriptor->bitMask) == enable) {
                Logger::debug("OCDEN fuse bit already set to desired value - aborting update operation");

                this->disableProgrammingMode();
                return;
            }

            const auto newValue = (enable)
                ? static_cast<unsigned char>(ocdenFuseByteValue & ~(ocdenFuseBitsDescriptor->bitMask))
                : static_cast<unsigned char>(ocdenFuseByteValue | ocdenFuseBitsDescriptor->bitMask);

            Logger::debug("New OCDEN fuse byte value (to be written): 0x" + StringService::toHex(newValue));

            Logger::warning("Updating OCDEN fuse bit");
            this->avr8DebugInterface->writeMemory(
                TargetMemoryType::FUSES,
                ocdenFuseBitsDescriptor->byteAddress,
                {newValue}
            );

            Logger::debug("Verifying OCDEN fuse bit");
            const auto postUpdateOcdenByteValue = this->avr8DebugInterface->readMemory(
                TargetMemoryType::FUSES,
                ocdenFuseBitsDescriptor->byteAddress,
                1
            ).at(0);

            if (postUpdateOcdenByteValue != newValue) {
                throw Exception("Failed to update OCDEN fuse bit - post-update verification failed");
            }

            Logger::info("OCDEN fuse bit updated");

            this->disableProgrammingMode();

        } catch (const Exception& exception) {
            this->disableProgrammingMode();
            throw exception;
        }
    }

    ProgramMemorySection Avr8::getProgramMemorySectionFromAddress(std::uint32_t address) {
        return this->targetParameters->bootSectionStartAddress.has_value()
            && address >= this->targetParameters->bootSectionStartAddress.value()
            ? ProgramMemorySection::BOOT
            : ProgramMemorySection::APPLICATION;
    }
}
