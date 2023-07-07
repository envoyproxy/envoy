#pragma once

#include <memory>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/grpc/async_client.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ext_authz::v3::ExtAuthz,
          envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute> {
public:
  ExtAuthzFilterConfig() : FactoryBase("envoy.filters.http.ext_authz") {}

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;

  class GetGrpcClient : public ThreadLocal::ThreadLocalObject {
  public:
    ::Envoy::Grpc::RawAsyncClientSharedPtr
    getCache(std::function<::Envoy::Grpc::RawAsyncClientSharedPtr()> cb) {
      if (!raw_async_client_) {
        raw_async_client_ = cb();
      }
      return raw_async_client_;
    }

  private:
    ::Envoy::Grpc::RawAsyncClientSharedPtr raw_async_client_;
  };
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
