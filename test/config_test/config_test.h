#pragma once

#include <cstdint>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace ConfigTest {

/**
 * Load all configurations from a tar file and make sure they are valid.
 * @param path supplies the path to recurse through looking for config files.
 * @return uint32_t the number of configs tested.
 */
uint32_t run(const std::string& path);

/**
 * Test --config-yaml overlay merge.
 */
void testMerge();

} // namespace ConfigTest
} // namespace Envoy
