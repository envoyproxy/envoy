#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/http/conn_manager_config.h"
#include "source/common/http/forward_client_cert.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

/**
 * Action that returns ForwardClientCertConfig when matched.
 * Implements Http::ForwardClientCertActionConfig so the conn_manager_utility can access it.
 */
class ForwardClientCertAction
    : public Matcher::ActionBase<envoy::extensions::filters::network::http_connection_manager::v3::
                                     HttpConnectionManager::ForwardClientCertConfig,
                                 Http::ForwardClientCertActionConfig> {
public:
  ForwardClientCertAction(Http::ForwardClientCertType forward_type,
                          std::vector<Http::ClientCertDetailsType> details)
      : forward_client_cert_type_(forward_type),
        set_current_client_cert_details_(std::move(details)) {}

  Http::ForwardClientCertType forwardClientCertType() const override {
    return forward_client_cert_type_;
  }

  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }

private:
  const Http::ForwardClientCertType forward_client_cert_type_;
  const std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
};

/**
 * Context for the ForwardClientCertActionFactory.
 */
struct ForwardClientCertActionFactoryContext {
  Server::Configuration::ServerFactoryContext& server_factory_context_;
};

/**
 * Factory for creating ForwardClientCertAction from proto config.
 */
class ForwardClientCertActionFactory
    : public Matcher::ActionFactory<ForwardClientCertActionFactoryContext> {
public:
  std::string name() const override { return "forward_client_cert"; }

  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message& config,
                                             ForwardClientCertActionFactoryContext&,
                                             ProtobufMessage::ValidationVisitor&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::ForwardClientCertConfig>();
  }

  std::string category() const override { return "envoy.http.forward_client_cert_details"; }
};

DECLARE_FACTORY(ForwardClientCertActionFactory);

/**
 * Validation visitor for the forward client cert matcher.
 */
class ForwardClientCertMatcherValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

/**
 * Helper to create the forward client cert matcher from proto config.
 */
Matcher::MatchTreePtr<Http::HttpMatchingData>
createForwardClientCertMatcher(const xds::type::matcher::v3::Matcher& matcher_config,
                               Server::Configuration::ServerFactoryContext& factory_context);

/**
 * Convert proto ForwardClientCertDetails enum to Http::ForwardClientCertType.
 */
Http::ForwardClientCertType
convertForwardClientCertDetailsType(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::ForwardClientCertDetails type);

/**
 * Convert proto SetCurrentClientCertDetails to vector of Http::ClientCertDetailsType.
 */
std::vector<Http::ClientCertDetailsType> convertSetCurrentClientCertDetails(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        SetCurrentClientCertDetails& details);

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
