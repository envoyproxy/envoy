#include <string>
#include <vector>

#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/log_level.h"
#include "library/common/api/external.h"
#include "library/common/config/internal.h"
#include "library/common/data/utility.h"

#if defined(__APPLE__)
#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"
#endif

#include "benchmark/benchmark.h"
namespace Envoy {
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LoadFromString(benchmark::State& state) {
  Platform::EngineBuilder engine_builder;
  ProtobufMessage::ValidationVisitor& validation_visitor =
      ProtobufMessage::getStrictValidationVisitor();

  for (auto _ : state) { // NOLINT
    std::string config_yaml = engine_builder.generateConfigStr();
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    MessageUtil::loadFromYaml(absl::StrCat(config_header, config_yaml), bootstrap,
                              validation_visitor);
  }
}

static void BM_LoadFromProto(benchmark::State& state) {
  Platform::EngineBuilder engine_builder;

  for (auto _ : state) { // NOLINT
    auto bootstrap = engine_builder.generateBootstrap();
    envoy::config::bootstrap::v3::Bootstrap envoy_bootstrap;
    envoy_bootstrap.CopyFrom(*bootstrap);
  }
}
} // namespace Envoy

BENCHMARK(Envoy::BM_LoadFromString)->Unit(benchmark::kMillisecond);
BENCHMARK(Envoy::BM_LoadFromProto)->Unit(benchmark::kMillisecond);
