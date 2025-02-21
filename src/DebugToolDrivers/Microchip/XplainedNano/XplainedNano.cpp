#include "XplainedNano.hpp"

namespace Bloom::DebugToolDrivers
{
    XplainedNano::XplainedNano()
        : EdbgDevice(
            XplainedNano::USB_VENDOR_ID,
            XplainedNano::USB_PRODUCT_ID,
            XplainedNano::CMSIS_HID_INTERFACE_NUMBER,
            true
        )
    {}
}
