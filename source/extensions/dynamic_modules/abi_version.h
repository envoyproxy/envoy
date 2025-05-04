#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "30e52545b6085c9b061a4e025ed9237e8f036cbde1b7050f5b008ce4196c4675";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
