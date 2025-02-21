#include "TargetDescriptionFile.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "src/Services/PathService.hpp"
#include "src/Logger/Logger.hpp"

#include "src/Exceptions/Exception.hpp"
#include "src/Targets/TargetDescription/Exceptions/TargetDescriptionParsingFailureException.hpp"

namespace Bloom::Targets::Microchip::Avr::Avr8Bit::TargetDescription
{
    using namespace Bloom::Exceptions;

    using Bloom::Targets::TargetDescription::RegisterGroup;
    using Bloom::Targets::TargetDescription::AddressSpace;
    using Bloom::Targets::TargetDescription::MemorySegment;
    using Bloom::Targets::TargetDescription::MemorySegmentType;
    using Bloom::Targets::TargetDescription::Register;
    using Bloom::Targets::TargetVariant;
    using Bloom::Targets::TargetRegisterDescriptor;

    TargetDescriptionFile::TargetDescriptionFile(
        const TargetSignature& targetSignature,
        std::optional<std::string> targetName
    ) {
        const auto targetSignatureHex = targetSignature.toHex();
        const auto mapping = TargetDescriptionFile::getTargetDescriptionMapping();
        const auto descriptionFiles = mapping.find(QString::fromStdString(targetSignatureHex).toLower())->toArray();

        if (descriptionFiles.empty()) {
            throw Exception(
                "Failed to resolve target description file for target \"" + targetSignatureHex
                    + "\" - unknown target signature."
            );
        }

        if (descriptionFiles.size() > 1 && !targetName.has_value()) {
            /*
             * There are numerous target description files mapped to this target signature and we don't have a target
             * name to filter by. There's really not much we can do at this point, so we'll just instruct the user to
             * provide a specific target name.
             */
            auto targetNames = QStringList();
            std::transform(
                descriptionFiles.begin(),
                descriptionFiles.end(),
                std::back_inserter(targetNames),
                [] (const QJsonValue& descriptionFile) {
                    return QString(
                        "\"" + descriptionFile.toObject().find("targetName")->toString().toLower() + "\""
                    );
                }
            );

            throw Exception(
                "Failed to resolve target description file for target \"" + targetSignatureHex
                    + "\" - ambiguous signature.\nThe signature is mapped to numerous targets: "
                    + targetNames.join(", ").toStdString() + ".\n\nPlease update the target name in your Bloom "
                    + "configuration file, to one of the above."
            );
        }

        for (const auto& mappingJsonValue : descriptionFiles) {
            const auto mappingObject = mappingJsonValue.toObject();

            if (
                targetName.has_value()
                && *targetName != mappingObject.find("targetName")->toString().toLower().toStdString()
            ) {
                continue;
            }

            const auto descriptionFilePath = QString::fromStdString(Services::PathService::applicationDirPath()) + "/"
                + mappingObject.find("targetDescriptionFilePath")->toString();

            Logger::debug("Loading AVR8 target description file: " + descriptionFilePath.toStdString());
            Targets::TargetDescription::TargetDescriptionFile::init(descriptionFilePath);
            return;
        }

        throw Exception(
            "Failed to resolve target description file for target \"" + *targetName
                + "\" - target signature \"" + targetSignatureHex + "\" does not belong to target with name \""
                + *targetName + "\". Please review your bloom.yaml configuration."
        );
    }

    void TargetDescriptionFile::init(const QDomDocument& xml) {
        Targets::TargetDescription::TargetDescriptionFile::init(xml);

        this->loadSupportedPhysicalInterfaces();
        this->loadPadDescriptors();
        this->loadTargetVariants();
        this->loadTargetRegisterDescriptors();
    }

    QJsonObject TargetDescriptionFile::getTargetDescriptionMapping() {
        auto mappingFile = QFile(
            QString::fromStdString(Services::PathService::resourcesDirPath() + "/TargetDescriptionFiles/AVR/Mapping.json")
        );

        if (!mappingFile.exists()) {
            throw Exception("Failed to load AVR target description mapping - mapping file not found");
        }

        mappingFile.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(mappingFile.readAll()).object();
    }

    TargetSignature TargetDescriptionFile::getTargetSignature() const {
        const auto& propertyGroups = this->propertyGroupsMappedByName;

        const auto signaturePropertyGroupIt = propertyGroups.find("signatures");
        if (signaturePropertyGroupIt == propertyGroups.end()) {
            throw TargetDescriptionParsingFailureException("Signature property group not found");
        }

        const auto& signatureProperties = signaturePropertyGroupIt->second.propertiesMappedByName;
        std::optional<unsigned char> signatureByteZero;
        std::optional<unsigned char> signatureByteOne;
        std::optional<unsigned char> signatureByteTwo;

        const auto signatureZeroIt = signatureProperties.find("signature0");
        if (signatureZeroIt != signatureProperties.end()) {
            signatureByteZero = static_cast<unsigned char>(signatureZeroIt->second.value.toShort(nullptr, 16));
        }

        const auto signatureOneIt = signatureProperties.find("signature1");
        if (signatureOneIt != signatureProperties.end()) {
            signatureByteOne = static_cast<unsigned char>(signatureOneIt->second.value.toShort(nullptr, 16));
        }

        const auto signatureTwoIt = signatureProperties.find("signature2");
        if (signatureTwoIt != signatureProperties.end()) {
            signatureByteTwo = static_cast<unsigned char>(signatureTwoIt->second.value.toShort(nullptr, 16));
        }

        if (signatureByteZero.has_value() && signatureByteOne.has_value() && signatureByteTwo.has_value()) {
            return TargetSignature(signatureByteZero.value(), signatureByteOne.value(), signatureByteTwo.value());
        }

        throw TargetDescriptionParsingFailureException(
            "Failed to extract target signature from AVR8 target description."
        );
    }

    Family TargetDescriptionFile::getFamily() const {
        static const auto targetFamiliesByName = TargetDescriptionFile::getFamilyNameToEnumMapping();

        const auto& familyName = this->getFamilyName();

        if (familyName.empty()) {
            throw Exception("Could not find target family name in target description file.");
        }

        const auto familyIt = targetFamiliesByName.find(familyName);

        if (familyIt == targetFamiliesByName.end()) {
            throw Exception("Unknown family name in target description file.");
        }

        return familyIt->second;
    }

