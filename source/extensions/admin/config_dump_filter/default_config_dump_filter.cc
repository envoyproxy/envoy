#include "source/extensions/admin/config_dump_filter/default_config_dump_filter.h"

#include "envoy/registry/registry.h"

#include "source/common/common/regex.h"

namespace Envoy {
namespace Extensions {
namespace ConfigDumpFilters {

namespace {
using envoy::type::matcher::v3::RegexMatcher;

Regex::CompiledMatcherPtr
buildNameRegexMatcher(const Server::Configuration::MatchingParameters& params) {
  if (const auto name_it = params.find("name"); name_it != params.end()) {
    envoy::type::matcher::v3::RegexMatcher matcher;
    *matcher.mutable_google_re2() = envoy::type::matcher::v3::RegexMatcher::GoogleRE2();
    matcher.set_regex(name_it->second);
    return Regex::Utility::parseRegex(matcher);
  }
  return nullptr;
}
} // namespace

DefaultConfigDumpFilter::DefaultConfigDumpFilter(
    const Server::Configuration::MatchingParameters& params)
    : name_regex_(buildNameRegexMatcher(params)) {}

bool DefaultConfigDumpFilter::match(const Protobuf::Message& message) const {
  if (name_regex_ == nullptr) {
    return true;
  }

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();

  if (descriptor == nullptr || reflection == nullptr)
    return false;

  const Protobuf::FieldDescriptor* name_descriptor = descriptor->FindFieldByName("name");
  if (name_descriptor == nullptr)
    return false;

  return name_regex_->match(reflection->GetString(message, name_descriptor));
}

Server::Configuration::ConfigDumpFilterPtr DefaultConfigDumpFilterFactory::createConfigDumpFilter(
    const Server::Configuration::MatchingParameters& params) const {
  return std::make_unique<DefaultConfigDumpFilter>(params);
}

static Envoy::Registry::RegisterFactory<DefaultConfigDumpFilterFactory,
                                        Server::Configuration::ConfigDumpFilterFactory>
    register_;
} // namespace ConfigDumpFilters
} // namespace Extensions
} // namespace Envoy