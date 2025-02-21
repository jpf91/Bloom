#pragma once

#include <QFile>
#include <QDomDocument>

#include "AddressSpace.hpp"
#include "MemorySegment.hpp"
#include "PropertyGroup.hpp"
#include "RegisterGroup.hpp"
#include "Module.hpp"
#include "Variant.hpp"
#include "Pinout.hpp"
#include "Interface.hpp"

namespace Bloom::Targets::TargetDescription
{
    /**
     * A target description file (TDF) is an XML file that describes a particular target. All supported targets come
     * with a target description file.
     *
     * Target description files are part of the Bloom codebase.
     * For target description files, see the directory "src/Targets/TargetDescriptionFiles/".
     *
     * During the build process, all target description files are copied to the distribution directory, ready
     * to be shipped with the Bloom binary.
     *
     * Processing of target description files is done in this class.
     *
     * This class may be extended to further reflect a TDF that is specific to a particular target, target architecture
     * or target family. For example, the Targets::Microchip::Avr::Avr8Bit::TargetDescription::TargetDescriptionFile
     * class inherits from this class, to represent TDFs for AVR8 targets. The derived class provides access to
     * additional data that is only found in AVR8 TDFs (such as AVR target signature, AVR Family, etc).
     *
     * For more information of TDFs, see src/Targets/TargetDescription/README.md
     */
    class TargetDescriptionFile
    {
    public:
        /**
         * Will construct a TargetDescriptionFile instance from the XML of a target description file, the path to which
         * is given via xmlFilePath.
         *
         * @param xmlFilePath
         */
        explicit TargetDescriptionFile(const QString& xmlFilePath) {
            this->init(xmlFilePath);
        }

        /**
         * Will construct a TargetDescriptionFile instance from pre-loaded XML.
         *
         * @param xml
         */
        explicit TargetDescriptionFile(const QDomDocument& xml) {
            this->init(xml);
        }

        /**
         * Returns the target name extracted from the TDF.
         *
         * @return
         */
        [[nodiscard]] const std::string& getTargetName() const;

        /**
         * Returns the target family name extracted from the TDF.
         *
         * @return
         */
        [[nodiscard]] const std::string& getFamilyName() const;

        /**
         * Returns a mapping of all property groups, with the property group name being the key.
         *
         * @return
         */
        [[nodiscard]] const std::map<std::string, PropertyGroup>& getPropertyGroupsMappedByName() const {
            return this->propertyGroupsMappedByName;
        }

        /**
         * Returns a mapping of all modules, with the module name being the key.
         *
         * @return
         */
        [[nodiscard]] const std::map<std::string, Module>& getModulesMappedByName() const {
            return this->modulesMappedByName;
        }

        /**
         * Returns a mapping of all peripheral modules, with the peripheral module name being the key.
         *
         * @return
         */
        [[nodiscard]] const std::map<std::string, Module>& getPeripheralModulesMappedByName() const {
            return this->peripheralModulesMappedByName;
        }

        /**
         * Returns all variants found in the TDF.
         *
         * @return
         */
        [[nodiscard]] const std::vector<Variant>& getVariants() const {
            return this->variants;
        }

        /**
         * Returns a mapping of pinouts, with the pinout name being the key.
         *
         * @return
         */
        [[nodiscard]] const std::map<std::string, Pinout>& getPinoutsMappedByName() const {
            return this->pinoutsMappedByName;
        }

    protected:
        std::string targetName;
        std::string familyName;

        std::map<std::string, AddressSpace> addressSpacesMappedById;
        std::map<std::string, PropertyGroup> propertyGroupsMappedByName;
        std::map<std::string, Module> modulesMappedByName;
        std::map<std::string, Module> peripheralModulesMappedByName;
        std::map<std::string, std::vector<RegisterGroup>> peripheralRegisterGroupsMappedByModuleRegisterGroupName;
        std::vector<Variant> variants;
        std::map<std::string, Pinout> pinoutsMappedByName;
        std::map<std::string, Interface> interfacesByName;

        TargetDescriptionFile() = default;
        virtual ~TargetDescriptionFile() = default;

        TargetDescriptionFile(const TargetDescriptionFile& other) = default;
        TargetDescriptionFile(TargetDescriptionFile&& other) = default;

        TargetDescriptionFile& operator = (const TargetDescriptionFile& other) = default;
        TargetDescriptionFile& operator = (TargetDescriptionFile&& other) = default;

        virtual void init(const QDomDocument& document);
        void init(const QString& xmlFilePath);

        /**
         * Constructs an AddressSpace object from an XML element.
         *
         * @param xmlElement
         * @return
         */
        static AddressSpace generateAddressSpaceFromXml(const QDomElement& xmlElement);

        /**
         * Constructs a MemorySegment object from an XML element.
         *
         * @param xmlElement
         * @return
         */
        static MemorySegment generateMemorySegmentFromXml(const QDomElement& xmlElement);

        /**
         * Constructs a RegisterGroup object from an XML element.
         *
         * @param xmlElement
         * @return
         */
        static RegisterGroup generateRegisterGroupFromXml(const QDomElement& xmlElement);

        /**
         * Constructs a Register object from an XML element.
         *
         * @param xmlElement
         * @return
         */
        static Register generateRegisterFromXml(const QDomElement& xmlElement);

        /**
         * Consturcts a BitField object from an XML element.
         *
         * @param xmlElement
         * @return
         */
        static BitField generateBitFieldFromXml(const QDomElement& xmlElement);

        /**
         * Extracts all address spaces and loads them into this->addressSpacesMappedById.
         */
        void loadAddressSpaces(const QDomDocument& document);

        /**
         * Extracts all property groups and loads them into this->propertyGroupsMappedByName.
         */
        void loadPropertyGroups(const QDomDocument& document);

        /**
         * Extracts all modules and loads them into this->modulesMappedByName.
         */
        void loadModules(const QDomDocument& document);

        /**
         * Extracts all peripheral modules and loads them into this->peripheralModulesMappedByName.
         */
        void loadPeripheralModules(const QDomDocument& document);

        /**
         * Extracts all variants and loads them into this->variants.
         */
        void loadVariants(const QDomDocument& document);

        /**
         * Extracts all pinouts and loads them into this->pinoutsMappedByName.
         */
        void loadPinouts(const QDomDocument& document);

        /**
         * Extracts all interfaces and loads them into this->interfacesByName
         */
        void loadInterfaces(const QDomDocument& document);
    };
}
