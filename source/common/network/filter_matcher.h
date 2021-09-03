#pragma once

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Network {

/**
 * The helper to transform ListenerFilterChainMatchPredicate message to single matcher.
 */
class ListenerFilterMatcherBuilder {
public:
  static ListenerFilterMatcherPtr buildListenerFilterMatcher(
      const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config);
};

/**
 * Any matcher (always matches).
 */
class ListenerFilterAnyMatcher final : public ListenerFilterMatcher {
public:
  bool matches(ListenerFilterCallbacks&) const override { return true; }
};

class ListenerFilterNotMatcher final : public ListenerFilterMatcher {
public:
  ListenerFilterNotMatcher(
      const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config)
      : sub_matcher_(ListenerFilterMatcherBuilder::buildListenerFilterMatcher(match_config)) {}
  bool matches(ListenerFilterCallbacks& cb) const override { return !sub_matcher_->matches(cb); }

private:
  const ListenerFilterMatcherPtr sub_matcher_;
};

/**
 * Destination port matcher.
 */
class ListenerFilterDstPortMatcher final : public ListenerFilterMatcher {
public:
  explicit ListenerFilterDstPortMatcher(const ::envoy::type::v3::Int32Range& range)
      : start_(range.start()), end_(range.end()) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    const auto& address = cb.socket().connectionInfoProvider().localAddress();
    // Match on destination port (only for IP addresses).
    if (address->type() == Address::Type::Ip) {
      const auto port = address->ip()->port();
      return start_ <= port && port < end_;
    } else {
      return true;
    }
  }

private:
  const uint32_t start_;
  const uint32_t end_;
};

/**
 * Matcher for implementing set logic.
 */
class ListenerFilterSetLogicMatcher : public ListenerFilterMatcher {
public:
  explicit ListenerFilterSetLogicMatcher(
      absl::Span<const ::envoy::config::listener::v3::ListenerFilterChainMatchPredicate* const>
          predicates);

protected:
  absl::FixedArray<ListenerFilterMatcherPtr> sub_matchers_;
};

class ListenerFilterAndMatcher final : public ListenerFilterSetLogicMatcher {
public:
  ListenerFilterAndMatcher(
      absl::Span<const ::envoy::config::listener::v3::ListenerFilterChainMatchPredicate* const>
          predicates)
      : ListenerFilterSetLogicMatcher(predicates) {}
  bool matches(ListenerFilterCallbacks& cb) const override;
};

class ListenerFilterOrMatcher final : public ListenerFilterSetLogicMatcher {
public:
  ListenerFilterOrMatcher(
      absl::Span<const ::envoy::config::listener::v3::ListenerFilterChainMatchPredicate* const>
          predicates)
      : ListenerFilterSetLogicMatcher(predicates) {}
  bool matches(ListenerFilterCallbacks& cb) const override;
};

} // namespace Network
} // namespace Envoy
