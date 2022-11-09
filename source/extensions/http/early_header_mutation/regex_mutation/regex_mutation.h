#pragma once

#include <vector>

#include "envoy/common/regex.h"
#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.validate.h"
#include "envoy/http/early_header_mutation.h"
#include "envoy/type/http/v3/header_mutation.pb.h"

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
    const Envoy::Http::LowerCaseString header_;
    const Envoy::Http::LowerCaseString rename_;
    Envoy::Regex::CompiledMatcherPtr regex_rewrite_{};
    std::string regex_rewrite_substitution_{};
  };

  RegexMutation(const ProtoHeaderMutations& mutations) {
    mutations_.reserve(mutations.size());
    for (const auto& proto_mutation : mutations) {
      mutations_.push_back(HeaderMutation{
          Envoy::Http::LowerCaseString(proto_mutation.header()),
          Envoy::Http::LowerCaseString(!proto_mutation.rename().empty() ? proto_mutation.rename()
                                                                        : proto_mutation.header()),
          Envoy::Regex::Utility::parseRegex(proto_mutation.regex_rewrite().pattern()),
          proto_mutation.regex_rewrite().substitution()});
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
