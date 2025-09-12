#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "26e44565b3e46aaa062e4745dccebb30e2b78aa436ed975547dd0128a9dfdb3c";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
