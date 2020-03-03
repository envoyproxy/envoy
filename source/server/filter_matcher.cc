#include "server/filter_matcher.h"

#include "envoy/network/filter.h"

namespace Envoy {
namespace Server {
namespace Listener {
class TwoPhaseMatcher : public ListenerFilterMatcher {
public:
  virtual void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) PURE;
};
struct TrueMatcher : public TwoPhaseMatcher {
  bool matches(Network::ListenerFilterCallbacks&) const override { return true; }
  void complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate&) override {
    // Nothing to do
  }
};
struct NotMatcher : public TwoPhaseMatcher {
public:
  explicit NotMatcher(std::vector<Server::ListenerFilterMatcherPtr>& all_matchers)
      : all_matchers_(all_matchers) {}
  bool matches(Network::ListenerFilterCallbacks& cb) const override {
    return !all_matchers_[sub_matcher_offset_]->matches(cb);
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = all_matchers_.size();
    auto ptr = sub.get();
    all_matchers_.emplace_back(std::move(sub));
    ptr->complete(self.not_match());
  }

private:
  std::vector<Server::ListenerFilterMatcherPtr>& all_matchers_;
  uint sub_matcher_offset_{0};
};

struct AndMatcher : public TwoPhaseMatcher {
  AndMatcher(std::vector<Server::ListenerFilterMatcherPtr>& all_matchers)
      : all_matchers_(all_matchers) {}
  bool matches(Network::ListenerFilterCallbacks& cb) const override {
    for (uint i = sub_matcher_offset_; i < sub_matcher_offset_; i++) {
      if (!all_matchers_[i]->matches(cb)) {
        return false;
      }
    }
    return true;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = all_matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (uint i = 0; i < sub_matcher_len_; i++) {
      all_matchers_.emplace_back(std::move(sub));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(all_matchers_[i + sub_matcher_offset_].get());
      matcher->complete(self.and_match().rules(i));
    }
  }

private:
  std::vector<Server::ListenerFilterMatcherPtr>& all_matchers_;
  uint sub_matcher_offset_{0};
  uint sub_matcher_len_{0};
};

struct OrMatcher : public TwoPhaseMatcher {
  OrMatcher(std::vector<Server::ListenerFilterMatcherPtr>& all_matchers)
      : all_matchers_(all_matchers) {}
  bool matches(Network::ListenerFilterCallbacks& cb) const override {
    for (uint i = sub_matcher_offset_; i < sub_matcher_offset_; i++) {
      if (all_matchers_[i]->matches(cb)) {
        return true;
      }
    }
    return false;
  }
  void
  complete(const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& self) override {
    std::unique_ptr<TwoPhaseMatcher> sub;
    sub_matcher_offset_ = all_matchers_.size();
    sub_matcher_len_ = self.and_match().rules_size();
    for (uint i = 0; i < sub_matcher_len_; i++) {
      all_matchers_.emplace_back(std::move(sub));
    }
    TwoPhaseMatcher* matcher{nullptr};
    for (uint i = 0; i < sub_matcher_len_; i++) {
      matcher = static_cast<TwoPhaseMatcher*>(all_matchers_[i + sub_matcher_offset_].get());
      matcher->complete(self.and_match().rules(i));
    }
  }

private:
  std::vector<Server::ListenerFilterMatcherPtr>& all_matchers_;
  uint sub_matcher_offset_{0};
  uint sub_matcher_len_{0};
};

struct DstPortMatcher : public TwoPhaseMatcher {
public:
  explicit DstPortMatcher(std::vector<Server::ListenerFilterMatcherPtr>&) {}
  bool matches(Network::ListenerFilterCallbacks& cb) const override {
    const auto& address = cb.socket().localAddress();

    // Match on destination port (only for IP addresses).
    if (address->type() == Network::Address::Type::Ip) {
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

Server::ListenerFilterMatcherPtr buildListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config) {
  std::vector<Server::ListenerFilterMatcherPtr> all_matchers(1);
  switch (match_config.rule_case()) {
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAnyMatch:
    all_matchers[0] = std::make_unique<TrueMatcher>();
    break;
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kNotMatch: {
    auto not_matcher = std::make_unique<NotMatcher>(all_matchers);
    auto not_ptr = not_matcher.get();
    all_matchers[0] = std::move(not_matcher);
    not_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAndMatch: {
    auto and_matcher = std::make_unique<AndMatcher>(all_matchers);
    auto and_ptr = and_matcher.get();
    all_matchers[0] = std::move(and_matcher);
    and_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kOrMatch: {
    auto or_matcher = std::make_unique<OrMatcher>(all_matchers);
    auto or_ptr = or_matcher.get();
    all_matchers[0] = std::move(or_matcher);
    or_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::
      kDestinationPortRange: {
    auto dst_port_matcher = std::make_unique<DstPortMatcher>(all_matchers);
    auto dst_port_ptr = dst_port_matcher.get();
    all_matchers[0] = std::move(dst_port_matcher);
    dst_port_ptr->complete(match_config);
    break;
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::RULE_NOT_SET: {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  }
  return std::make_unique<Server::OwnedListenerFilterMatcher>(std::move(all_matchers));
}
} // namespace Listener
} // namespace Server
} // namespace Envoy