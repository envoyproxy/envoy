#pragma once

#include <vector>

#include "envoy/common/regex.h"
#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.validate.h"
#include "envoy/http/early_header_mutation.h"

#include "source/common/common/regex.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {

using ProtoHeaderMutations =
    Protobuf::RepeatedPtrField<envoy::config::common::mutation_rules::v3::HeaderMutation>;

class RegexMutation : public Envoy::Http::EarlyHeaderMutation {
public:
  struct HeaderMutation {
    HeaderMutation(const envoy::config::common::mutation_rules::v3::HeaderMutation& config)
        : header_(config.header()),
          rename_(config.rename().empty() ? config.header() : config.rename()),
          regex_rewrite_substitution_(config.regex_rewrite().substitution()) {
      regex_rewrite_ = Envoy::Regex::Utility::parseRegex(config.regex_rewrite().pattern());
    }

    const Envoy::Http::LowerCaseString header_;
    const Envoy::Http::LowerCaseString rename_;
    Envoy::Regex::CompiledMatcherPtr regex_rewrite_{};
    const std::string regex_rewrite_substitution_{};
  };

  RegexMutation(const ProtoHeaderMutations& mutations) {
    mutations_.reserve(mutations.size());
    for (const auto& proto_mutation : mutations) {
      mutations_.push_back(proto_mutation);
    }
  }
  bool mutate(Envoy::Http::RequestHeaderMap& headers) const override;

private:
  std::vector<HeaderMutation> mutations_;
};

} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
