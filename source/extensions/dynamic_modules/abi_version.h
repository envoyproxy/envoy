#pragma once
#ifdef __cplusplus
namespace Envoy {
namespace Extensions {
namespace DynamicModules {
static constexpr const char* kAbiVersion = "0874b1e9587ef1dbd355ffde32f3caf424cb819df552de4833b2ed5b8996c18b";
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
#else
#define kAbiVersion "0874b1e9587ef1dbd355ffde32f3caf424cb819df552de4833b2ed5b8996c18b"
#endif