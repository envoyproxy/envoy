#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief Contains the configuration for the sampler. Might be extended in a future version
 *
 */
class SamplerConfig {
public:
  static constexpr uint32_t ROOT_SPANS_PER_MINUTE_DEFAULT = 1000;

  explicit SamplerConfig(uint32_t default_root_spans_per_minute)
      : default_root_spans_per_minute_(default_root_spans_per_minute > 0
                                           ? default_root_spans_per_minute
                                           : ROOT_SPANS_PER_MINUTE_DEFAULT),
        root_spans_per_minute_(default_root_spans_per_minute_) {}

  SamplerConfig(const SamplerConfig&) = delete;
  SamplerConfig& operator=(const SamplerConfig&) = delete;

  /**
   * @brief Parses a json string containing the expected root spans per minute.
   *
   * @param json A string containing the configuration.
   *
   * @return true if parsing was successful, false otherwise
   */
  bool parse(const std::string& json);

  /**
   * @brief Returns wanted root spans per minute
   *
   * @return uint32_t wanted root spans per minute
   */
  uint32_t getRootSpansPerMinute() const { return root_spans_per_minute_.load(); }

private:
  const uint32_t default_root_spans_per_minute_;
  std::atomic<uint32_t> root_spans_per_minute_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
