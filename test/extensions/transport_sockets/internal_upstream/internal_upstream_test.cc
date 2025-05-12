#include "envoy/common/hashable.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.validate.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/internal_upstream/config.h"
#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

namespace {

using IoSocket::UserSpace::IoHandle;
using IoSocket::UserSpace::PassthroughState;

class TestObject : public StreamInfo::FilterState::Object {};

class MockUserSpaceIoHandle : public Network::MockIoHandle, public IoHandle {
public:
  MOCK_METHOD(void, setWriteEnd, ());
  MOCK_METHOD(bool, isPeerShutDownWrite, (), (const));
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, setNewDataAvailable, ());
  MOCK_METHOD(Buffer::Instance*, getWriteBuffer, ());
  MOCK_METHOD(bool, isWritable, (), (const));
  MOCK_METHOD(bool, isPeerWritable, (), (const));
  MOCK_METHOD(void, onPeerBufferLowWatermark, ());
  MOCK_METHOD(bool, isReadable, (), (const));
  MOCK_METHOD(std::shared_ptr<PassthroughState>, passthroughState, ());
};

class MockPassthroughState : public PassthroughState {
public:
  MOCK_METHOD(void, initialize,
              (std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
               const StreamInfo::FilterState::Objects& filter_state_objects));
  MOCK_METHOD(void, mergeInto,
              (envoy::config::core::v3::Metadata & metadata,
               StreamInfo::FilterState& filter_state));
};

class InternalSocketTest : public testing::Test {
public:
  InternalSocketTest() : metadata_(std::make_unique<envoy::config::core::v3::Metadata>()) {}

  void initialize(Network::IoHandle& io_handle) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket_ = std::make_unique<InternalSocket>(std::move(inner_socket), std::move(metadata_),
                                               filter_state_objects_);
    EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(_));
    socket_->setTransportSocketCallbacks(transport_callbacks_);
  }

  std::unique_ptr<envoy::config::core::v3::Metadata> metadata_;
  StreamInfo::FilterState::Objects filter_state_objects_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<InternalSocket> socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
};

// Test that the internal transport socket works for the regular sockets.
TEST_F(InternalSocketTest, NativeSocket) {
  NiceMock<Network::MockIoHandle> io_handle;
  initialize(io_handle);
}

// Test that internal transport socket updates the passthrough state for the user space sockets.
TEST_F(InternalSocketTest, PassthroughStateInjected) {
  auto filter_state_object = std::make_shared<TestObject>();
  filter_state_objects_.push_back(
      {filter_state_object, StreamInfo::FilterState::StateType::ReadOnly,
       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection, "test.object"});
  ProtobufWkt::Struct& map = (*metadata_->mutable_filter_metadata())["envoy.test"];
  ProtobufWkt::Value val;
  val.set_string_value("val");
  (*map.mutable_fields())["key"] = val;

  auto state = std::make_shared<NiceMock<MockPassthroughState>>();
  NiceMock<MockUserSpaceIoHandle> io_handle;
  EXPECT_CALL(io_handle, passthroughState()).WillRepeatedly(testing::Return(state));
  EXPECT_CALL(*state, initialize(_, _))
      .WillOnce(Invoke([&](std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                           const StreamInfo::FilterState::Objects& filter_state_objects) -> void {
        ASSERT_EQ("val",
                  metadata->filter_metadata().at("envoy.test").fields().at("key").string_value());
        ASSERT_EQ(1, filter_state_objects.size());
        const auto& object = filter_state_objects.at(0);
        ASSERT_EQ("test.object", object.name_);
        ASSERT_EQ(filter_state_object.get(), object.data_.get());
      }));
  initialize(io_handle);
}

TEST_F(InternalSocketTest, EmptyPassthroughState) {
  metadata_ = nullptr;
  auto state = std::make_shared<NiceMock<MockPassthroughState>>();
  NiceMock<MockUserSpaceIoHandle> io_handle;
  EXPECT_CALL(io_handle, passthroughState()).WillRepeatedly(testing::Return(state));
  EXPECT_CALL(*state, initialize(_, _))
      .WillOnce(Invoke([&](std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                           const StreamInfo::FilterState::Objects& filter_state_objects) -> void {
        ASSERT_EQ(nullptr, metadata);
        ASSERT_EQ(0, filter_state_objects.size());
      }));
  initialize(io_handle);
}

class ConfigTest : public testing::Test {
public:
  ConfigTest()
      : host_(std::make_shared<NiceMock<Upstream::MockHostDescription>>()),
        host_metadata_(std::make_shared<envoy::config::core::v3::Metadata>()) {
    ON_CALL(*host_, metadata()).WillByDefault(testing::Return(host_metadata_));
  }

protected:
  void initialize() {
    config_ = std::make_unique<Config>(config_proto_, *stats_store_.rootScope());
  }
  envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport
      config_proto_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  std::unique_ptr<Config> config_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_;
  std::shared_ptr<envoy::config::core::v3::Metadata> host_metadata_;
};

constexpr absl::string_view SampleConfig = R"EOF(
  passthrough_metadata:
  - kind: { host: {}}
    name: host.metadata
  - kind: { cluster: {}}
    name: cluster.metadata
  transport_socket:
    name: raw_buffer
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF";

TEST_F(ConfigTest, Basic) {
  TestUtility::loadFromYaml(std::string(SampleConfig), config_proto_);
  initialize();

  TestUtility::loadFromYaml(R"EOF(
  filter_metadata:
    cluster.metadata:
      key0: value0
    other.metadata:
      key2: value2
  )EOF",
                            host_->cluster_.metadata_);
  TestUtility::loadFromYaml(R"EOF(
  filter_metadata:
    host.metadata:
      key1: value1
    other.metadata:
      key3: value3
  )EOF",
                            *host_metadata_);
  auto metadata = config_->extractMetadata(host_);
  envoy::config::core::v3::Metadata expected;
  TestUtility::loadFromYaml(R"EOF(
  filter_metadata:
    host.metadata:
      key1: value1
    cluster.metadata:
      key0: value0
  )EOF",
                            expected);
  EXPECT_THAT(*metadata, ProtoEq(expected));

  EXPECT_EQ(0, stats_store_.counter("internal_upstream.no_metadata").value());
}

TEST_F(ConfigTest, UnsupportedMetadata) {
  TestUtility::loadFromYaml(R"EOF(
  passthrough_metadata:
  - kind: { route: {}}
    name: route.metadata
  transport_socket:
    name: raw_buffer
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
  )EOF",
                            config_proto_);
  EXPECT_THROW_WITH_REGEX(initialize(), EnvoyException,
                          "metadata type is not supported:(.|\n)*route");
}

TEST_F(ConfigTest, MissingState) {
  TestUtility::loadFromYaml(std::string(SampleConfig), config_proto_);
  initialize();
  auto metadata = config_->extractMetadata(host_);
  EXPECT_EQ(0, metadata->filter_metadata().size());
  EXPECT_EQ(2, stats_store_.counter("internal_upstream.no_metadata").value());
}

TEST_F(ConfigTest, Empty) {
  initialize();
  EXPECT_EQ(nullptr, config_->extractMetadata(host_));
  EXPECT_EQ(0, stats_store_.counter("internal_upstream.no_metadata").value());
}

} // namespace
} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
