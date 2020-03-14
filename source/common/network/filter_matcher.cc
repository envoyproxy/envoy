#include "common/network/filter_matcher.h"

#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {
class TwoPhaseMatcher : public ListenerFilterMatcher {
public:
  virtual void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) PURE;
};
struct TrueMatcher : public TwoPhaseMatcher {
  bool matches(ListenerFilterCallbacks&) const override { return true; }
  void complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate&) override {
    // Nothing to do
  }
};
struct NotMatcher : public TwoPhaseMatcher {
public:
  explicit NotMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    return !matchers_[sub_matcher_offset_]->matches(cb);
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = matchers_.size();
    auto ptr = sub.get();
    matchers_.emplace_back(std::move(sub));
    ptr->complete(self.not_match());
  }

private:
  std::vector<ListenerFilterMatcherPtr>& matchers_;
  uint sub_matcher_offset_{0};
};

struct AndMatcher : public TwoPhaseMatcher {
  AndMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    for (uint i = sub_matcher_offset_; i < sub_matcher_offset_; i++) {
      if (!matchers_[i]->matches(cb)) {
        return false;
      }
    }
    return true;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matchers_.emplace_back(std::move(sub));
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

struct OrMatcher : public TwoPhaseMatcher {
  OrMatcher(std::vector<ListenerFilterMatcherPtr>& matchers) : matchers_(matchers) {}
  bool matches(ListenerFilterCallbacks& cb) const override {
    for (uint i = sub_matcher_offset_; i < sub_matcher_offset_; i++) {
      if (matchers_[i]->matches(cb)) {
        return true;
      }
    }
    return false;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matchers_.emplace_back(std::move(sub));
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

struct DstPortMatcher : public TwoPhaseMatcher {
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

OwnedListenerFilterMatcher::OwnedListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config) {
  matchers_.push_back(nullptr);
  switch (match_config.rule_case()) {
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAnyMatch:
    matchers_[0] = std::make_unique<TrueMatcher>();
    break;
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kNotMatch: {
    auto not_matcher = std::make_unique<NotMatcher>(matchers_);
    auto not_ptr = not_matcher.get();
    matchers_[0] = std::move(not_matcher);
    not_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAndMatch: {
    auto and_matcher = std::make_unique<AndMatcher>(matchers_);
    auto and_ptr = and_matcher.get();
    matchers_[0] = std::move(and_matcher);
    and_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kOrMatch: {
    auto or_matcher = std::make_unique<OrMatcher>(matchers_);
    auto or_ptr = or_matcher.get();
    matchers_[0] = std::move(or_matcher);
    or_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::
      kDestinationPortRange: {
    auto dst_port_matcher = std::make_unique<DstPortMatcher>();
    auto dst_port_ptr = dst_port_matcher.get();
    matchers_[0] = std::move(dst_port_matcher);
    dst_port_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::RULE_NOT_SET: {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  }
}

} // namespace Network
} // namespace Envoy