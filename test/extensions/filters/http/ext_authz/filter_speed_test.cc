// Changing the default behavior of ext_authz is generally not allowed. While you may add tests, you
// generally should not change or remove existing tests.

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

using testing::NiceMock;
using testing::Return;

// Measures one request through a filter whose route overrides the gRPC service. The argument is
// the number of per-route configs that the request merges. The stream is destroyed while the check
// is in flight.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PerRouteGrpcServiceRequest(benchmark::State& state) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
  TestUtility::loadFromYaml(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: ext_authz_server
  )EOF",
                            proto_config);
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_proto_config;
  TestUtility::loadFromYaml(R"EOF(
  check_settings:
    context_extensions:
      key: value
    grpc_service:
      envoy_grpc:
        cluster_name: per_route_ext_authz_server
      timeout: 1s
      retry_policy:
        retry_back_off:
          base_interval: 0.1s
          max_interval: 1s
        num_retries: 3
      initial_metadata:
      - key: x-metadata-1
        value: value-1
      - key: x-metadata-2
        value: value-2
      - key: x-metadata-3
        value: value-3
      - key: x-metadata-4
        value: value-4
  )EOF",
                            per_route_proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Stats::IsolatedStoreImpl stats;
  absl::Status creation_status;
  auto config = std::make_shared<FilterConfig>(proto_config, *stats.rootScope(), *stats.rootScope(),
                                               "", factory_context, creation_status);
  RELEASE_ASSERT(creation_status.ok(), "");
  const FilterConfigPerRoute per_route_config(per_route_proto_config, creation_status);
  RELEASE_ASSERT(creation_status.ok(), "");

  auto async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  NiceMock<Grpc::MockAsyncRequest> async_request;
  ON_CALL(*async_client,
          sendRaw(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(Return(&async_request));
  ON_CALL(factory_context.cluster_manager_.async_client_manager_,
          getOrCreateRawAsyncClientWithHashKey(testing::_, testing::_, testing::_))
      .WillByDefault(Return(Grpc::RawAsyncClientSharedPtr(async_client)));

  NiceMock<Network::MockConnection> connection;
  const auto address = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(address);
  connection.stream_info_.downstream_connection_info_provider_->setLocalAddress(address);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  ON_CALL(decoder_callbacks, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection}));
  ON_CALL(decoder_callbacks, perFilterConfigs())
      .WillByDefault(Return(Router::RouteSpecificFilterConfigs(state.range(0), &per_route_config)));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host"}};

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Filter filter(config, std::make_unique<NiceMock<Filters::Common::ExtAuthz::MockClient>>(),
                  factory_context);
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.decodeHeaders(request_headers, false);
    filter.onDestroy();
  }
}
BENCHMARK(BM_PerRouteGrpcServiceRequest)->Arg(1)->Arg(2);

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
