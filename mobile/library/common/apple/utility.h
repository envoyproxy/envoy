#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <string>

namespace Envoy {
namespace Apple {

// Converts a CStringRef from Objective-C into a C++ std::string in the most effecient way feasible.
std::string toString(CFStringRef);

// Converts a CFNumberRef (an Objective-C number representation) into a C++ int value.
int toInt(CFNumberRef);

} // namespace Apple
} // namespace Envoy
