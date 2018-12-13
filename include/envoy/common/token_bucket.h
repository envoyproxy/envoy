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
   * @param tokens supplies the number of tokens to be consumed. Default is 1.
   * @return true if bucket is not empty, otherwise it returns false.
   */
  virtual bool consume(uint64_t tokens = 1) PURE;

  /**
   * @return returns the approximate time until a next token is available. Currently it
   * returns the upper bound on the amount of time until a next token is available.
   */
  virtual uint64_t nextTokenAvailableMs() PURE;
};

typedef std::unique_ptr<TokenBucket> TokenBucketPtr;

}; // namespace Envoy
