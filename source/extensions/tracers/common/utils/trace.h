#pragma once

#include <string>

#include "envoy/common/time.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {
namespace Utils {

/**
 * Common trace ID utilities for propagators.
 * These functions provide generic trace/span ID parsing and generation
 * that can be used by different propagation standards (B3, W3C, etc.).
 */
class Trace {
public:
  /**
   * Parses a trace ID from a hex string into high and low 64-bit parts.
   *
   * @param trace_id_hex The hexadecimal trace ID string (16 or 32 characters).
   * @param trace_id_high Output parameter for the high 64 bits (0 for 64-bit trace IDs).
   * @param trace_id_low Output parameter for the low 64 bits.
   * @return true if parsing was successful, false otherwise.
   */
  static bool parseTraceId(const std::string& trace_id_hex, uint64_t& trace_id_high,
                           uint64_t& trace_id_low);

  /**
   * Parses a span ID from a hex string.
   *
   * @param span_id_hex The hexadecimal span ID string (16 characters).
   * @param span_id Output parameter for the span ID.
   * @return true if parsing was successful, false otherwise.
   */
  static bool parseSpanId(const std::string& span_id_hex, uint64_t& span_id);

  /**
   * Generates a random 64-bit integer using a time-based seed.
   *
   * @param time_source Time source for seeding the random generator.
   * @return A randomly-generated 64-bit integer.
   */
  static uint64_t generateRandom64(TimeSource& time_source);

  /**
   * Generates a random 64-bit integer using default time source.
   *
   * @return A randomly-generated 64-bit integer.
   */
  static uint64_t generateRandom64();
};

} // namespace Utils
} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy