#pragma once

#include <vector>

#include "source/common/common/matchers.h"
#include "source/extensions/matching/http/metadata_input/meta_input.h"

#include "envoy/matcher/matcher.h"
#include "envoy/type/matcher/v3/value.pb.h"
#include "envoy/extensions/matching/input_matchers/metadata/v3/metadata.pb.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Metadata {

class Matcher : public Envoy::Matcher::InputMatcher, Logger::Loggable<Logger::Id::filter> {
public:
  Matcher(Envoy::Matchers::ValueMatcherConstSharedPtr, bool);
  bool match(const Envoy::Matcher::MatchingDataType& input) override;

private:
  Envoy::Matchers::ValueMatcherConstSharedPtr value_matcher_;
  bool invert_;
};

} // namespace Metadata
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
