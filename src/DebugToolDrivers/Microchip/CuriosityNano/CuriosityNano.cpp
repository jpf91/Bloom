#include "CuriosityNano.hpp"

namespace Bloom::DebugToolDrivers
{
    CuriosityNano::CuriosityNano()
        : EdbgDevice(
            CuriosityNano::USB_VENDOR_ID,
            CuriosityNano::USB_PRODUCT_ID,
            CuriosityNano::CMSIS_HID_INTERFACE_NUMBER,
            true
        )
    {}
}