    TargetParameters TargetDescriptionFile::getTargetParameters() const {
        TargetParameters targetParameters;

        const auto& peripheralModules = this->getPeripheralModulesMappedByName();
        const auto& propertyGroups = this->getPropertyGroupsMappedByName();

        const auto programMemoryAddressSpace = this->getProgramMemoryAddressSpace();

        if (programMemoryAddressSpace.has_value()) {
            targetParameters.flashSize = programMemoryAddressSpace->size;
            targetParameters.flashStartAddress = programMemoryAddressSpace->startAddress;

            const auto appMemorySegment = this->getFlashApplicationMemorySegment(programMemoryAddressSpace.value());

            if (appMemorySegment.has_value()) {
                targetParameters.appSectionStartAddress = appMemorySegment->startAddress;
                targetParameters.appSectionSize = appMemorySegment->size;
                targetParameters.flashPageSize = appMemorySegment->pageSize;
            }
        }

        const auto ramMemorySegment = this->getRamMemorySegment();
        if (ramMemorySegment.has_value()) {
            targetParameters.ramSize = ramMemorySegment->size;
            targetParameters.ramStartAddress = ramMemorySegment->startAddress;
        }

        const auto ioMemorySegment = this->getIoMemorySegment();
        if (ioMemorySegment.has_value()) {
            targetParameters.mappedIoSegmentSize = ioMemorySegment->size;
            targetParameters.mappedIoSegmentStartAddress = ioMemorySegment->startAddress;
        }

        const auto registerMemorySegment = this->getRegisterMemorySegment();
        if (registerMemorySegment.has_value()) {
            targetParameters.gpRegisterSize = registerMemorySegment->size;
            targetParameters.gpRegisterStartAddress = registerMemorySegment->startAddress;
        }

        const auto eepromMemorySegment = this->getEepromMemorySegment();
        if (eepromMemorySegment.has_value()) {
            targetParameters.eepromSize = eepromMemorySegment->size;
            targetParameters.eepromStartAddress = eepromMemorySegment->startAddress;

            if (eepromMemorySegment->pageSize.has_value()) {
                targetParameters.eepromPageSize = eepromMemorySegment->pageSize.value();
            }
        }

        const auto firstBootSectionMemorySegment = this->getFirstBootSectionMemorySegment();
        if (firstBootSectionMemorySegment.has_value()) {
            targetParameters.bootSectionStartAddress = firstBootSectionMemorySegment->startAddress / 2;
            targetParameters.bootSectionSize = firstBootSectionMemorySegment->size;
        }

        std::uint32_t cpuRegistersOffset = 0;

        const auto cpuPeripheralModuleIt = peripheralModules.find("cpu");
        if (cpuPeripheralModuleIt != peripheralModules.end()) {
            const auto& cpuPeripheralModule = cpuPeripheralModuleIt->second;

            const auto cpuInstanceIt = cpuPeripheralModule.instancesMappedByName.find("cpu");
            if (cpuInstanceIt != cpuPeripheralModule.instancesMappedByName.end()) {
                const auto& cpuInstance = cpuInstanceIt->second;

                const auto cpuRegisterGroupIt = cpuInstance.registerGroupsMappedByName.find("cpu");
                if (cpuRegisterGroupIt != cpuInstance.registerGroupsMappedByName.end()) {
                    cpuRegistersOffset = cpuRegisterGroupIt->second.offset.value_or(0);
                }
            }
        }

        const auto statusRegister = this->getStatusRegister();
        if (statusRegister.has_value()) {
            targetParameters.statusRegisterStartAddress = cpuRegistersOffset + statusRegister->offset;
            targetParameters.statusRegisterSize = statusRegister->size;
        }

        const auto stackPointerRegister = this->getStackPointerRegister();
        if (stackPointerRegister.has_value()) {
            targetParameters.stackPointerRegisterLowAddress = cpuRegistersOffset + stackPointerRegister->offset;
            targetParameters.stackPointerRegisterSize = stackPointerRegister->size;

        } else {
            // Sometimes the SP register is split into two register nodes, one for low, the other for high
            const auto stackPointerLowRegister = this->getStackPointerLowRegister();
            const auto stackPointerHighRegister = this->getStackPointerHighRegister();

            if (stackPointerLowRegister.has_value()) {
                targetParameters.stackPointerRegisterLowAddress = cpuRegistersOffset
                    + stackPointerLowRegister->offset;
                targetParameters.stackPointerRegisterSize = stackPointerLowRegister->size;
            }

            if (stackPointerHighRegister.has_value()) {
                targetParameters.stackPointerRegisterSize =
                    targetParameters.stackPointerRegisterSize.has_value() ?
                    targetParameters.stackPointerRegisterSize.value() + stackPointerHighRegister->size :
                    stackPointerHighRegister->size;
            }
        }

        const auto& supportedPhysicalInterfaces = this->getSupportedPhysicalInterfaces();

        if (
            supportedPhysicalInterfaces.contains(PhysicalInterface::DEBUG_WIRE)
            || supportedPhysicalInterfaces.contains(PhysicalInterface::JTAG)
        ) {
            this->loadDebugWireAndJtagTargetParameters(targetParameters);
        }

        if (supportedPhysicalInterfaces.contains(PhysicalInterface::PDI)) {
            this->loadPdiTargetParameters(targetParameters);
        }

        if (supportedPhysicalInterfaces.contains(PhysicalInterface::UPDI)) {
            this->loadUpdiTargetParameters(targetParameters);
        }

        return targetParameters;
    }

    IspParameters TargetDescriptionFile::getIspParameters() const {
        if (!this->propertyGroupsMappedByName.contains("isp_interface")) {
            throw Exception("TDF missing ISP parameters");
        }

        const auto& ispParameterPropertiesByName = this->propertyGroupsMappedByName.at(
            "isp_interface"
        ).propertiesMappedByName;

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_timeout")) {
            throw Exception("TDF missing ISP programming mode timeout property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_stabdelay")) {
            throw Exception("TDF missing ISP programming mode stabilization delay property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_cmdexedelay")) {
            throw Exception("TDF missing ISP programming mode command execution delay property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_synchloops")) {
            throw Exception("TDF missing ISP programming mode sync loops property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_bytedelay")) {
            throw Exception("TDF missing ISP programming mode byte delay property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_pollindex")) {
            throw Exception("TDF missing ISP programming mode poll index property");
        }

        if (!ispParameterPropertiesByName.contains("ispenterprogmode_pollvalue")) {
            throw Exception("TDF missing ISP programming mode poll value property");
        }

        if (!ispParameterPropertiesByName.contains("ispleaveprogmode_predelay")) {
            throw Exception("TDF missing ISP programming mode pre-delay property");
        }

        if (!ispParameterPropertiesByName.contains("ispleaveprogmode_postdelay")) {
            throw Exception("TDF missing ISP programming mode post-delay property");
        }

        if (!ispParameterPropertiesByName.contains("ispreadsign_pollindex")) {
            throw Exception("TDF missing ISP read signature poll index property");
        }

        if (!ispParameterPropertiesByName.contains("ispreadfuse_pollindex")) {
            throw Exception("TDF missing ISP read fuse poll index property");
        }

        if (!ispParameterPropertiesByName.contains("ispreadlock_pollindex")) {
            throw Exception("TDF missing ISP read lock poll index property");
        }

