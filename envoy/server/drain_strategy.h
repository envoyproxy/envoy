#pragma once

namespace Envoy {
namespace Server {

/**
 * During the drain sequence, different components ask the DrainManager
 * whether to drain via drainClose(). This enum dictates the behaviour of
 * drainClose() calls.
 */
enum class DrainStrategy {
  /**
   * The probability of drainClose() returning true increases from 0 to 100%
   * over the duration of the drain period.
   */
  Gradual,

  /**
   * drainClose() will return true as soon as the drain sequence is initiated.
   */
  Immediate,
};

} // namespace Server
} // namespace Envoy
