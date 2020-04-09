#include "common/http/request_id_extension_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/request_id_extension_uuid_impl.h"

namespace Envoy {
namespace Http {

namespace {

// NoopRequestIDExtension is the implementation used outside of HTTP context.
class NoopRequestIDExtension : public RequestIDExtension {
public:
  void set(RequestHeaderMap&, bool) override {}
  void setInResponse(ResponseHeaderMap&, const RequestHeaderMap&) override {}
  bool modBy(const RequestHeaderMap&, uint64_t&, uint64_t) override { return false; }
  TraceStatus getTraceStatus(const RequestHeaderMap&) override { return TraceStatus::NoTrace; }
  void setTraceStatus(RequestHeaderMap&, TraceStatus) override {}
};

} // namespace

RequestIDExtensionSharedPtr RequestIDExtensionFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
        config,
    Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::RequestIDExtensionFactory>::getFactoryByType(
          type);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), context.messageValidationVisitor(), *factory);
  return factory->createExtensionInstance(*message, context);
}

RequestIDExtensionSharedPtr
RequestIDExtensionFactory::defaultInstance(Envoy::Runtime::RandomGenerator& random) {
  return std::make_shared<UUIDRequestIDExtension>(random);
}

RequestIDExtensionSharedPtr RequestIDExtensionFactory::noopInstance() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(std::shared_ptr<RequestIDExtension>,
                                 std::make_shared<NoopRequestIDExtension>());
}

} // namespace Http
} // namespace Envoy
