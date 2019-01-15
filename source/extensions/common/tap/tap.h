#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/service/tap/v2alpha/common.pb.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Sink for sending tap messages.
 */
class Sink {
public:
  virtual ~Sink() = default;

  /**
   * Send a fully buffered trace to the sink.
   * @param trace supplies the trace to send. The trace message is opaque and is assumed to be a
   *        discrete trace message (as opposed to a portion of a larger trace that should be
   *        aggregated).
   */
  virtual void submitBufferedTrace(std::shared_ptr<Protobuf::Message> trace) PURE;
};

/**
 * Generic configuration for a tap extension (filter, transport socket, etc.).
 */
class ExtensionConfig {
public:
  virtual ~ExtensionConfig() = default;

  /**
   * @return the ID to use for admin extension configuration tracking (if applicable).
   */
  virtual const std::string& adminId() PURE;

  /**
   * Clear any active tap configuration.
   */
  virtual void clearTapConfig() PURE;

  /**
   * Install a new tap configuration.
   * @param proto_config supplies the generic tap config to install. Not all configuration fields
   *        may be applicable to an extension (e.g. HTTP fields). The extension is free to fail
   *        the configuration load via exception if it wishes.
   * @param admin_streamer supplies the singleton admin sink to use for output if the configuration
   *        specifies that output type. May not be used if the configuration does not specify
   *        admin output. May be nullptr if admin is not used to supply the config.
   */
  virtual void newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                            Sink* admin_streamer) PURE;
};

class Matcher;
using MatcherPtr = std::unique_ptr<Matcher>;

/**
 * Base class for all tap matchers.
 *
 * A high level note on the design of tap matching which is different from other matching in Envoy
 * due to a requirement to support streaming matching (match as new data arrives versus
 * calculating the match given all available data at once).
 * - The matching system is composed of a constant matching configuration. This is essentially
 *   a tree of matchers given logical AND, OR, NOT, etc.
 * - A per-stream/request matching status must be kept in order to compute interim match status.
 * - In order to make this computationally efficient, the matching tree is kept in a vector, with
 *   all references to other matchers implemented using an index into the vector.
 * - The previous point allows the creation of a per-stream/request vector of booleans of the same
 *   size as the matcher vector. Then, when match status is updated given new information, the
 *   vector of booleans can be easily updated using the same indexes as in the constant match
 *   configuration.
 * - Finally, a matches() function can be trivially implemented by looking in the status vector at
 *   the index position that the current matcher is located in.
 */
class Matcher {
public:
  virtual ~Matcher() = default;

  /**
   * @return the matcher's index in the match tree vector (see above).
   */
  size_t index() { return my_index_; }

  /**
   * Update match status given new information.
   * @param request_headers supplies the request headers, if available.
   * @param response_headers supplies the response headers, if available.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual bool updateMatchStatus(const Http::HeaderMap* request_headers,
                                 const Http::HeaderMap* response_headers,
                                 std::vector<bool>& statuses) const PURE;

  /**
   * @return whether given currently available information, the matcher matches.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  bool matches(const std::vector<bool>& statuses) const { return statuses[my_index_]; }

protected:
  /**
   * Base class constructor for a matcher.
   * @param matchers supplies the match tree vector being built.
   */
  Matcher(const std::vector<MatcherPtr>& matchers)
      // NOTE: This code assumes that the index for the matcher being constructed has already been
      // allocated, which is why my_index_ is set to size() - 1. See buildMatcher() in
      // tap_matcher.cc.
      : my_index_(matchers.size() - 1) {}

  const size_t my_index_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
