#pragma once

#include <memory>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Matchers {

/**
 * Generic string matching interface.
 */
class StringMatcher {
public:
  virtual ~StringMatcher() = default;

  struct Context {
    OptRef<const Network::TransportSocketOptions> transport_socket_options_;
  };

  /**
   * Return whether a passed string value matches.
   */
  virtual bool match(const absl::string_view value,
                     OptRef<const Context> context = absl::nullopt) const PURE;
};

using StringMatcherPtr = std::unique_ptr<const StringMatcher>;

} // namespace Matchers
} // namespace Envoy
