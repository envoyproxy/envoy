#pragma once

#include <cstdint>
#include <string>

namespace Envoy {
namespace ConfigTest {

/**
 * Load all configurations from a tar file and make sure they are valid.
 * @param path supplies the path to recurse through looking for config files.
 * @return uint32_t the number of configs tested.
 */
uint32_t run(const std::string& path);

} // namespace ConfigTest
} // namespace Envoy