        auto output = IspParameters();

        output.programModeTimeout = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_timeout").value.toUShort()
        );
        output.programModeStabilizationDelay = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_stabdelay").value.toUShort()
        );
        output.programModeCommandExecutionDelay = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_cmdexedelay").value.toUShort()
        );
        output.programModeSyncLoops = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_synchloops").value.toUShort()
        );
        output.programModeByteDelay = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_bytedelay").value.toUShort()
        );
        output.programModePollValue = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_pollvalue").value.toUShort(nullptr, 16)
        );
        output.programModePollIndex = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispenterprogmode_pollindex").value.toUShort()
        );
        output.programModePreDelay = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispleaveprogmode_predelay").value.toUShort()
        );
        output.programModePostDelay = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispleaveprogmode_postdelay").value.toUShort()
        );
        output.readSignaturePollIndex = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispreadsign_pollindex").value.toUShort()
        );
        output.readFusePollIndex = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispreadfuse_pollindex").value.toUShort()
        );
        output.readLockPollIndex = static_cast<std::uint8_t>(
            ispParameterPropertiesByName.at("ispreadlock_pollindex").value.toUShort()
        );

        return output;
    }

    std::optional<FuseBitsDescriptor> TargetDescriptionFile::getDwenFuseBitsDescriptor() const {
        return this->getFuseBitsDescriptorByName("dwen");
    }

    std::optional<FuseBitsDescriptor> TargetDescriptionFile::getSpienFuseBitsDescriptor() const {
        return this->getFuseBitsDescriptorByName("spien");
    }

    std::optional<FuseBitsDescriptor> TargetDescriptionFile::getOcdenFuseBitsDescriptor() const {
        return this->getFuseBitsDescriptorByName("ocden");
    }

    std::optional<FuseBitsDescriptor> TargetDescriptionFile::getJtagenFuseBitsDescriptor() const {
        return this->getFuseBitsDescriptorByName("jtagen");
    }

    void TargetDescriptionFile::loadSupportedPhysicalInterfaces() {
        auto interfaceNamesToInterfaces = std::map<std::string, PhysicalInterface>({
           {"updi", PhysicalInterface::UPDI},
           {"debugwire", PhysicalInterface::DEBUG_WIRE},
           {"jtag", PhysicalInterface::JTAG},
           {"pdi", PhysicalInterface::PDI},
           {"isp", PhysicalInterface::ISP},
        });

        for (const auto& [interfaceName, interface]: this->interfacesByName) {
            const auto interfaceIt = interfaceNamesToInterfaces.find(interfaceName);
            if (interfaceIt != interfaceNamesToInterfaces.end()) {
                this->supportedPhysicalInterfaces.insert(interfaceIt->second);
            }
        }
    }

    void TargetDescriptionFile::loadPadDescriptors() {
        const auto& modules = this->getModulesMappedByName();

        const auto portModuleIt = modules.find("port");
        const auto portModule = (portModuleIt != modules.end()) ? std::optional(portModuleIt->second) : std::nullopt;

        const auto& peripheralModules = this->getPeripheralModulesMappedByName();

        const auto portPeripheralModuleIt = peripheralModules.find("port");
        if (portPeripheralModuleIt == peripheralModules.end()) {
            return;
        }

        const auto& portPeripheralModule = portPeripheralModuleIt->second;

        for (const auto& [instanceName, instance] : portPeripheralModule.instancesMappedByName) {
            if (instanceName.find("port") != 0) {
                continue;
            }

            const auto portPeripheralRegisterGroupIt = portPeripheralModule.registerGroupsMappedByName.find(
                instanceName
            );

            const auto portPeripheralRegisterGroup =
                portPeripheralRegisterGroupIt != portPeripheralModule.registerGroupsMappedByName.end()
                ? std::optional(portPeripheralRegisterGroupIt->second)
                : std::nullopt;

            for (const auto& signal : instance.instanceSignals) {
                if (!signal.index.has_value()) {
                    continue;
                }

                auto& padDescriptor = this->padDescriptorsByName.insert(
                    std::pair(signal.padName, PadDescriptor())
                ).first->second;

                padDescriptor.name = signal.padName;
                padDescriptor.gpioPinNumber = signal.index.value();

                if (!portModule.has_value()) {
                    continue;
                }

                const auto instanceRegisterGroupIt = portModule->registerGroupsMappedByName.find(instanceName);
                if (instanceRegisterGroupIt != portModule->registerGroupsMappedByName.end()) {
                    // We have register information for this port
                    const auto& registerGroup = instanceRegisterGroupIt->second;

                    for (const auto& [registerName, portRegister] : registerGroup.registersMappedByName) {
                        if (registerName.find("port") == 0) {
                            // This is the data register for the port
                            padDescriptor.gpioPortAddress = portRegister.offset;
                            continue;
                        }

                        if (registerName.find("pin") == 0) {
                            // This is the input data register for the port
                            padDescriptor.gpioPortInputAddress = portRegister.offset;
                            continue;
                        }

                        if (registerName.find("ddr") == 0) {
                            // This is the data direction register for the port
                            padDescriptor.gpioDdrAddress = portRegister.offset;
                            continue;
                        }
                    }

                    continue;
                }

                const auto portRegisterGroupIt = portModule->registerGroupsMappedByName.find("port");
                if (portRegisterGroupIt != portModule->registerGroupsMappedByName.end()) {
                    // We have generic register information for all ports on the target
                    const auto& registerGroup = portRegisterGroupIt->second;

                    for (const auto& [registerName, portRegister] : registerGroup.registersMappedByName) {
                        if (registerName == "out") {
                            // Include the port register offset
                            padDescriptor.gpioPortAddress = (
                                portPeripheralRegisterGroup.has_value()
                                && portPeripheralRegisterGroup->offset.has_value()
                            )
                                ? portPeripheralRegisterGroup->offset.value_or(0) + portRegister.offset
                                : 0 + portRegister.offset;
                            continue;
                        }

                        if (registerName == "dir") {
                            padDescriptor.gpioDdrAddress = (
                                portPeripheralRegisterGroup.has_value()
                                    && portPeripheralRegisterGroup->offset.has_value()
                            )
                                ? portPeripheralRegisterGroup->offset.value_or(0) + portRegister.offset
                                : 0 + portRegister.offset;
                            continue;
                        }

                        if (registerName == "in") {
                            padDescriptor.gpioPortInputAddress = (
                                portPeripheralRegisterGroup.has_value()
                                    && portPeripheralRegisterGroup->offset.has_value()
                            )
                                ? portPeripheralRegisterGroup->offset.value_or(0) + portRegister.offset
                                : 0 + portRegister.offset;
                            continue;
                        }
                    }

                    continue;
                }
            }
        }
    }

    void TargetDescriptionFile::loadTargetVariants() {
        auto tdVariants = this->getVariants();
        auto tdPinoutsByName = this->getPinoutsMappedByName();
        const auto& modules = this->getModulesMappedByName();

        for (const auto& tdVariant : tdVariants) {
            if (tdVariant.disabled) {
                continue;
            }

            auto targetVariant = TargetVariant();
            targetVariant.id = static_cast<int>(this->targetVariantsById.size());
            targetVariant.name = tdVariant.name;
            targetVariant.packageName = tdVariant.package;

            if (tdVariant.package.find("QFP") == 0 || tdVariant.package.find("TQFP") == 0) {
                targetVariant.package = TargetPackage::QFP;

            } else if (tdVariant.package.find("PDIP") == 0 || tdVariant.package.find("DIP") == 0) {
                targetVariant.package = TargetPackage::DIP;

            } else if (tdVariant.package.find("QFN") == 0 || tdVariant.package.find("VQFN") == 0) {
                targetVariant.package = TargetPackage::QFN;

            } else if (tdVariant.package.find("SOIC") == 0) {
                targetVariant.package = TargetPackage::SOIC;

            } else if (tdVariant.package.find("SSOP") == 0) {
                targetVariant.package = TargetPackage::SSOP;
            }

            const auto tdPinoutIt = tdPinoutsByName.find(tdVariant.pinoutName);
            if (tdPinoutIt == tdPinoutsByName.end()) {
                // Missing pinouts in the target description file
                continue;
            }

            const auto& tdPinout = tdPinoutIt->second;
            for (const auto& tdPin : tdPinout.pins) {
                auto targetPin = TargetPinDescriptor();
                targetPin.name = tdPin.pad;
                targetPin.padName = tdPin.pad;
                targetPin.number = tdPin.position;
                targetPin.variantId = targetVariant.id;

                // TODO: REMOVE THIS:
                if (
                    tdPin.pad.find("vcc") == 0
                    || tdPin.pad.find("avcc") == 0
                    || tdPin.pad.find("aref") == 0
                    || tdPin.pad.find("avdd") == 0
                    || tdPin.pad.find("vdd") == 0
                ) {
                    targetPin.type = TargetPinType::VCC;

                } else if (tdPin.pad.find("gnd") == 0) {
                    targetPin.type = TargetPinType::GND;
                }

                const auto padIt = this->padDescriptorsByName.find(targetPin.padName);
                if (padIt != this->padDescriptorsByName.end()) {
                    const auto& pad = padIt->second;
                    if (pad.gpioPortAddress.has_value() && pad.gpioDdrAddress.has_value()) {
                        targetPin.type = TargetPinType::GPIO;
                    }
                }

                targetVariant.pinDescriptorsByNumber.insert(std::pair(targetPin.number, targetPin));
            }

            this->targetVariantsById.insert(std::pair(targetVariant.id, targetVariant));
        }
    }

    void TargetDescriptionFile::loadTargetRegisterDescriptors() {
        const auto& modulesByName = this->modulesMappedByName;
        const auto& peripheralModulesByName = this->peripheralModulesMappedByName;

        for (const auto& [moduleName, module] : modulesByName) {
            for (const auto& [registerGroupName, registerGroup] : module.registerGroupsMappedByName) {
                const auto peripheralRegisterGroupsIt = this->peripheralRegisterGroupsMappedByModuleRegisterGroupName.find(
                    registerGroupName
                );

                if (peripheralRegisterGroupsIt != this->peripheralRegisterGroupsMappedByModuleRegisterGroupName.end()) {
                    const auto& peripheralRegisterGroups = peripheralRegisterGroupsIt->second;

                    for (const auto& peripheralRegisterGroup : peripheralRegisterGroups) {
                        if (peripheralRegisterGroup.addressSpaceId.value_or("") != "data") {
                            // Currently, we only deal with registers in the data address space.
                            continue;
                        }

                        for (const auto& [moduleRegisterName, moduleRegister] : registerGroup.registersMappedByName) {
                            if (moduleRegister.size < 1) {
                                continue;
                            }

                            auto registerDescriptor = TargetRegisterDescriptor();
                            registerDescriptor.type = moduleName == "port"
                                ? TargetRegisterType::PORT_REGISTER : TargetRegisterType::OTHER;
                            registerDescriptor.memoryType = TargetMemoryType::RAM;
                            registerDescriptor.name = moduleRegisterName;
                            registerDescriptor.groupName = peripheralRegisterGroup.name;
                            registerDescriptor.size = moduleRegister.size;
                            registerDescriptor.startAddress = moduleRegister.offset
                                + peripheralRegisterGroup.offset.value_or(0);

                            if (moduleRegister.caption.has_value() && !moduleRegister.caption->empty()) {
                                registerDescriptor.description = moduleRegister.caption;
                            }

                            if (moduleRegister.readWriteAccess.has_value()) {
                                const auto& readWriteAccess = moduleRegister.readWriteAccess.value();
                                registerDescriptor.readable = readWriteAccess.find('r') != std::string::npos;
                                registerDescriptor.writable = readWriteAccess.find('w') != std::string::npos;

                            } else {
                                /*
                                 * If the TDF doesn't specify the OCD read/write access for a register, we assume both
                                 * are permitted.
                                 */
                                registerDescriptor.readable = true;
                                registerDescriptor.writable = true;
                            }

                            this->targetRegisterDescriptorsByType[registerDescriptor.type].insert(registerDescriptor);
                        }
                    }
                }
            }
        }
    }

    std::optional<FuseBitsDescriptor> TargetDescriptionFile::getFuseBitsDescriptorByName(
        const std::string& fuseBitName
    ) const {
        const auto& peripheralModules = this->getPeripheralModulesMappedByName();
        std::uint32_t fuseAddressOffset = 0;

        const auto fusePeripheralModuleIt = peripheralModules.find("fuse");
        if (fusePeripheralModuleIt != peripheralModules.end()) {
            const auto& fusePeripheralModule = fusePeripheralModuleIt->second;

            const auto fuseInstanceIt = fusePeripheralModule.instancesMappedByName.find("fuse");
            if (fuseInstanceIt != fusePeripheralModule.instancesMappedByName.end()) {
                const auto& fuseInstance = fuseInstanceIt->second;

                const auto fuseRegisterGroupIt = fuseInstance.registerGroupsMappedByName.find("fuse");
                if (fuseRegisterGroupIt != fuseInstance.registerGroupsMappedByName.end()) {
                    fuseAddressOffset = fuseRegisterGroupIt->second.offset.value_or(0);
                }
            }
        }

        const auto fuseModuleIt = this->modulesMappedByName.find("fuse");

        if (fuseModuleIt == this->modulesMappedByName.end()) {
            return std::nullopt;
        }

        const auto& fuseModule = fuseModuleIt->second;

        const auto fuseRegisterGroupIt = fuseModule.registerGroupsMappedByName.find("fuse");

        if (fuseRegisterGroupIt == fuseModule.registerGroupsMappedByName.end()) {
            return std::nullopt;
        }

        const auto& fuseRegisterGroup = fuseRegisterGroupIt->second;

        static const auto fuseTypesByName = std::map<std::string, FuseType>({
            {"low", FuseType::LOW},
            {"high", FuseType::HIGH},
            {"extended", FuseType::EXTENDED},
        });


        for (const auto& [fuseTypeName, fuse] : fuseRegisterGroup.registersMappedByName) {
            const auto fuseTypeIt = fuseTypesByName.find(fuseTypeName);
            if (fuseTypeIt == fuseTypesByName.end()) {
                // Unknown fuse type name
                continue;
            }

            const auto fuseBitFieldIt = fuse.bitFieldsMappedByName.find(fuseBitName);

            if (fuseBitFieldIt != fuse.bitFieldsMappedByName.end()) {
                return FuseBitsDescriptor(
                    fuseAddressOffset + fuse.offset,
                    fuseTypeIt->second,
                    fuseBitFieldIt->second.mask
                );
            }
        }

        return std::nullopt;
    }

    std::optional<AddressSpace> TargetDescriptionFile::getProgramMemoryAddressSpace() const {
        const auto programAddressSpaceIt = this->addressSpacesMappedById.find("prog");

        if (programAddressSpaceIt != this->addressSpacesMappedById.end()) {
            return programAddressSpaceIt->second;
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getFlashApplicationMemorySegment(
        const AddressSpace& programAddressSpace
    ) const {
        const auto& programMemorySegments = programAddressSpace.memorySegmentsByTypeAndName;

        const auto flashMemorySegmentsIt = programMemorySegments.find(MemorySegmentType::FLASH);
        if (flashMemorySegmentsIt != programMemorySegments.end()) {
            const auto& flashMemorySegments = flashMemorySegmentsIt->second;

            /*
             * In AVR8 TDFs, flash application memory segments are typically named "APP_SECTION", "PROGMEM" or
             * "FLASH".
             */
            const auto appSectionSegmentIt = flashMemorySegments.find("app_section");
            if (appSectionSegmentIt != flashMemorySegments.end()) {
                return appSectionSegmentIt->second;
            }

            const auto programMemSegmentIt = flashMemorySegments.find("progmem");
            if (programMemSegmentIt != flashMemorySegments.end()) {
                return programMemSegmentIt->second;
            }

            const auto flashSegmentIt = flashMemorySegments.find("flash");
            if (flashSegmentIt != flashMemorySegments.end()) {
                return flashSegmentIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getRamMemorySegment() const {
        const auto& addressMapping = this->addressSpacesMappedById;

        // Internal RAM  &register attributes are usually found in the data address space
        auto dataAddressSpaceIt = addressMapping.find("data");

        if (dataAddressSpaceIt != addressMapping.end()) {
            const auto& dataAddressSpace = dataAddressSpaceIt->second;
            const auto& dataMemorySegments = dataAddressSpace.memorySegmentsByTypeAndName;

            if (dataMemorySegments.find(MemorySegmentType::RAM) != dataMemorySegments.end()) {
                const auto& ramMemorySegments = dataMemorySegments.find(MemorySegmentType::RAM)->second;
                auto ramMemorySegmentIt = ramMemorySegments.begin();

                if (ramMemorySegmentIt != ramMemorySegments.end()) {
                    return ramMemorySegmentIt->second;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getIoMemorySegment() const {
        const auto dataAddressMappingIt = this->addressSpacesMappedById.find("data");

        if (dataAddressMappingIt != this->addressSpacesMappedById.end()) {
            const auto& dataAddressSpace = dataAddressMappingIt->second;
            const auto ioMemorySegmentsIt = dataAddressSpace.memorySegmentsByTypeAndName.find(MemorySegmentType::IO);

            if (ioMemorySegmentsIt != dataAddressSpace.memorySegmentsByTypeAndName.end()) {
                const auto& ramMemorySegments = ioMemorySegmentsIt->second;
                const auto ramMemorySegmentIt = ramMemorySegments.begin();

                if (ramMemorySegmentIt != ramMemorySegments.end()) {
                    return ramMemorySegmentIt->second;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getRegisterMemorySegment() const {
        const auto& addressMapping = this->addressSpacesMappedById;

        // Internal RAM  &register attributes are usually found in the data address space
        auto dataAddressSpaceIt = addressMapping.find("data");

        if (dataAddressSpaceIt != addressMapping.end()) {
            const auto& dataAddressSpace = dataAddressSpaceIt->second;
            const auto& dataMemorySegments = dataAddressSpace.memorySegmentsByTypeAndName;

            if (dataMemorySegments.find(MemorySegmentType::REGISTERS) != dataMemorySegments.end()) {
                const auto& registerMemorySegments = dataMemorySegments.find(MemorySegmentType::REGISTERS)->second;
                auto registerMemorySegmentIt = registerMemorySegments.begin();

                if (registerMemorySegmentIt != registerMemorySegments.end()) {
                    return registerMemorySegmentIt->second;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getEepromMemorySegment() const {
        const auto eepromAddressSpaceIt = this->addressSpacesMappedById.find("eeprom");

        if (eepromAddressSpaceIt != this->addressSpacesMappedById.end()) {
            const auto& eepromAddressSpace = eepromAddressSpaceIt->second;
            const auto eepromSegmentsIt = eepromAddressSpace.memorySegmentsByTypeAndName.find(
                MemorySegmentType::EEPROM
            );

            if (
                eepromSegmentsIt != eepromAddressSpace.memorySegmentsByTypeAndName.end()
                && !eepromSegmentsIt->second.empty()
            ) {
                return eepromSegmentsIt->second.begin()->second;
            }

        } else {
            // The EEPROM memory segment may be part of the data address space
            const auto dataAddressSpaceIt = this->addressSpacesMappedById.find("data");

            if (dataAddressSpaceIt != this->addressSpacesMappedById.end()) {
                const auto& dataAddressSpace = dataAddressSpaceIt->second;
                const auto eepromSegmentsIt = dataAddressSpace.memorySegmentsByTypeAndName.find(
                    MemorySegmentType::EEPROM
                );

                if (
                    eepromSegmentsIt != dataAddressSpace.memorySegmentsByTypeAndName.end()
                    && !eepromSegmentsIt->second.empty()
                ) {
                    return eepromSegmentsIt->second.begin()->second;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getFirstBootSectionMemorySegment() const {
        const auto programAddressSpaceIt = this->addressSpacesMappedById.find("prog");

        if (programAddressSpaceIt != this->addressSpacesMappedById.end()) {
            const auto& programAddressSpace = programAddressSpaceIt->second;
            const auto& programMemorySegments = programAddressSpace.memorySegmentsByTypeAndName;

            const auto flashMemorySegmentsit = programMemorySegments.find(MemorySegmentType::FLASH);

            if (flashMemorySegmentsit != programMemorySegments.end()) {
                const auto& flashMemorySegments = flashMemorySegmentsit->second;

                auto bootSectionSegmentIt = flashMemorySegments.find("boot_section_1");
                if (bootSectionSegmentIt != flashMemorySegments.end()) {
                    return bootSectionSegmentIt->second;
                }

                bootSectionSegmentIt = flashMemorySegments.find("boot_section");
                if (bootSectionSegmentIt != flashMemorySegments.end()) {
                    return bootSectionSegmentIt->second;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getSignatureMemorySegment() const {
        const auto signatureAddressSpaceIt = this->addressSpacesMappedById.find("signatures");
        if (signatureAddressSpaceIt != this->addressSpacesMappedById.end()) {
            const auto& signaturesAddressSpace = signatureAddressSpaceIt->second;
            const auto& signaturesAddressSpaceSegments = signaturesAddressSpace.memorySegmentsByTypeAndName;
            const auto signatureMemorySegmentsIt = signaturesAddressSpaceSegments.find(MemorySegmentType::SIGNATURES);

            if (
                signatureMemorySegmentsIt != signaturesAddressSpaceSegments.end()
                && !signatureMemorySegmentsIt->second.empty()
            ) {
                return signatureMemorySegmentsIt->second.begin()->second;
            }

        } else {
            // The signatures memory segment may be part of the data address space
            const auto dataAddressSpaceIt = this->addressSpacesMappedById.find("data");

            if (dataAddressSpaceIt != this->addressSpacesMappedById.end()) {
                const auto& dataAddressSpace = dataAddressSpaceIt->second;
                const auto signatureSegmentsIt = dataAddressSpace.memorySegmentsByTypeAndName.find(
                    MemorySegmentType::SIGNATURES
                );

                if (signatureSegmentsIt != dataAddressSpace.memorySegmentsByTypeAndName.end()) {
                    const auto& signatureSegmentsByName = signatureSegmentsIt->second;
                    const auto signatureSegmentIt = signatureSegmentsByName.find("signatures");

                    if (signatureSegmentIt != signatureSegmentsByName.end()) {
                        return signatureSegmentIt->second;
                    }
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getFuseMemorySegment() const {
        const auto dataAddressSpaceIt = this->addressSpacesMappedById.find("data");

        if (dataAddressSpaceIt != this->addressSpacesMappedById.end()) {
            const auto& dataAddressSpace = dataAddressSpaceIt->second;

            const auto fuseMemorySegmentsIt = dataAddressSpace.memorySegmentsByTypeAndName.find(
                MemorySegmentType::FUSES
            );

            if (
                fuseMemorySegmentsIt != dataAddressSpace.memorySegmentsByTypeAndName.end()
                && !fuseMemorySegmentsIt->second.empty()
            ) {
                return fuseMemorySegmentsIt->second.begin()->second;
            }
        }

        return std::nullopt;
    }

    std::optional<MemorySegment> TargetDescriptionFile::getLockbitsMemorySegment() const {
        const auto dataAddressSpaceIt = this->addressSpacesMappedById.find("data");

        if (dataAddressSpaceIt != this->addressSpacesMappedById.end()) {
            const auto& dataAddressSpace = dataAddressSpaceIt->second;

            const auto lockbitsMemorySegmentsIt = dataAddressSpace.memorySegmentsByTypeAndName.find(
                MemorySegmentType::LOCKBITS
            );

            if (
                lockbitsMemorySegmentsIt != dataAddressSpace.memorySegmentsByTypeAndName.end()
                && !lockbitsMemorySegmentsIt->second.empty()
            ) {
                return lockbitsMemorySegmentsIt->second.begin()->second;
            }
        }

        return std::nullopt;
    }

    std::optional<RegisterGroup> TargetDescriptionFile::getCpuRegisterGroup() const {
        const auto& modulesByName = this->modulesMappedByName;
        const auto cpuModuleIt = modulesByName.find("cpu");

        if (cpuModuleIt != modulesByName.end()) {
            const auto& cpuModule = cpuModuleIt->second;
            const auto cpuRegisterGroupIt = cpuModule.registerGroupsMappedByName.find("cpu");

            if (cpuRegisterGroupIt != cpuModule.registerGroupsMappedByName.end()) {
                return cpuRegisterGroupIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<RegisterGroup> TargetDescriptionFile::getBootLoadRegisterGroup() const {
        const auto bootLoadModuleIt = this->modulesMappedByName.find("boot_load");

        if (bootLoadModuleIt != this->modulesMappedByName.end()) {
            const auto& bootLoadModule = bootLoadModuleIt->second;
            auto bootLoadRegisterGroupIt = bootLoadModule.registerGroupsMappedByName.find("boot_load");

            if (bootLoadRegisterGroupIt != bootLoadModule.registerGroupsMappedByName.end()) {
                return bootLoadRegisterGroupIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<RegisterGroup> TargetDescriptionFile::getEepromRegisterGroup() const {
        const auto& modulesByName = this->modulesMappedByName;

        if (modulesByName.find("eeprom") != modulesByName.end()) {
            auto eepromModule = modulesByName.find("eeprom")->second;
            auto eepromRegisterGroupIt = eepromModule.registerGroupsMappedByName.find("eeprom");

            if (eepromRegisterGroupIt != eepromModule.registerGroupsMappedByName.end()) {
                return eepromRegisterGroupIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getStatusRegister() const {
        auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            auto statusRegisterIt = cpuRegisterGroup->registersMappedByName.find("sreg");

            if (statusRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return statusRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getStackPointerRegister() const {
        auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            auto stackPointerRegisterIt = cpuRegisterGroup->registersMappedByName.find("sp");

            if (stackPointerRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return stackPointerRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getStackPointerHighRegister() const {
        auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            auto stackPointerHighRegisterIt = cpuRegisterGroup->registersMappedByName.find("sph");

            if (stackPointerHighRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return stackPointerHighRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getStackPointerLowRegister() const {
        auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            auto stackPointerLowRegisterIt = cpuRegisterGroup->registersMappedByName.find("spl");

            if (stackPointerLowRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return stackPointerLowRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getOscillatorCalibrationRegister() const {
        auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            const auto& cpuRegisters = cpuRegisterGroup->registersMappedByName;

            auto osccalRegisterIt = cpuRegisters.find("osccal");
            if (osccalRegisterIt != cpuRegisters.end()) {
                return osccalRegisterIt->second;
            }

            osccalRegisterIt = cpuRegisters.find("osccal0");
            if (osccalRegisterIt != cpuRegisters.end()) {
                return osccalRegisterIt->second;
            }

            osccalRegisterIt = cpuRegisters.find("osccal1");
            if (osccalRegisterIt != cpuRegisters.end()) {
                return osccalRegisterIt->second;
            }

            osccalRegisterIt = cpuRegisters.find("fosccal");
            if (osccalRegisterIt != cpuRegisters.end()) {
                return osccalRegisterIt->second;
            }

            osccalRegisterIt = cpuRegisters.find("sosccala");
            if (osccalRegisterIt != cpuRegisters.end()) {
                return osccalRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getSpmcsRegister() const {
        const auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            const auto spmcsRegisterIt = cpuRegisterGroup->registersMappedByName.find("spmcsr");

            if (spmcsRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return spmcsRegisterIt->second;
            }
        }

        const auto bootLoadRegisterGroup = this->getBootLoadRegisterGroup();

        if (bootLoadRegisterGroup.has_value()) {
            const auto spmcsRegisterIt = bootLoadRegisterGroup->registersMappedByName.find("spmcsr");

            if (spmcsRegisterIt != bootLoadRegisterGroup->registersMappedByName.end()) {
                return spmcsRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getSpmcRegister() const {
        const auto cpuRegisterGroup = this->getCpuRegisterGroup();

        if (cpuRegisterGroup.has_value()) {
            const auto spmcRegisterIt = cpuRegisterGroup->registersMappedByName.find("spmcr");

            if (spmcRegisterIt != cpuRegisterGroup->registersMappedByName.end()) {
                return spmcRegisterIt->second;
            }
        }

        const auto bootLoadRegisterGroup = this->getBootLoadRegisterGroup();

        if (bootLoadRegisterGroup.has_value()) {
            const auto spmcRegisterIt = bootLoadRegisterGroup->registersMappedByName.find("spmcr");

            if (spmcRegisterIt != bootLoadRegisterGroup->registersMappedByName.end()) {
                return spmcRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getEepromAddressRegister() const {
        auto eepromRegisterGroup = this->getEepromRegisterGroup();

        if (eepromRegisterGroup.has_value()) {
            auto eepromAddressRegisterIt = eepromRegisterGroup->registersMappedByName.find("eear");

            if (eepromAddressRegisterIt != eepromRegisterGroup->registersMappedByName.end()) {
                return eepromAddressRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getEepromAddressLowRegister() const {
        auto eepromRegisterGroup = this->getEepromRegisterGroup();

        if (eepromRegisterGroup.has_value()) {
            auto eepromAddressRegisterIt = eepromRegisterGroup->registersMappedByName.find("eearl");

            if (eepromAddressRegisterIt != eepromRegisterGroup->registersMappedByName.end()) {
                return eepromAddressRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getEepromAddressHighRegister() const {
        auto eepromRegisterGroup = this->getEepromRegisterGroup();

        if (eepromRegisterGroup.has_value()) {
            auto eepromAddressRegisterIt = eepromRegisterGroup->registersMappedByName.find("eearh");

            if (eepromAddressRegisterIt != eepromRegisterGroup->registersMappedByName.end()) {
                return eepromAddressRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getEepromDataRegister() const {
        auto eepromRegisterGroup = this->getEepromRegisterGroup();

        if (eepromRegisterGroup.has_value()) {
            auto eepromDataRegisterIt = eepromRegisterGroup->registersMappedByName.find("eedr");

            if (eepromDataRegisterIt != eepromRegisterGroup->registersMappedByName.end()) {
                return eepromDataRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    std::optional<Register> TargetDescriptionFile::getEepromControlRegister() const {
        auto eepromRegisterGroup = this->getEepromRegisterGroup();

        if (eepromRegisterGroup.has_value()) {
            auto eepromControlRegisterIt = eepromRegisterGroup->registersMappedByName.find("eecr");

            if (eepromControlRegisterIt != eepromRegisterGroup->registersMappedByName.end()) {
                return eepromControlRegisterIt->second;
            }
        }

        return std::nullopt;
    }

    void TargetDescriptionFile::loadDebugWireAndJtagTargetParameters(TargetParameters& targetParameters) const {
        const auto& peripheralModules = this->getPeripheralModulesMappedByName();
        const auto& propertyGroups = this->getPropertyGroupsMappedByName();

        // OCD attributes can be found in property groups
        const auto ocdPropertyGroupIt = propertyGroups.find("ocd");
        if (ocdPropertyGroupIt != propertyGroups.end()) {
            const auto& ocdProperties = ocdPropertyGroupIt->second.propertiesMappedByName;

            const auto ocdRevisionPropertyIt = ocdProperties.find("ocd_revision");
            if (ocdRevisionPropertyIt != ocdProperties.end()) {
                targetParameters.ocdRevision = ocdRevisionPropertyIt->second.value.toUShort(nullptr, 10);
            }

            const auto ocdDataRegPropertyIt = ocdProperties.find("ocd_datareg");
            if (ocdDataRegPropertyIt != ocdProperties.end()) {
                targetParameters.ocdDataRegister = ocdDataRegPropertyIt->second.value.toUShort(nullptr, 16);
            }
        }

        const auto spmcsRegister = this->getSpmcsRegister();
        if (spmcsRegister.has_value()) {
            targetParameters.spmcRegisterStartAddress = spmcsRegister->offset;

        } else {
            const auto spmcRegister = this->getSpmcRegister();
            if (spmcRegister.has_value()) {
                targetParameters.spmcRegisterStartAddress = spmcRegister->offset;
            }
        }

        const auto osccalRegister = this->getOscillatorCalibrationRegister();
        if (osccalRegister.has_value()) {
            targetParameters.osccalAddress = osccalRegister->offset;
        }

        const auto eepromAddressRegister = this->getEepromAddressRegister();
        if (eepromAddressRegister.has_value()) {
            targetParameters.eepromAddressRegisterLow = eepromAddressRegister->offset;
            targetParameters.eepromAddressRegisterHigh = (eepromAddressRegister->size == 2)
                ? eepromAddressRegister->offset + 1 : eepromAddressRegister->offset;

        } else {
            const auto eepromAddressLowRegister = this->getEepromAddressLowRegister();
            if (eepromAddressLowRegister.has_value()) {
                targetParameters.eepromAddressRegisterLow = eepromAddressLowRegister->offset;
                auto eepromAddressHighRegister = this->getEepromAddressHighRegister();

                if (eepromAddressHighRegister.has_value()) {
                    targetParameters.eepromAddressRegisterHigh = eepromAddressHighRegister->offset;

                } else {
                    targetParameters.eepromAddressRegisterHigh = eepromAddressLowRegister->offset;
                }
            }
        }

        const auto eepromDataRegister = this->getEepromDataRegister();
        if (eepromDataRegister.has_value()) {
            targetParameters.eepromDataRegisterAddress = eepromDataRegister->offset;
        }

        const auto eepromControlRegister = this->getEepromControlRegister();
        if (eepromControlRegister.has_value()) {
            targetParameters.eepromControlRegisterAddress = eepromControlRegister->offset;
        }
    }

    void TargetDescriptionFile::loadPdiTargetParameters(TargetParameters& targetParameters) const {
        const auto& peripheralModules = this->getPeripheralModulesMappedByName();
        const auto& propertyGroups = this->getPropertyGroupsMappedByName();

        const auto pdiPropertyGroupIt = propertyGroups.find("pdi_interface");
        if (pdiPropertyGroupIt == propertyGroups.end()) {
            return;
        }

        const auto& pdiInterfaceProperties = pdiPropertyGroupIt->second.propertiesMappedByName;

        const auto appOffsetPropertyIt = pdiInterfaceProperties.find("app_section_offset");
        if (appOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.appSectionPdiOffset = appOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto bootOffsetPropertyIt = pdiInterfaceProperties.find("boot_section_offset");
        if (bootOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.bootSectionPdiOffset = bootOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto dataOffsetPropertyIt = pdiInterfaceProperties.find("datamem_offset");
        if (dataOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.ramPdiOffset = dataOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto eepromOffsetPropertyIt = pdiInterfaceProperties.find("eeprom_offset");
        if (eepromOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.eepromPdiOffset = eepromOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto userSigOffsetPropertyIt = pdiInterfaceProperties.find("user_signatures_offset");
        if (userSigOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.userSignaturesPdiOffset = userSigOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto prodSigOffsetPropertyIt = pdiInterfaceProperties.find("prod_signatures_offset");
        if (prodSigOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.productSignaturesPdiOffset = prodSigOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto fuseRegOffsetPropertyIt = pdiInterfaceProperties.find("fuse_registers_offset");
        if (fuseRegOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.fuseRegistersPdiOffset = fuseRegOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto lockRegOffsetPropertyIt = pdiInterfaceProperties.find("lock_registers_offset");
        if (lockRegOffsetPropertyIt != pdiInterfaceProperties.end()) {
            targetParameters.lockRegistersPdiOffset = lockRegOffsetPropertyIt->second.value.toUInt(nullptr, 16);
        }

        const auto nvmPeripheralModuleIt = peripheralModules.find("nvm");
        if (nvmPeripheralModuleIt != peripheralModules.end()) {
            const auto& nvmModule = nvmPeripheralModuleIt->second;

            const auto nvmInstanceIt = nvmModule.instancesMappedByName.find("nvm");
            if (nvmInstanceIt != nvmModule.instancesMappedByName.end()) {
                const auto& nvmInstance = nvmInstanceIt->second;

                const auto nvmRegisterGroupIt = nvmInstance.registerGroupsMappedByName.find("nvm");
                if (nvmRegisterGroupIt != nvmInstance.registerGroupsMappedByName.end()) {
                    targetParameters.nvmModuleBaseAddress = nvmRegisterGroupIt->second.offset;
                }
            }
        }

        const auto mcuPeripheralModuleIt = peripheralModules.find("mcu");
        if (mcuPeripheralModuleIt != peripheralModules.end()) {
            const auto& mcuModule = mcuPeripheralModuleIt->second;

            const auto mcuInstanceIt = mcuModule.instancesMappedByName.find("mcu");
            if (mcuInstanceIt != mcuModule.instancesMappedByName.end()) {
                const auto& mcuInstance = mcuInstanceIt->second;

                const auto mcuRegisterGroupIt = mcuInstance.registerGroupsMappedByName.find("mcu");
                if (mcuRegisterGroupIt != mcuInstance.registerGroupsMappedByName.end()) {
                    targetParameters.mcuModuleBaseAddress = mcuRegisterGroupIt->second.offset;
                }
            }
        }
    }

    void TargetDescriptionFile::loadUpdiTargetParameters(TargetParameters& targetParameters) const {
        const auto& propertyGroups = this->getPropertyGroupsMappedByName();
        const auto& peripheralModules = this->getPeripheralModulesMappedByName();
        const auto& modulesByName = this->getModulesMappedByName();

        const auto nvmCtrlPeripheralModuleIt = peripheralModules.find("nvmctrl");
        if (nvmCtrlPeripheralModuleIt != peripheralModules.end()) {
            const auto& nvmCtrlModule = nvmCtrlPeripheralModuleIt->second;

            const auto nvmCtrlInstanceIt = nvmCtrlModule.instancesMappedByName.find("nvmctrl");
            if (nvmCtrlInstanceIt != nvmCtrlModule.instancesMappedByName.end()) {
                const auto& nvmCtrlInstance = nvmCtrlInstanceIt->second;

                const auto nvmCtrlRegisterGroupIt = nvmCtrlInstance.registerGroupsMappedByName.find("nvmctrl");
                if (nvmCtrlRegisterGroupIt != nvmCtrlInstance.registerGroupsMappedByName.end()) {
                    targetParameters.nvmModuleBaseAddress = nvmCtrlRegisterGroupIt->second.offset;
                }
            }
        }

        const auto updiPropertyGroupIt = propertyGroups.find("updi_interface");
        if (updiPropertyGroupIt != propertyGroups.end()) {
            const auto& updiInterfaceProperties = updiPropertyGroupIt->second.propertiesMappedByName;

            const auto ocdBaseAddressPropertyIt = updiInterfaceProperties.find("ocd_base_addr");
            if (ocdBaseAddressPropertyIt != updiInterfaceProperties.end()) {
                targetParameters.ocdModuleAddress = ocdBaseAddressPropertyIt->second.value.toUShort(nullptr, 16);
            }

            const auto progMemOffsetPropertyIt = updiInterfaceProperties.find("progmem_offset");
            if (progMemOffsetPropertyIt != updiInterfaceProperties.end()) {
                targetParameters.programMemoryUpdiStartAddress = progMemOffsetPropertyIt->second.value.toUInt(
                    nullptr,
                    16
                );
            }
        }

        const auto signatureMemorySegment = this->getSignatureMemorySegment();
        if (signatureMemorySegment.has_value()) {
            targetParameters.signatureSegmentStartAddress = signatureMemorySegment->startAddress;
            targetParameters.signatureSegmentSize = signatureMemorySegment->size;
        }

        const auto fuseMemorySegment = this->getFuseMemorySegment();
        if (fuseMemorySegment.has_value()) {
            targetParameters.fuseSegmentStartAddress = fuseMemorySegment->startAddress;
            targetParameters.fuseSegmentSize = fuseMemorySegment->size;
        }

        const auto lockbitsMemorySegment = this->getLockbitsMemorySegment();
        if (lockbitsMemorySegment.has_value()) {
            targetParameters.lockbitsSegmentStartAddress = lockbitsMemorySegment->startAddress;
        }
    }
}
