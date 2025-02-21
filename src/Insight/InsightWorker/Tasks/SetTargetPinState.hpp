#pragma once

#include "InsightWorkerTask.hpp"
#include "src/Targets/TargetPinDescriptor.hpp"

namespace Bloom
{
    class SetTargetPinState: public InsightWorkerTask
    {
        Q_OBJECT

    public:
        SetTargetPinState(const Targets::TargetPinDescriptor& pinDescriptor, const Targets::TargetPinState& pinState)
            : pinDescriptor(pinDescriptor)
            , pinState(pinState)
        {}

        QString brief() const override {
            return "Updating target pin state";
        }

        TaskGroups taskGroups() const override {
            return TaskGroups({
                TaskGroup::USES_TARGET_CONTROLLER,
            });
        };

    protected:
        void run(Services::TargetControllerService& targetControllerService) override;

    private:
        Targets::TargetPinDescriptor pinDescriptor;
        Targets::TargetPinState pinState;
    };
}
