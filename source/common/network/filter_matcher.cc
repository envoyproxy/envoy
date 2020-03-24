#include "common/network/filter_matcher.h"

#include "envoy/network/filter.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Network {

// TwoPhaseMatcher is introduced to save number of memory allocation at build phase memory usage
// match phase. Work with SetListenerFilterMatcher. The first phase is constructor, and the second
// phase is finishBuild(). The invariance is that the direct matchers in a set logic matcher should
// be continuous in the vector.
//
// Examples:
//  `m := A && B && C` where `B := X || Y`
// The 3 matchers "ABC" should be continuous, while the matchers "XY" should be continuous in the
// storage of SetListenerFilterMatcher.
class TwoPhaseMatcher : public ListenerFilterMatcher {
public:
  // Construct the matcher. The owning matcher should put this in a vector.
  TwoPhaseMatcher() = default;
  // Reserve a continuous spaces in the vector for the direct sub matchers and then recursively
  // build the sub matchers.
  virtual void
  finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) PURE;
};

namespace {
// Forward declare: Used by each concrete TwoPhaseMatcher
std::unique_ptr<TwoPhaseMatcher> createFromMessage(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config,
    std::vector<ListenerFilterMatcherPtr>& matchers);
} // namespace
class TrueMatcher final : public TwoPhaseMatcher {
public:
  bool matches(ListenerFilterCallbacks&) const override { return true; }
  void finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate&) override {
    // Nothing to do
  }
};
class NotMatcher final : public TwoPhaseMatcher {
public:
  explicit NotMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    return !matchers_[sub_matcher_offset_]->matches(cb);
  }
  void
  finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub = createFromMessage(self.not_match(), matchers_);
    sub_matcher_offset_ = matchers_.size();
    auto ptr = sub.get();
    matchers_.emplace_back(std::move(sub));
    ptr->finishBuild(self.not_match());
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint32_t sub_matcher_offset_{0};
};

class AndMatcher final : public TwoPhaseMatcher {
public:
  AndMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    auto end = sub_matcher_offset_ + sub_matcher_len_;

    for (uint32_t i = sub_matcher_offset_; i < end; i++) {
      if (!matchers_[i]->matches(cb)) {
        return false;
      }
    }
    return true;
  }
  void
  finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (const auto& sub : self.and_match().rules()) {
      matchers_.emplace_back(createFromMessage(sub, matchers_));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint32_t i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(matchers_[i + sub_matcher_offset_].get());
      matcher->finishBuild(self.and_match().rules(i));
    }
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint32_t sub_matcher_offset_{0};
  uint32_t sub_matcher_len_{0};
};

class OrMatcher final : public TwoPhaseMatcher {
public:
  OrMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    auto end = sub_matcher_offset_ + sub_matcher_len_;
    for (uint32_t i = sub_matcher_offset_; i < end; i++) {
      if (matchers_[i]->matches(cb)) {
        return true;
      }
    }
    return false;
  }
  void
  finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.or_match().rules_size();
    for (const auto& sub : self.or_match().rules()) {
      matchers_.emplace_back(createFromMessage(sub, matchers_));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint32_t i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(matchers_[i + sub_matcher_offset_].get());
      matcher->finishBuild(self.or_match().rules(i));
    }
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint32_t sub_matcher_offset_{0};
  uint32_t sub_matcher_len_{0};
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
  finishBuild(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
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
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}
} // namespace

SetListenerFilterMatcher::SetListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config) {
  auto uptr = createFromMessage(match_config, matchers_);
  auto& ref = *uptr;
  matchers_.push_back(std::move(uptr));
  ref.finishBuild(match_config);
}

} // namespace Network
} // namespace Envoy