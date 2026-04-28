// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/clusters/original_dst/original_dst_cluster.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/benchmark/main.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

class OriginalDstLbContext : public LoadBalancerContextBase {
public:
  OriginalDstLbContext(const Network::Connection* connection,
                       const Http::TestRequestHeaderMapImpl& headers)
      : connection_(connection),
        downstream_headers_(std::make_unique<Http::TestRequestHeaderMapImpl>(headers)) {}

  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  const Network::Connection* connection_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class OriginalDstTester : public BaseTester {
public:
  OriginalDstTester(uint64_t num_hosts) : BaseTester(0 /* original_dst manages its own hosts */) {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 1.250s
      type: ORIGINAL_DST
      lb_policy: CLUSTER_PROVIDED
      original_dst_lb_config:
        use_http_header: true
    )EOF";

    cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
    EXPECT_CALL(*cleanup_timer_, enableTimer(testing::_, testing::_));

    auto cluster_config = parseClusterFromV3Yaml(yaml);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);
    OriginalDstClusterFactory factory;
    auto status_or_pair = factory.createClusterImpl(cluster_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_pair.status());

    cluster_ = std::dynamic_pointer_cast<OriginalDstCluster>(status_or_pair.value().first);
    cluster_->initialize([]() { return absl::OkStatus(); });
    handle_ = std::make_shared<OriginalDstClusterHandle>(cluster_);

    populateHosts(num_hosts);
  }

  ~OriginalDstTester() {
    EXPECT_CALL(server_context_.dispatcher_, post(testing::_));
    EXPECT_CALL(*cleanup_timer_, disableTimer());
  }

  void populateHosts(uint64_t num_hosts) {
    for (uint64_t i = 0; i < num_hosts; i++) {
      NiceMock<Network::MockConnection> connection;
      connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
          std::make_shared<Network::Address::Ipv4Instance>(
              fmt::format("10.{}.{}.{}", (i >> 16) & 0xff, (i >> 8) & 0xff, i & 0xff),
              static_cast<uint32_t>(10000 + (i % 55535))));

      OriginalDstCluster::LoadBalancer lb(handle_);
      Http::TestRequestHeaderMapImpl headers{
          {"x-envoy-original-dst-host",
           fmt::format("10.{}.{}.{}:{}", (i >> 16) & 0xff, (i >> 8) & 0xff, i & 0xff,
                       10000 + (i % 55535))}};

      OriginalDstLbContext context(&connection, headers);

      Event::PostCb post_cb;
      EXPECT_CALL(server_context_.dispatcher_, post(testing::_))
          .WillOnce([&post_cb](Event::PostCb cb) { post_cb = std::move(cb); });
      lb.chooseHost(&context);
      post_cb();
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<OriginalDstCluster> cluster_;
  OriginalDstClusterHandleSharedPtr handle_;
  Event::MockTimer* cleanup_timer_;
};

void benchmarkOriginalDstChooseHostCacheHit(::benchmark::State& state) {
  const uint64_t num_hosts = state.range(0);
  OriginalDstTester tester(num_hosts);

  // Prepare a context that will hit an existing host.
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.0", 10000));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-original-dst-host", "10.0.0.0:10000"}};
  OriginalDstLbContext lb_context(&connection, headers);

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    OriginalDstCluster::LoadBalancer lb(tester.handle_);
    auto host = lb.chooseHost(&lb_context);
    ::benchmark::DoNotOptimize(host);
  }
}
BENCHMARK(benchmarkOriginalDstChooseHostCacheHit)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(::benchmark::kNanosecond);

void benchmarkOriginalDstChooseHostCacheMiss(::benchmark::State& state) {
  OriginalDstTester tester(100);

  uint64_t i = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    state.PauseTiming();
    // Each iteration uses a unique address to force a cache miss.
    auto addr = std::make_shared<Network::Address::Ipv4Instance>(
        fmt::format("192.{}.{}.{}", (i >> 16) & 0xff, (i >> 8) & 0xff, i & 0xff),
        static_cast<uint32_t>(20000 + (i % 45535)));
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(addr);

    Http::TestRequestHeaderMapImpl headers{{"x-envoy-original-dst-host", addr->asString()}};
    OriginalDstLbContext lb_context(&connection, headers);

    Event::PostCb post_cb;
    EXPECT_CALL(tester.server_context_.dispatcher_, post(testing::_))
        .WillOnce([&post_cb](Event::PostCb cb) { post_cb = std::move(cb); });
    state.ResumeTiming();

    OriginalDstCluster::LoadBalancer lb(tester.handle_);
    auto host = lb.chooseHost(&lb_context);
    ::benchmark::DoNotOptimize(host);

    state.PauseTiming();
    post_cb();
    i++;
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkOriginalDstChooseHostCacheMiss)->Unit(::benchmark::kNanosecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
