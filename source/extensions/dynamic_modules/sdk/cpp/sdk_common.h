#pragma once

#include <cstdint>
#include <string_view>

namespace Envoy {
namespace DynamicModules {

/**
 * CommonHandle exposes the host callbacks that are process-wide rather than tied to any single
 * Envoy object. It is inherited by the per-extension config handles instead of being reached
 * through a global, so that these methods are only in scope where the host can actually service
 * them, and so that a test can supply them by overriding the same handle it already fakes.
 *
 * All of the methods read from the Envoy runtime: the layered key/value configuration described by
 * the `layered_runtime` bootstrap option, including RTDS layers and values set through the admin
 * `/runtime_modify` endpoint.
 *
 * NOTE: Envoy reaches the runtime through a server context that only exists on the main thread.
 * That is why these methods live on the config handles, which are used while a config is being
 * built on the main thread, and not on the per-request or per-stream handles, which run on worker
 * threads where the runtime is not reachable. Read the values you need while building your config
 * and cache them there.
 */
class CommonHandle {
public:
  virtual ~CommonHandle();

  /**
   * Reads a runtime value as a boolean.
   * @param key The runtime key to look up.
   * @param default_value Returned when the key does not exist or the stored value is not a boolean.
   * @return The runtime value as a boolean, or default_value.
   */
  virtual bool getRuntimeBool(std::string_view key, bool default_value) = 0;

  /**
   * Reads a runtime value as an unsigned integer.
   *
   * Envoy stores every numeric runtime value as a double, so this conversion is lossy at both
   * ends: a value above 2^53 is rounded to the nearest representable value and a fractional value
   * is truncated toward zero, and in both cases the converted value is returned rather than
   * default_value. Only a negative value, or one beyond the range of a uint64_t, yields
   * default_value. Use getRuntimeNumber to read the value without either conversion.
   *
   * @param key The runtime key to look up.
   * @param default_value Returned when the key does not exist or the stored value is not an
   * integer.
   * @return The runtime value as an unsigned integer, or default_value.
   */
  virtual uint64_t getRuntimeInt(std::string_view key, uint64_t default_value) = 0;

  /**
   * Reads a runtime value as a double.
   *
   * This is the lossless counterpart to getRuntimeInt: it returns the value exactly as Envoy
   * stores it, so it neither rounds nor truncates, and it reads negative values, which
   * getRuntimeInt answers with its default.
   *
   * @param key The runtime key to look up.
   * @param default_value Returned when the key does not exist or the stored value is not a number.
   * @return The runtime value as a double, or default_value.
   */
  virtual double getRuntimeNumber(std::string_view key, double default_value) = 0;
};

} // namespace DynamicModules
} // namespace Envoy
