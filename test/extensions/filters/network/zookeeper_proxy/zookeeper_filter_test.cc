#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/zookeeper_proxy/zookeeper_decoder.h"
#include "extensions/filters/network/zookeeper_proxy/zookeeper_filter.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

class ZooKeeperFilterTest : public TestBase {
public:
  ZooKeeperFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<ZooKeeperFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<ZooKeeperFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Buffer::OwnedImpl* encodeConnect(const bool readonly = false, const uint64_t zxid = 100,
                                   const uint32_t session_timeout = 10,
                                   const uint32_t session_id = 200,
                                   const std::string& passwd = "") const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();
    const uint32_t message_size = readonly ? 28 + passwd.length() + 1 : 28 + passwd.length();

    buffer->writeBEInt<uint32_t>(message_size);
    buffer->writeBEInt<uint32_t>(0); // Protocol version.
    buffer->writeBEInt<uint64_t>(zxid);
    buffer->writeBEInt<uint32_t>(session_timeout);
    buffer->writeBEInt<uint64_t>(session_id);
    buffer->writeBEInt<uint32_t>(passwd.length());
    buffer->add(passwd);

    if (readonly) {
      char readonly_flag = 0b1;
      buffer->add(std::string(1, readonly_flag));
    }

    return buffer;
  }

  Buffer::OwnedImpl* encodeBadMessage() const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    // Bad length.
    buffer->writeBEInt<uint32_t>(1);
    // Trailing int.
    buffer->writeBEInt<uint32_t>(3);

    return buffer;
  }

  Buffer::OwnedImpl* encodePing() const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    buffer->writeBEInt<uint32_t>(8);
    buffer->writeBEInt<int32_t>(PING_XID);

    return buffer;
  }

  Buffer::OwnedImpl* encodeAuth(const std::string& scheme) const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    buffer->writeBEInt<uint32_t>(28 + scheme.length());
    buffer->writeBEInt<int32_t>(AUTH_XID);
    // Opcode.
    buffer->writeBEInt<int32_t>(100);
    // Type.
    buffer->writeBEInt<int32_t>(0);
    // Scheme.
    buffer->writeBEInt<uint32_t>(scheme.length());
    buffer->add(scheme);
    // Credential.
    buffer->writeBEInt<uint32_t>(6);
    buffer->add("p@sswd");

    return buffer;
  }

  Buffer::OwnedImpl* encodeGetData(const std::string& path, const bool watch,
                                   const bool getchildren = false) const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    buffer->writeBEInt<int32_t>(13 + path.length());
    buffer->writeBEInt<int32_t>(1000);
    // Opcode.
    const int32_t opcode =
        getchildren ? enumToInt(Opcodes::GETCHILDREN) : enumToInt(Opcodes::GETDATA);
    buffer->writeBEInt<int32_t>(opcode);
    // Path.
    buffer->writeBEInt<int32_t>(path.length());
    buffer->add(path);
    // Watch.
    char watch_flag = watch ? 0b1 : 0b0;
    buffer->add(std::string(1, watch_flag));

    return buffer;
  }

  Buffer::OwnedImpl* encodeCreateRequest(const std::string& path, const std::string& data,
                                         const bool ephemeral, const bool sequence) const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    buffer->writeBEInt<int32_t>(24 + path.length() + data.length());
    buffer->writeBEInt<int32_t>(1000);
    // Opcode.
    buffer->writeBEInt<int32_t>(enumToInt(Opcodes::CREATE));
    // Path.
    buffer->writeBEInt<int32_t>(path.length());
    buffer->add(path);
    // Data.
    buffer->writeBEInt<int32_t>(data.length());
    buffer->add(data);
    // Acls.
    buffer->writeBEInt<int32_t>(0);
    // Flags.
    int flags = 0;
    if (ephemeral) {
      flags &= 0x1;
    }
    if (sequence) {
      flags &= 0x2;
    }
    buffer->writeBEInt<int32_t>(flags);

    return buffer;
  }

  Buffer::OwnedImpl* encodeSetRequest(const std::string& path, const std::string& data,
                                      const int32_t version) const {
    Buffer::OwnedImpl* buffer = new Buffer::OwnedImpl();

    buffer->writeBEInt<int32_t>(20 + path.length() + data.length());
    buffer->writeBEInt<int32_t>(1000);
    // Opcode.
    buffer->writeBEInt<int32_t>(enumToInt(Opcodes::SETDATA));
    // Path.
    buffer->writeBEInt<int32_t>(path.length());
    buffer->add(path);
    // Data.
    buffer->writeBEInt<int32_t>(data.length());
    buffer->add(data);
    // Version.
    buffer->writeBEInt<int32_t>(version);

    return buffer;
  }

  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<ZooKeeperFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test.zookeeper"};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

// Test Connect counter increment.
TEST_F(ZooKeeperFilterTest, Connect) {
  initialize();

  Buffer::InstancePtr data(encodeConnect());

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

// Test Connect readonly counter increment.
TEST_F(ZooKeeperFilterTest, ConnectReadonly) {
  initialize();

  Buffer::InstancePtr data(encodeConnect(true));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(1UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

// Test fallback.
TEST_F(ZooKeeperFilterTest, Fallback) {
  initialize();

  Buffer::InstancePtr data(encodeBadMessage());

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(0UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(2UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PingRequest) {
  initialize();

  Buffer::InstancePtr data(encodePing());

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().ping_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, AuthRequest) {
  initialize();

  Buffer::InstancePtr data(encodeAuth("digest"));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(scope_.counter("test.zookeeper.auth.digest_rq").value(), 1);
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetDataRequest) {
  initialize();

  Buffer::InstancePtr data(encodeGetData("/foo", true));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().getdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CreateRequest) {
  initialize();

  Buffer::InstancePtr data(encodeCreateRequest("/foo", "bar", false, false));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().create_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetRequest) {
  initialize();

  Buffer::InstancePtr data(encodeSetRequest("/foo", "bar", -1));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().setdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest) {
  initialize();

  Buffer::InstancePtr data(encodeGetData("/foo", false, true));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().getchildren_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
