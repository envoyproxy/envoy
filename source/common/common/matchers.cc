#include "source/common/common/matchers.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"
#include "envoy/type/matcher/v3/number.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/matcher/v3/value.pb.h"

#include "source/common/common/filter_state_object_matchers.h"
#include "source/common/common/macros.h"
#include "source/common/common/regex.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/http/path_utility.h"

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "filter_state_object_matchers.h"

namespace Envoy {
namespace Matchers {

ValueMatcherConstSharedPtr
ValueMatcher::create(const envoy::type::matcher::v3::ValueMatcher& v,
                     Server::Configuration::CommonFactoryContext& context) {
  switch (v.match_pattern_case()) {
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kNullMatch:
    return std::make_shared<const NullMatcher>();
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kDoubleMatch:
    return std::make_shared<const DoubleMatcher>(v.double_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kStringMatch:
    return std::make_shared<const StringMatcherImpl<std::decay_t<decltype(v.string_match())>>>(
        v.string_match(), context);
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kBoolMatch:
    return std::make_shared<const BoolMatcher>(v.bool_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kPresentMatch:
    return std::make_shared<const PresentMatcher>(v.present_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kListMatch:
    return std::make_shared<const ListMatcher>(v.list_match(), context);
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kOrMatch:
    return std::make_shared<const OrMatcher>(v.or_match(), context);
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::MATCH_PATTERN_NOT_SET:
    break; // Fall through to PANIC.
  }
  PANIC("unexpected");
}

bool NullMatcher::match(const ProtobufWkt::Value& value) const {
  return value.kind_case() == ProtobufWkt::Value::kNullValue;
}

bool BoolMatcher::match(const ProtobufWkt::Value& value) const {
  return value.kind_case() == ProtobufWkt::Value::kBoolValue && matcher_ == value.bool_value();
}

bool PresentMatcher::match(const ProtobufWkt::Value& value) const {
  return matcher_ && value.kind_case() != ProtobufWkt::Value::KIND_NOT_SET;
}

bool DoubleMatcher::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kNumberValue) {
    return false;
  }

  const double v = value.number_value();
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::v3::DoubleMatcher::MatchPatternCase::kRange:
    return matcher_.range().start() <= v && v < matcher_.range().end();
  case envoy::type::matcher::v3::DoubleMatcher::MatchPatternCase::kExact:
    return matcher_.exact() == v;
  case envoy::type::matcher::v3::DoubleMatcher::MatchPatternCase::MATCH_PATTERN_NOT_SET:
    break; // Fall through to PANIC.
  };
  PANIC("unexpected");
}

ListMatcher::ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher,
                         Server::Configuration::CommonFactoryContext& context) {
  ASSERT(matcher.match_pattern_case() ==
         envoy::type::matcher::v3::ListMatcher::MatchPatternCase::kOneOf);

  oneof_value_matcher_ = ValueMatcher::create(matcher.one_of(), context);
}

bool ListMatcher::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kListValue) {
    return false;
  }

  if (oneof_value_matcher_) {
    for (const auto& lv : value.list_value().values()) {
      if (oneof_value_matcher_->match(lv)) {
        return true;
      }
    }
  }
  return false;
}

OrMatcher::OrMatcher(const envoy::type::matcher::v3::OrMatcher& matcher,
                     Server::Configuration::CommonFactoryContext& context) {
  or_matchers_.reserve(matcher.value_matchers().size());
  for (const auto& or_matcher : matcher.value_matchers()) {
    or_matchers_.push_back(ValueMatcher::create(or_matcher, context));
  }
}

bool OrMatcher::match(const ProtobufWkt::Value& value) const {
  for (const auto& or_matcher : or_matchers_) {
    if (or_matcher->match(value)) {
      return true;
    }
  }
  return false;
}

MetadataMatcher::MetadataMatcher(const envoy::type::matcher::v3::MetadataMatcher& matcher,
                                 Server::Configuration::CommonFactoryContext& context)
    : matcher_(matcher) {
  for (const auto& seg : matcher.path()) {
    path_.push_back(seg.key());
  }
  const auto& v = matcher_.value();
  value_matcher_ = ValueMatcher::create(v, context);
}

