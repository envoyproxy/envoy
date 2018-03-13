#pragma once

#include <chrono>

namespace Envoy {

/**
 * This class implements the token bucket algorithm (not thread-safe).
 *
 * https://en.wikipedia.org/wiki/Token_bucket
 */
class TokenBucket {

  constexpr static double nano = double(std::micro::num) / double(std::micro::den);

public:
  /**
   * @param max_tokens supplies the maximun number of tokens in the bucket.
   * @param refill_rate supplies the number of tokens coming back into the bucket on each second.
   */
  TokenBucket(uint64_t max_tokens, double tokens_per_sec = 1);

  /**
   * @param tokens supplies the number of tokens consumed from the bucket on each call. Default
   * is 1.
   * @return true if bucket is not empty, false otherwise.
   */
  bool consume(uint64_t tokens = 1);

private:
  const double max_tokens_;
  const double refill_rate_;
  double tokens_;
  std::chrono::time_point<std::chrono::high_resolution_clock> last_fill_;
};

} // namespace Envoy
