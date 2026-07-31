#include "source/extensions/rate_limit_descriptors/jwt_claim/config.h"

#include "envoy/extensions/rate_limit_descriptors/jwt_claim/v3/jwt_claim.pb.h"
#include "envoy/extensions/rate_limit_descriptors/jwt_claim/v3/jwt_claim.pb.validate.h"

#include "source/common/jwt/jwt.h"
#include "source/common/jwt/struct_utils.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace JwtClaim {

namespace {

// Converts a claim value to a string, mirroring the conversion performed by
// the jwt_authn filter's JwtClaimToHeader. Only string claims are supported;
// all other kinds are treated as absent.
std::optional<std::string> claimValueAsString(const Protobuf::Value& value) {
  if (value.kind_case() != Protobuf::Value::kStringValue) {
    return std::nullopt;
  }
  return value.string_value();
}

} // namespace

/**
 * Descriptor producer that extracts a named (possibly nested) claim from an
 * unverified JWT found in an HTTP header.
 *
 * SECURITY WARNING: this producer does not verify the JWT signature. See the
 * warning in jwt_claim.proto for details.
 */
class JwtClaimDescriptor : public RateLimit::DescriptorProducer {
public:
  explicit JwtClaimDescriptor(
      const envoy::extensions::rate_limit_descriptors::jwt_claim::v3::Descriptor& config)
      : descriptor_key_(config.descriptor_key()),
        header_name_(Http::LowerCaseString(config.header_name())),
        value_prefix_(config.value_prefix()), claim_name_(config.claim_name()),
        default_value_(config.default_value()), skip_if_absent_(config.skip_if_absent()) {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry, const std::string&,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo&) const override {
    std::optional<std::string> claim_value = extractClaimValue(headers);
    if (claim_value.has_value()) {
      descriptor_entry = {descriptor_key_, std::move(claim_value.value())};
      return true;
    }
    if (!default_value_.empty()) {
      descriptor_entry = {descriptor_key_, default_value_};
      return true;
    }
    return skip_if_absent_;
  }

private:
  std::optional<std::string> extractClaimValue(const Http::RequestHeaderMap& headers) const {
    const auto header_value = headers.get(header_name_);
    if (header_value.empty()) {
      return std::nullopt;
    }
    absl::string_view value = header_value[0]->value().getStringView();
    if (!value_prefix_.empty()) {
      if (!absl::StartsWith(value, value_prefix_)) {
        return std::nullopt;
      }
      value.remove_prefix(value_prefix_.size());
    }

    JwtVerify::Jwt jwt;
    if (jwt.parseFromString(std::string(value)) != JwtVerify::Status::Ok) {
      return std::nullopt;
    }

    JwtVerify::StructUtils payload_getter(jwt.payload_pb_);
    const Protobuf::Value* found;
    if (payload_getter.GetValue(claim_name_, found) != JwtVerify::StructUtils::OK) {
      return std::nullopt;
    }
    return claimValueAsString(*found);
  }

  const std::string descriptor_key_;
  const Http::LowerCaseString header_name_;
  const std::string value_prefix_;
  const std::string claim_name_;
  const std::string default_value_;
  const bool skip_if_absent_;
};

std::string JwtClaimDescriptorFactory::name() const {
  return "envoy.rate_limit_descriptors.jwt_claim";
}

ProtobufTypes::MessagePtr JwtClaimDescriptorFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::rate_limit_descriptors::jwt_claim::v3::Descriptor>();
}

absl::StatusOr<RateLimit::DescriptorProducerPtr>
JwtClaimDescriptorFactory::createDescriptorProducerFromProto(
    const Protobuf::Message& message, Server::Configuration::CommonFactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::rate_limit_descriptors::jwt_claim::v3::Descriptor&>(
      message, context.messageValidationVisitor());
  return std::make_unique<JwtClaimDescriptor>(config);
}

REGISTER_FACTORY(JwtClaimDescriptorFactory, RateLimit::DescriptorProducerFactory);

} // namespace JwtClaim
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy
