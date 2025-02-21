#pragma once

#include <string>
#include <atomic>
#include <cstdint>

#include "CommandTypes.hpp"

#include "src/TargetController/Responses/Response.hpp"

namespace Bloom::TargetController::Commands
{
    using CommandIdType = int;
    static_assert(std::atomic<CommandIdType>::is_always_lock_free);

    class Command
    {
    public:
        using SuccessResponseType = Responses::Response;

        CommandIdType id = ++(Command::lastCommandId);

        static constexpr CommandType type = CommandType::GENERIC;
        static const inline std::string name = "GenericCommand";

        Command() = default;
        virtual ~Command() = default;

        Command(const Command& other) = default;
        Command(Command&& other) = default;

        Command& operator = (const Command& other) = default;
        Command& operator = (Command&& other) = default;

        [[nodiscard]] virtual CommandType getType() const {
            return Command::type;
        }

        [[nodiscard]] virtual bool requiresActiveState() const {
            return true;
        }

        [[nodiscard]] virtual bool requiresStoppedTargetState() const {
            return false;
        }

        [[nodiscard]] virtual bool requiresDebugMode() const {
            return true;
        }

    private:
        static inline std::atomic<CommandIdType> lastCommandId = 0;
    };
}
