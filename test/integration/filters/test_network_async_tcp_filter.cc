#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats.h"
#include "envoy/tcp/async_tcp_client.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/tcp/async_tcp_client_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "test/integration/filters/test_network_async_tcp_filter.pb.h"
#include "test/integration/filters/test_network_async_tcp_filter.pb.validate.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace {

/**
 * All stats for this filter. @see stats_macros.h
 */
#define ALL_TEST_NETWORK_ASYNC_TCP_FILTER_STATS(COUNTER)                                           \
  COUNTER(on_new_connection)                                                                       \
  COUNTER(on_receive_async_data)                                                                   \
  COUNTER(on_data)

/**
 * Struct definition for stats. @see stats_macros.h
 */
struct TestNetworkAsyncTcpFilterStats {
  ALL_TEST_NETWORK_ASYNC_TCP_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * A trivial filter by creating a TCP client against an upstream cluster.
 * The upstream data is directly sent back to downstream.
 */
class TestNetworkAsyncTcpFilter : public Network::ReadFilter {
public:
  TestNetworkAsyncTcpFilter(
      const test::integration::filters::TestNetworkAsyncTcpFilterConfig& config,
      Stats::Scope& scope, Upstream::ClusterManager& cluster_manager)
      : stats_(generateStats("test_network_async_tcp_filter", scope)),
        cluster_name_(config.cluster_name()), cluster_manager_(cluster_manager) {
    const auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name_);
    options_ = std::make_shared<Tcp::AsyncTcpClientOptions>(true);
    if (thread_local_cluster != nullptr) {
      client_ = thread_local_cluster->tcpAsyncClient(nullptr, options_);
    }
  }

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    stats_.on_data_.inc();
    ENVOY_LOG_MISC(debug, "Downstream onData: {}, length: {} sending to upstream", data.toString(),
                   data.length());
    client_->write(data, end_stream);

    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override {
    stats_.on_new_connection_.inc();
    if (!client_->connect()) {
      ENVOY_LOG_MISC(debug, "There is no avaliable host in the cluster");
      return Network::FilterStatus::StopIteration;
    }
    request_callbacks_ = std::make_unique<RequestAsyncCallbacks>(*this);
    client_->setAsyncTcpClientCallbacks(*request_callbacks_);
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    downstream_callbacks_ = std::make_unique<DownstreamCallbacks>(*this);
    read_callbacks_->connection().enableHalfClose(true);
    read_callbacks_->connection().addConnectionCallbacks(*downstream_callbacks_);
  }

private:
  struct DownstreamCallbacks : public Envoy::Network::ConnectionCallbacks {
    explicit DownstreamCallbacks(TestNetworkAsyncTcpFilter& parent) : parent_(parent) {}
    ~DownstreamCallbacks() override = default;
    void onEvent(Network::ConnectionEvent event) override {
      ENVOY_LOG_MISC(debug, "tcp client test filter downstream callback onEvent: {}",
                     static_cast<int>(event));
      if (event != Network::ConnectionEvent::RemoteClose) {
        return;
      }

      ENVOY_LOG_MISC(debug, "tcp client test filter downstream detected close type: {}.",
                     static_cast<int>(parent_.read_callbacks_->connection().detectedCloseType()));

      if (parent_.read_callbacks_->connection().detectedCloseType() ==
          Network::DetectedCloseType::RemoteReset) {
        parent_.client_->close(Network::ConnectionCloseType::AbortReset);
      } else {
        parent_.client_->close(Network::ConnectionCloseType::NoFlush);
      }
    };

    void onAboveWriteBufferHighWatermark() override{};
    void onBelowWriteBufferLowWatermark() override{};

    TestNetworkAsyncTcpFilter& parent_;
  };

  struct RequestAsyncCallbacks : public Tcp::AsyncTcpClientCallbacks {
    RequestAsyncCallbacks(TestNetworkAsyncTcpFilter& parent) : parent_(parent) {}

    void onData(Buffer::Instance& data, bool end_stream) override {
      parent_.stats_.on_receive_async_data_.inc();
      ENVOY_LOG_MISC(debug, "async onData from peer: {}, length: {} back to down", data.toString(),
                     data.length());
      parent_.read_callbacks_->connection().write(data, end_stream);
      if (end_stream) {
        parent_.client_->close(Network::ConnectionCloseType::NoFlush);
      }
    }

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      ENVOY_LOG_MISC(debug, "tcp client test filter upstream callback onEvent: {}",
                     static_cast<int>(event));
      if (event != Network::ConnectionEvent::RemoteClose) {
        return;
      }

      ENVOY_LOG_MISC(debug, "tcp client test filter upstream detected close type: {}.",
                     static_cast<int>(parent_.client_->detectedCloseType()));

      if (parent_.client_->detectedCloseType() == Network::DetectedCloseType::RemoteReset) {
        parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::AbortReset);
      } else {
        parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      }
    };

    void onAboveWriteBufferHighWatermark() override {
      parent_.read_callbacks_->connection().readDisable(true);
    }
    void onBelowWriteBufferLowWatermark() override {
      parent_.read_callbacks_->connection().readDisable(false);
    }

    TestNetworkAsyncTcpFilter& parent_;
  };

  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  TestNetworkAsyncTcpFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return {ALL_TEST_NETWORK_ASYNC_TCP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  TestNetworkAsyncTcpFilterStats stats_;
  Tcp::AsyncTcpClientPtr client_;
  absl::string_view cluster_name_;
  std::unique_ptr<RequestAsyncCallbacks> request_callbacks_;
  std::unique_ptr<DownstreamCallbacks> downstream_callbacks_;
  Upstream::ClusterManager& cluster_manager_;
  Tcp::AsyncTcpClientOptionsConstSharedPtr options_;
};

class TestNetworkAsyncTcpFilterConfigFactory
    : public Extensions::NetworkFilters::Common::FactoryBase<
          test::integration::filters::TestNetworkAsyncTcpFilterConfig> {
public:
  TestNetworkAsyncTcpFilterConfigFactory()
      : Extensions::NetworkFilters::Common::FactoryBase<
            test::integration::filters::TestNetworkAsyncTcpFilterConfig>(
            "envoy.test.test_network_async_tcp_filter") {}

  std::string name() const override { return "envoy.test.test_network_async_tcp_filter"; }

  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::TestNetworkAsyncTcpFilterConfig& config,
      Server::Configuration::FactoryContext& context) override {
    return [config, &context](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<TestNetworkAsyncTcpFilter>(
          config, context.scope(), context.clusterManager()));
    };
  }
};

REGISTER_FACTORY(TestNetworkAsyncTcpFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace
} // namespace Envoy
