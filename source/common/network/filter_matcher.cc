#include "common/network/filter_matcher.h"

#include "envoy/network/filter.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Network {

// Introduced to save number of memory allocation at build phase memory usage match phase.
// Work with OwnedListenerFilterMatcher.
// The first phase is constructor, requiring only 1 slots.
// The second phase is complete() which reserves contiguously slots.
class TwoPhaseMatcher : public ListenerFilterMatcher {
public:
  virtual void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) PURE;
};

namespace {
// Forward declare: Used by each concrete TwoPhaseMatcher
std::unique_ptr<TwoPhaseMatcher> createFromMessage(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config,
    std::vector<ListenerFilterMatcherPtr>& matchers);
} // namespace
struct TrueMatcher final : public TwoPhaseMatcher {
  bool matches(ListenerFilterCallbacks&) const override { return true; }
  void complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate&) override {
    // Nothing to do
  }
};
struct NotMatcher final : public TwoPhaseMatcher {
public:
  explicit NotMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    return !matchers_[sub_matcher_offset_]->matches(cb);
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub = createFromMessage(self.not_match(), matchers_);
    sub_matcher_offset_ = matchers_.size();
    auto ptr = sub.get();
    matchers_.emplace_back(std::move(sub));
    ptr->complete(self.not_match());
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint sub_matcher_offset_{0};
};

struct AndMatcher final : public TwoPhaseMatcher {
  AndMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    auto end = sub_matcher_offset_ + sub_matcher_len_;

    for (uint i = sub_matcher_offset_; i < end; i++) {
      if (!matchers_[i]->matches(cb)) {
        return false;
      }
    }
    return true;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (const auto& sub : self.and_match().rules()) {
      matchers_.emplace_back(createFromMessage(sub, matchers_));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(matchers_[i + sub_matcher_offset_].get());
      matcher->complete(self.and_match().rules(i));
    }
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint sub_matcher_offset_{0};
  uint sub_matcher_len_{0};
};

struct OrMatcher final : public TwoPhaseMatcher {
  OrMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    auto end = sub_matcher_offset_ + sub_matcher_len_;
    for (uint i = sub_matcher_offset_; i < end; i++) {
      if (matchers_[i]->matches(cb)) {
        return true;
      }
    }
    return false;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.or_match().rules_size();
    for (const auto& sub : self.or_match().rules()) {
      matchers_.emplace_back(createFromMessage(sub, matchers_));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(matchers_[i + sub_matcher_offset_].get());
      matcher->complete(self.or_match().rules(i));
    }
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint sub_matcher_offset_{0};
  uint sub_matcher_len_{0};
};

struct DstPortMatcher final : public TwoPhaseMatcher {
public:
  DstPortMatcher() = default;
  bool matches(ListenerFilterCallbacks& cb) const override {
    const auto& address = cb.socket().localAddress();
    // Match on destination port (only for IP addresses).
    if (address->type() == Address::Type::Ip) {
      const auto port = address->ip()->port();
      return start_ <= port && port < end_;
    } else {
      return true;
    }
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    ASSERT(self.rule_case() == envoy::config::listener::v3::ListenerFilterChainMatchPredicate::
                                   RuleCase::kDestinationPortRange);
    start_ = self.destination_port_range().start();
    end_ = self.destination_port_range().end();
  }

private:
  uint32_t start_;
  uint32_t end_;
};
namespace {
std::unique_ptr<TwoPhaseMatcher> createFromMessage(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config,
    std::vector<ListenerFilterMatcherPtr>& matchers) {
  switch (match_config.rule_case()) {
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAnyMatch:
    return std::make_unique<TrueMatcher>();
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kNotMatch: {
    return std::make_unique<NotMatcher>(matchers);
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAndMatch: {
    return std::make_unique<AndMatcher>(matchers);
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kOrMatch: {
    return std::make_unique<OrMatcher>(matchers);
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::
      kDestinationPortRange: {
    return std::make_unique<DstPortMatcher>();
  }
  // Invalid message.
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::RULE_NOT_SET: {
    throw EnvoyException(
        absl::StrFormat("invalid listener filter chain matcher: %s", match_config.DebugString()));
  }
  // Below could happen if the control plane provides deprecated api or newer api. Should be
  // considered as corrupted config.
  default:
    throw EnvoyException(absl::StrFormat("unsupported listener filter chain matcher: %s",
                                         match_config.DebugString()));
  }
}
} // namespace

OwnedListenerFilterMatcher::OwnedListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config) {
  auto uptr = createFromMessage(match_config, matchers_);
  auto& ref = *uptr;
  matchers_.push_back(std::move(uptr));
  ref.complete(match_config);
}

} // namespace Network
} // namespace Envoy