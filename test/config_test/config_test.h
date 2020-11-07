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

/**
 * Loads the given bootstrap file with an optional bootstrap_version into the
 * given bootstrap protobuf message using the server's loadBootstrapConfig.
 */
void loadVersionedBootstrapFile(const std::string& filename,
                                envoy::config::bootstrap::v3::Bootstrap& bootstrap_message,
                                absl::optional<uint32_t> bootstrap_version = absl::nullopt);

/**
 * Loads the given bootstrap proto into the given bootstrap protobuf message
 * using the server's loadBootstrapConfig.
 */
void loadBootstrapConfigProto(const envoy::config::bootstrap::v3::Bootstrap& in_proto,
                              envoy::config::bootstrap::v3::Bootstrap& bootstrap_message);

} // namespace ConfigTest
} // namespace Envoy
