#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "c5161460270d2895054f0a7773a30c00b2ae8072c7c4966dc1d1fe1bcb40b2d5";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
