/**
 * Utility to convert bootstrap from its YAML/JSON/proto representation to text
 * proto.
 *
 * Usage:
 *
 * bootstrap2pb <input YAML/JSON/proto path> <output text proto path>
 */
#include <cstdlib>
#include <fstream>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/random_generator.h"
#include "source/common/event/real_time_system.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/exe/platform_impl.h"

// NOLINT(namespace-envoy)
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input YAML/JSON/proto path> <output text text path>"
              << std::endl;
    return EXIT_FAILURE;
  }

  Envoy::PlatformImpl platform_impl_;
  Envoy::Stats::IsolatedStoreImpl stats_store;
  Envoy::Event::RealTimeSystem time_system; // NO_CHECK_FORMAT(real_time)
  Envoy::Random::RandomGeneratorImpl rand;
  // TODO: fix or remove - this seems to be broken
  Envoy::Api::Impl api(platform_impl_.threadFactory(), stats_store, time_system,
                       platform_impl_.fileSystem(), rand);

  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  Envoy::MessageUtil::loadFromFile(argv[1], bootstrap,
                                   Envoy::ProtobufMessage::getStrictValidationVisitor(), api);
  std::ofstream bootstrap_file(argv[2]);
  bootstrap_file << bootstrap.DebugString();
  return EXIT_SUCCESS;
}
