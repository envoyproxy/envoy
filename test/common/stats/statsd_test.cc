#include "common/stats/statsd.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Stats {
namespace Statsd {

class TcpStatsdSinkTest : public testing::Test {
public:
  TcpStatsdSinkTest() {
    EXPECT_CALL(cluster_manager_, get("statsd"));
    sink_.reset(new TcpStatsdSink("cluster", "host", "statsd", tls_, cluster_manager_));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Upstream::MockClusterManager cluster_manager_;
  std::unique_ptr<TcpStatsdSink> sink_;
};

TEST_F(TcpStatsdSinkTest, All) {
  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = connection;
  conn_info.host_.reset(new Upstream::HostImpl(
      Upstream::ClusterInfoPtr{new Upstream::MockClusterInfo}, "tcp://127.0.0.1:80", false, 1, ""));

  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("statsd")).WillOnce(Return(conn_info));
  EXPECT_CALL(*connection, connect());

  EXPECT_CALL(*connection, write(BufferStringEqual("envoy.test_counter:1|c\n")));
  sink_->flushCounter("test_counter", 1);

  EXPECT_CALL(*connection, write(BufferStringEqual("envoy.test_gauge:2|g\n")));
  sink_->flushGauge("test_gauge", 2);

  // Test a disconnect. We should connect again.
  connection->raiseEvents(Network::ConnectionEvent::RemoteClose);

  connection = new NiceMock<Network::MockClientConnection>();
  conn_info.connection_ = connection;
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("statsd")).WillOnce(Return(conn_info));
  EXPECT_CALL(*connection, connect());

  EXPECT_CALL(*connection, write(BufferStringEqual("envoy.test_timer:5|ms\n")));
  sink_->onTimespanComplete("test_timer", std::chrono::milliseconds(5));

  EXPECT_CALL(*connection, write(BufferStringEqual("envoy.histogram_test_timer:15|ms\n")));
  sink_->onHistogramComplete("histogram_test_timer", 15);

  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  tls_.shutdownThread();
}

} // Statsd
} // Stats
