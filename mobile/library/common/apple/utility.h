#include <CoreFoundation/CoreFoundation.h>

#include <string>

namespace Envoy {
namespace Apple {

std::string toString(CFStringRef);
int toInt(CFNumberRef);

} // namespace Apple
} // namespace Envoy
