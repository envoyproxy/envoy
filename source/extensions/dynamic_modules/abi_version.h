#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "671102aad648320f21c53d1c04aa9780d1d2668e3e63e8a993d30a9029a168e4";

#ifdef __cplusplus
} // extern "C"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace AbiVersion {
// For C++ code, also provide the version in the namespace.
constexpr const char kAbiVersion[] =
    "671102aad648320f21c53d1c04aa9780d1d2668e3e63e8a993d30a9029a168e4";
} // namespace AbiVersion
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
