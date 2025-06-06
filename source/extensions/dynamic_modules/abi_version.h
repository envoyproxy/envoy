#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "33a5ad036b3fc582cca0e4e35b5777d5b8cf92acf4234a8b88f9239140b04f5d";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#endif
