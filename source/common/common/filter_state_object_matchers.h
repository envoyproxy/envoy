#pragma once

#include <memory>

#include "envoy/common/matchers.h"
#include "envoy/common/pure.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Matchers {

class FilterStateObjectMatcher {
public:
  virtual bool match(const StreamInfo::FilterState::Object& object) const PURE;
  virtual ~FilterStateObjectMatcher() = default;
};

using FilterStateObjectMatcherPtr = std::unique_ptr<FilterStateObjectMatcher>;

class FilterStateIpRangeMatcher : public FilterStateObjectMatcher {
public:
  FilterStateIpRangeMatcher(std::unique_ptr<Network::Address::IpList>&& ip_list);
  bool match(const StreamInfo::FilterState::Object& object) const override;

private:
  std::unique_ptr<Envoy::Network::Address::IpList> ip_list_;
};

class FilterStateStringMatcher : public FilterStateObjectMatcher {
public:
  FilterStateStringMatcher(StringMatcherPtr&& string_matcher);
  bool match(const StreamInfo::FilterState::Object& object) const override;

private:
  const StringMatcherPtr string_matcher_;
};

} // namespace Matchers
} // namespace Envoy
