#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "066a903148256459ae89aaefffe12dac2ed317a66216c5ecd6f579cf6e3a6c5f";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
