#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Envoy {

/**
 * This class defines an interface for the token bucket algorithm.
 *
 * https://en.wikipedia.org/wiki/Token_bucket
 */
class TokenBucket {
public:
  virtual ~TokenBucket() = default;

  /**
   * @param tokens supplies the number of tokens to be consumed.
   * @param allow_partial supplies whether the token bucket will allow consumption of less tokens
   *                      than asked for. If allow_partial is true, the bucket contains 3 tokens,
   *                      and the caller asks for 5, the bucket will return 3 tokens and now be
   *                      empty.
   * @return the number of tokens actually consumed.
   */
  virtual uint64_t consume(uint64_t tokens, bool allow_partial) PURE;

  /**
   * @param tokens supplies the number of tokens to be consumed.
   * @param allow_partial supplies whether the token bucket will allow consumption of less tokens
   *                      than asked for. If allow_partial is true, the bucket contains 3 tokens,
   *                      and the caller asks for 5, the bucket will return 3 tokens and now be
   *                      empty.
   * @param time_to_next_token out param indicating the approx time until next token is available.
   * @return the number of tokens actually consumed.
   */
  virtual uint64_t consume(uint64_t tokens, bool allow_partial,
                           std::chrono::milliseconds& time_to_next_token) PURE;

  /**
   * @return returns the approximate time until a next token is available. Currently it
   * returns the upper bound on the amount of time until a next token is available.
   */
  virtual std::chrono::milliseconds nextTokenAvailable() PURE;

  /**
   * Reset the bucket with a specific number of tokens. Refill will begin again from the time that
   * this routine is called.
   * Note: The reset call might be honored only the first time this method is called. Check the
   * concrete implementation to confirm.
   */
  virtual void maybeReset(uint64_t num_tokens) PURE;
};

using TokenBucketPtr = std::unique_ptr<TokenBucket>;

}; // namespace Envoy
