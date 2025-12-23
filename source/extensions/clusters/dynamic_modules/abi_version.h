#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {
#endif
// This is the cluster ABI version calculated as a sha256 hash of the cluster ABI header file.
// When the cluster ABI changes, this value must change, and the correctness of this value is
// checked by the test.
const char* kClusterAbiVersion = "06bb267ebe1c5e5859d6d411f04d5727a5d48683fe9e4560f8998a033ae84cef";

#ifdef __cplusplus
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
#endif