namespace {

absl::StatusOr<FilterStateObjectMatcherPtr>
filterStateObjectMatcherFromProto(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
                                  Server::Configuration::CommonFactoryContext& context) {
  switch (matcher.matcher_case()) {
  case envoy::type::matcher::v3::FilterStateMatcher::MatcherCase::kStringMatch:
    return std::make_unique<FilterStateStringMatcher>(
        std::make_unique<const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
            matcher.string_match(), context));
    break;
  case envoy::type::matcher::v3::FilterStateMatcher::MatcherCase::kAddressMatch: {
    auto ip_list = Network::Address::IpList::create(matcher.address_match().ranges());
    RETURN_IF_NOT_OK_REF(ip_list.status());
    return std::make_unique<FilterStateIpRangeMatcher>(std::move(*ip_list));
    break;
  }
  default:
    PANIC_DUE_TO_PROTO_UNSET;
  }
}

} // namespace

absl::StatusOr<FilterStateMatcherPtr>
FilterStateMatcher::create(const envoy::type::matcher::v3::FilterStateMatcher& config,
                           Server::Configuration::CommonFactoryContext& context) {
  absl::StatusOr<FilterStateObjectMatcherPtr> matcher =
      filterStateObjectMatcherFromProto(config, context);
  if (!matcher.ok()) {
    return matcher.status();
  }
  return std::make_unique<FilterStateMatcher>(config.key(), std::move(*matcher));
}

FilterStateMatcher::FilterStateMatcher(std::string key,
                                       FilterStateObjectMatcherPtr&& object_matcher)
    : key_(key), object_matcher_(std::move(object_matcher)) {}

bool FilterStateMatcher::match(const StreamInfo::FilterState& filter_state) const {
  const auto* object = filter_state.getDataReadOnlyGeneric(key_);
  if (object == nullptr) {
    return false;
  }
  return object_matcher_->match(*object);
}

PathMatcherConstSharedPtr
PathMatcher::createExact(const std::string& exact, bool ignore_case,
                         Server::Configuration::CommonFactoryContext& context) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact(exact);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher, context);
}

namespace {
PathMatcherConstSharedPtr
createPrefixPathMatcher(const std::string& prefix, bool ignore_case,
                        Server::Configuration::CommonFactoryContext& context) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix(prefix);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher, context);
}

} // namespace

PathMatcherConstSharedPtr
PathMatcher::createPrefix(const std::string& prefix, bool ignore_case,
                          Server::Configuration::CommonFactoryContext& context) {
  // "" and "/" prefixes are the most common among prefix path matchers (as they effectively
  // represent "match any path" cases). They are optimized by using the same shared instances of the
  // matchers, to avoid creating a lot of identical instances of those trivial matchers.
  if (prefix.empty()) {
    static const PathMatcherConstSharedPtr emptyPrefixPathMatcher =
        createPrefixPathMatcher("", false, context);
    return emptyPrefixPathMatcher;
  }
  if (prefix == "/") {
    static const PathMatcherConstSharedPtr slashPrefixPathMatcher =
        createPrefixPathMatcher("/", false, context);
    return slashPrefixPathMatcher;
  }
  return createPrefixPathMatcher(prefix, ignore_case, context);
}

PathMatcherConstSharedPtr
PathMatcher::createSafeRegex(const envoy::type::matcher::v3::RegexMatcher& regex_matcher,
                             Server::Configuration::CommonFactoryContext& context) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.mutable_safe_regex()->MergeFrom(regex_matcher);
  return std::make_shared<const PathMatcher>(matcher, context);
}

bool MetadataMatcher::match(const envoy::config::core::v3::Metadata& metadata) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(&metadata, matcher_.filter(), path_);
  return value_matcher_->match(value) ^ matcher_.invert();
}

bool PathMatcher::match(const absl::string_view path) const {
  return matcher_.match(Http::PathUtil::removeQueryAndFragment(path));
}

StringMatcherPtr getExtensionStringMatcher(const ::xds::core::v3::TypedExtensionConfig& config,
                                           Server::Configuration::CommonFactoryContext& context) {
  auto factory = Config::Utility::getAndCheckFactory<StringMatcherExtensionFactory>(config, false);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), *factory);
  return factory->createStringMatcher(*message, context);
}

} // namespace Matchers
} // namespace Envoy
