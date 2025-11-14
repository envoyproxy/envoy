#pragma once

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/http/header_evaluator.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {

using ProtoHeaderMutatons =
    Protobuf::RepeatedPtrField<envoy::config::common::mutation_rules::v3::HeaderMutation>;
using ProtoHeaderValueOption = envoy::config::core::v3::HeaderValueOption;

class HeaderMutations : public HeaderEvaluator {
public:
  static absl::StatusOr<std::unique_ptr<HeaderMutations>>
  create(const ProtoHeaderMutatons& header_mutations,
         Server::Configuration::CommonFactoryContext& context,
         const Formatter::CommandParserPtrVector& command_parsers = {});

  // Http::HeaderEvaluator
  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context& context,
                       const StreamInfo::StreamInfo& stream_info) const override;

private:
  HeaderMutations(const ProtoHeaderMutatons& header_mutations,
                  Server::Configuration::CommonFactoryContext& context,
                  const Formatter::CommandParserPtrVector& command_parsers,
                  absl::Status& creation_status);

  std::vector<std::unique_ptr<HeaderEvaluator>> header_mutations_;
};

} // namespace Http
} // namespace Envoy
