#pragma once

#include "envoy/extensions/request_id/uuid/v3/uuid.pb.h"
#include "envoy/extensions/request_id/uuid/v3/uuid.pb.validate.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/server/request_id_extension_config.h"

namespace Envoy {
namespace Extensions {
namespace RequestId {

// UUIDRequestIDExtension is the default implementation if no other extension is explicitly
// configured.
class UUIDRequestIDExtension : public Http::RequestIDExtension {
public:
  UUIDRequestIDExtension(const envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig& config,
                         Random::RandomGenerator& random)
      : random_(random),
        pack_trace_reason_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, pack_trace_reason, true)),
        use_request_id_for_trace_sampling_(
            PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_request_id_for_trace_sampling, true)) {}

  static Http::RequestIDExtensionSharedPtr defaultInstance(Random::RandomGenerator& random) {
    return std::make_shared<UUIDRequestIDExtension>(
        envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(), random);
  }

  bool packTraceReason() { return pack_trace_reason_; }

  // Http::RequestIDExtension
  void set(Http::RequestHeaderMap& request_headers, bool force) override;
  void setInResponse(Http::ResponseHeaderMap& response_headers,
                     const Http::RequestHeaderMap& request_headers) override;
  absl::optional<absl::string_view>
  get(const Http::RequestHeaderMap& request_headers) const override;
  absl::optional<uint64_t> getInteger(const Http::RequestHeaderMap& request_headers) const override;
  Tracing::Reason getTraceReason(const Http::RequestHeaderMap& request_headers) override;
  void setTraceReason(Http::RequestHeaderMap& request_headers, Tracing::Reason status) override;
  bool useRequestIdForTraceSampling() const override { return use_request_id_for_trace_sampling_; }

private:
  Envoy::Random::RandomGenerator& random_;
  const bool pack_trace_reason_;
  const bool use_request_id_for_trace_sampling_;

  // Byte on this position has predefined value of 4 for UUID4.
  static const int TRACE_BYTE_POSITION = 14;

  // Value of '9' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we sample trace.
  static const char TRACE_SAMPLED = '9';

  // Value of 'a' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we force trace.
  static const char TRACE_FORCED = 'a';

  // Value of 'b' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because of client trace.
  static const char TRACE_CLIENT = 'b';

  // Initial value for freshly generated uuid4.
  static const char NO_TRACE = '4';
};

// Factory for the UUID request ID extension.
class UUIDRequestIDExtensionFactory : public Server::Configuration::RequestIDExtensionFactory {
public:
  std::string name() const override { return "envoy.request_id.uuid"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig>();
  }
  Http::RequestIDExtensionSharedPtr
  createExtensionInstance(const Protobuf::Message& config,
                          Server::Configuration::FactoryContext& context) override {
    return std::make_shared<UUIDRequestIDExtension>(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig&>(
            config, context.messageValidationVisitor()),
        context.api().randomGenerator());
  }
};

} // namespace RequestId
} // namespace Extensions
} // namespace Envoy
