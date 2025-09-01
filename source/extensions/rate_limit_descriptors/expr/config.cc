#include "source/extensions/rate_limit_descriptors/expr/config.h"

#include "envoy/extensions/rate_limit_descriptors/expr/v3/expr.pb.h"
#include "envoy/extensions/rate_limit_descriptors/expr/v3/expr.pb.validate.h"

#include "source/common/protobuf/utility.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace Expr {

namespace {

/**
 * Descriptor producer for a symbolic expression descriptor.
 */
class ExpressionDescriptor : public RateLimit::DescriptorProducer {
public:
  ExpressionDescriptor(
      const envoy::extensions::rate_limit_descriptors::expr::v3::Descriptor& config,
      Extensions::Filters::Common::Expr::CompiledExpression&& compiled_expr)
      : descriptor_key_(config.descriptor_key()), skip_if_error_(config.skip_if_error()),
        compiled_expr_(std::move(compiled_expr)) {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry, const std::string&,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override {
    Protobuf::Arena arena;
    const auto result = compiled_expr_.evaluate(arena, nullptr, info, &headers, nullptr, nullptr);
    if (!result.has_value() || result.value().IsError()) {
      // If result is an error and if skip_if_error is true skip this descriptor,
      // while calling rate limiting service. If skip_if_error is false, do not call rate limiting
      // service.
      return skip_if_error_;
    }
    descriptor_entry = {descriptor_key_, Filters::Common::Expr::print(result.value())};
    return true;
  }

private:
  const std::string descriptor_key_;
  const bool skip_if_error_;
  const Extensions::Filters::Common::Expr::CompiledExpression compiled_expr_;
};

} // namespace

std::string ExprDescriptorFactory::name() const { return "envoy.rate_limit_descriptors.expr"; }

ProtobufTypes::MessagePtr ExprDescriptorFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::rate_limit_descriptors::expr::v3::Descriptor>();
}

absl::StatusOr<RateLimit::DescriptorProducerPtr>
ExprDescriptorFactory::createDescriptorProducerFromProto(
    const Protobuf::Message& message, Server::Configuration::CommonFactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::rate_limit_descriptors::expr::v3::Descriptor&>(
      message, context.messageValidationVisitor());
  auto builder = Extensions::Filters::Common::Expr::getBuilder(context);
  switch (config.expr_specifier_case()) {
#if defined(USE_CEL_PARSER)
  case envoy::extensions::rate_limit_descriptors::expr::v3::Descriptor::kText: {
    auto parse_status = google::api::expr::parser::Parse(config.text());
    if (!parse_status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("Unable to parse descriptor expression: ",
                                                     parse_status.status().ToString()));
    }
    auto compiled_expr = Extensions::Filters::Common::Expr::CompiledExpression::Create(
        builder, parse_status.value().expr());
    if (!compiled_expr.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
    }
    return std::make_unique<ExpressionDescriptor>(config, std::move(compiled_expr.value()));
  }
#endif
  case envoy::extensions::rate_limit_descriptors::expr::v3::Descriptor::kParsed: {
    auto compiled_expr =
        Extensions::Filters::Common::Expr::CompiledExpression::Create(builder, config.parsed());
    if (!compiled_expr.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
    }
    return std::make_unique<ExpressionDescriptor>(config, std::move(compiled_expr.value()));
  }
  default:
    return absl::InvalidArgumentError(
        "Rate limit descriptor extension failed: expression specifier is not set");
  }
}

REGISTER_FACTORY(ExprDescriptorFactory, RateLimit::DescriptorProducerFactory);

} // namespace Expr
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy
