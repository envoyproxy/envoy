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
  virtual ~TokenBucket() {}

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
   * @return returns the approximate time until a next token is available. Currently it
   * returns the upper bound on the amount of time until a next token is available.
   */
  virtual std::chrono::milliseconds nextTokenAvailable() PURE;
};

typedef std::unique_ptr<TokenBucket> TokenBucketPtr;

}; // namespace Envoy
