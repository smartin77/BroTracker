// Project-local override supported by the Teensy core's weak USB string symbols.
// Leave manufacturer, hardware serial number and composite descriptors unchanged.
#include "usb_names.h"

#define BROTRACKER_USB_PRODUCT_NAME \
    {'B', 'r', 'o', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', \
     'U', 'S', 'B', ' ', 'a', 'u', 'd', 'i', 'o'}

struct usb_string_descriptor_struct usb_string_product_name = {
    2 + sizeof((uint16_t[])BROTRACKER_USB_PRODUCT_NAME),
    3, // USB string descriptor (UTF-16LE).
    BROTRACKER_USB_PRODUCT_NAME
};
