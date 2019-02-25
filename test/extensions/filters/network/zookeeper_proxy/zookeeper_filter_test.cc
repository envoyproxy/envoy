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

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(rhs.size() > 0);
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

class ZooKeeperFilterTest : public TestBase {
public:
  ZooKeeperFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<ZooKeeperFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<ZooKeeperFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Buffer::OwnedImpl encodeConnect(const bool readonly = false, const uint64_t zxid = 100,
                                  const uint32_t session_timeout = 10,
                                  const uint32_t session_id = 200,
                                  const std::string& passwd = "") const {
    Buffer::OwnedImpl buffer;
    const uint32_t message_size = readonly ? 28 + passwd.length() + 1 : 28 + passwd.length();

    buffer.writeBEInt<uint32_t>(message_size);
    buffer.writeBEInt<uint32_t>(0); // Protocol version.
    buffer.writeBEInt<uint64_t>(zxid);
    buffer.writeBEInt<uint32_t>(session_timeout);
    buffer.writeBEInt<uint64_t>(session_id);
    addString(buffer, passwd);

    if (readonly) {
      const char readonly_flag = 0b1;
      buffer.add(std::string(1, readonly_flag));
    }

    return buffer;
  }

  Buffer::OwnedImpl encodeBadMessage() const {
    Buffer::OwnedImpl buffer;

    // Bad length.
    buffer.writeBEInt<uint32_t>(1);
    // Trailing int.
    buffer.writeBEInt<uint32_t>(3);

    return buffer;
  }

  Buffer::OwnedImpl encodePing() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(enumToInt(XidCodes::PING_XID));

    return buffer;
  }

  Buffer::OwnedImpl encodeCloseRequest() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::CLOSE));

    return buffer;
  }

  Buffer::OwnedImpl encodeAuth(const std::string& scheme) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(28 + scheme.length());
    buffer.writeBEInt<int32_t>(enumToInt(XidCodes::AUTH_XID));
    // Opcode.
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::SETAUTH));
    // Type.
    buffer.writeBEInt<int32_t>(0);
    // Scheme.
    addString(buffer, scheme);
    // Credential.
    addString(buffer, "p@sswd");

    return buffer;
  }

  Buffer::OwnedImpl encodePathWatch(const std::string& path, const bool watch,
                                    const int32_t opcode = enumToInt(OpCodes::GETDATA)) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(13 + path.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(opcode);
    // Path.
    addString(buffer, path);
    // Watch.
    const char watch_flag = watch ? 0b1 : 0b0;
    buffer.add(std::string(1, watch_flag));

    return buffer;
  }

  Buffer::OwnedImpl encodePathVersion(const std::string& path, const int32_t version,
                                      const int32_t opcode = enumToInt(OpCodes::GETDATA)) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(16 + path.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(opcode);
    // Path.
    addString(buffer, path);
    // Version
    buffer.writeBEInt<int32_t>(version);

    return buffer;
  }

  Buffer::OwnedImpl encodePath(const std::string& path, const int32_t opcode) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(8 + path.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(opcode);
    // Path.
    addString(buffer, path);

    return buffer;
  }

  Buffer::OwnedImpl encodeCreateRequest(const std::string& path, const std::string& data,
                                        const bool ephemeral, const bool sequence,
                                        const bool txn = false) const {
    Buffer::OwnedImpl buffer;

    if (!txn) {
      buffer.writeBEInt<int32_t>(24 + path.length() + data.length());
      buffer.writeBEInt<int32_t>(1000);
      buffer.writeBEInt<int32_t>(enumToInt(OpCodes::CREATE));
    }

    // Path.
    addString(buffer, path);
    // Data.
    addString(buffer, data);
    // Acls.
    buffer.writeBEInt<int32_t>(0);
    // Flags.
    int flags = 0;
    if (ephemeral) {
      flags &= 0x1;
    }
    if (sequence) {
      flags &= 0x2;
    }
    buffer.writeBEInt<int32_t>(flags);

    return buffer;
  }

  Buffer::OwnedImpl encodeSetRequest(const std::string& path, const std::string& data,
                                     const int32_t version) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(20 + path.length() + data.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::SETDATA));
    // Path.
    addString(buffer, path);
    // Data.
    addString(buffer, data);
    // Version.
    buffer.writeBEInt<int32_t>(version);

    return buffer;
  }

  Buffer::OwnedImpl encodeDeleteRequest(const std::string& path, const int32_t version) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(16 + path.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::DELETE));
    // Path.
    addString(buffer, path);
    // Version.
    buffer.writeBEInt<int32_t>(version);

    return buffer;
  }

  Buffer::OwnedImpl encodeSetAclRequest(const std::string& path, const std::string& scheme,
                                        const std::string& credential,
                                        const int32_t version) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(32 + path.length() + scheme.length() + credential.length());
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::SETACL));
    // Path.
    addString(buffer, path);

    // Acls.
    buffer.writeBEInt<int32_t>(1);
    // Type.
    buffer.writeBEInt<int32_t>(0);
    // Scheme.
    addString(buffer, scheme);
    // Credential.
    addString(buffer, credential);

    // Version.
    buffer.writeBEInt<int32_t>(version);

    return buffer;
  }

  Buffer::OwnedImpl encodeReconfigRequest(const std::string& joining, const std::string& leaving,
                                          const std::string& new_members, int64_t config_id) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(28 + joining.length() + leaving.length() + new_members.length());
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::RECONFIG));
    addString(buffer, joining);
    addString(buffer, leaving);
    addString(buffer, new_members);
    buffer.writeBEInt<int64_t>(config_id);

    return buffer;
  }

  Buffer::OwnedImpl encodeSetWatchesRequest(const std::vector<std::string>& dataw,
                                            const std::vector<std::string>& existw,
                                            const std::vector<std::string>& childw) const {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl watches_buffer;

    addStrings(watches_buffer, dataw);
    addStrings(watches_buffer, existw);
    addStrings(watches_buffer, childw);

    buffer.writeBEInt<int32_t>(8 + watches_buffer.length());
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::SETWATCHES));
    buffer.add(watches_buffer);

    return buffer;
  }

  Buffer::OwnedImpl
  encodeMultiRequest(const std::vector<std::pair<int32_t, Buffer::OwnedImpl>>& ops) const {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl requests;

    for (const auto& op_pair : ops) {
      // Header.
      requests.writeBEInt<int32_t>(op_pair.first);
      requests.add(std::string(1, 0b0));
      requests.writeBEInt<int32_t>(-1);

      // Payload.
      requests.add(op_pair.second);
    }

    // Done header.
    requests.writeBEInt<int32_t>(-1);
    requests.add(std::string(1, 0b1));
    requests.writeBEInt<int32_t>(-1);

    // Multi prefix.
    buffer.writeBEInt<int32_t>(8 + requests.length());
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(enumToInt(OpCodes::MULTI));

    // Requests.
    buffer.add(requests);

    return buffer;
  }

  void addString(Buffer::OwnedImpl& buffer, const std::string& str) const {
    buffer.writeBEInt<uint32_t>(str.length());
    buffer.add(str);
  }

  void addStrings(Buffer::OwnedImpl& buffer, const std::vector<std::string>& watches) const {
    buffer.writeBEInt<uint32_t>(watches.size());

    for (const auto& watch : watches) {
      addString(buffer, watch);
    }
  }

  void expectSetDynamicMetadata(const std::map<std::string, std::string>& expected) {
    EXPECT_CALL(filter_callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_,
                setDynamicMetadata("envoy.filters.network.zookeeper_proxy", MapEq(expected)));
  }

  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<ZooKeeperFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test.zookeeper"};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

// Test Connect counter increment.
TEST_F(ZooKeeperFilterTest, Connect) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeConnect()));

  expectSetDynamicMetadata({{"opname", "connect"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

// Test Connect readonly counter increment.
TEST_F(ZooKeeperFilterTest, ConnectReadonly) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeConnect(true)));

  expectSetDynamicMetadata({{"opname", "connect_readonly"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(1UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

// Test fallback.
TEST_F(ZooKeeperFilterTest, Fallback) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeBadMessage()));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(0UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(2UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PingRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodePing()));

  expectSetDynamicMetadata({{"opname", "ping"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().ping_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, AuthRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeAuth("digest")));

  expectSetDynamicMetadata({{"opname", "auth"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(scope_.counter("test.zookeeper.auth.digest_rq").value(), 1);
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetDataRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodePathWatch("/foo", true)));

  expectSetDynamicMetadata({{"opname", "getdata"}, {"path", "/foo"}, {"watch", "true"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().getdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CreateRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeCreateRequest("/foo", "bar", false, false)));

  expectSetDynamicMetadata(
      {{"opname", "create"}, {"path", "/foo"}, {"ephemeral", "false"}, {"sequence", "false"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().create_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeSetRequest("/foo", "bar", -1)));

  expectSetDynamicMetadata({{"opname", "setdata"}, {"path", "/foo"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().setdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest) {
  initialize();

  Buffer::InstancePtr data(
      new Buffer::OwnedImpl(encodePathWatch("/foo", false, enumToInt(OpCodes::GETCHILDREN))));

  expectSetDynamicMetadata({{"opname", "getchildren"}, {"path", "/foo"}, {"watch", "false"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().getchildren_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, DeleteRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeDeleteRequest("/foo", -1)));

  expectSetDynamicMetadata({{"opname", "remove"}, {"path", "/foo"}, {"version", "-1"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().remove_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, ExistsRequest) {
  initialize();

  Buffer::InstancePtr data(
      new Buffer::OwnedImpl(encodePathWatch("/foo", false, enumToInt(OpCodes::EXISTS))));

  expectSetDynamicMetadata({{"opname", "exists"}, {"path", "/foo"}, {"watch", "false"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().exists_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetAclRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodePath("/foo", enumToInt(OpCodes::GETACL))));

  expectSetDynamicMetadata({{"opname", "getacl"}, {"path", "/foo"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().getacl_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetAclRequest) {
  initialize();

  Buffer::InstancePtr data(
      new Buffer::OwnedImpl(encodeSetAclRequest("/foo", "digest", "passwd", -1)));

  expectSetDynamicMetadata({{"opname", "setacl"}, {"path", "/foo"}, {"version", "-1"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().setacl_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SyncRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodePath("/foo", enumToInt(OpCodes::SYNC))));

  expectSetDynamicMetadata({{"opname", "sync"}, {"path", "/foo"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().sync_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CheckRequest) {
  initialize();

  Buffer::InstancePtr data(
      new Buffer::OwnedImpl(encodePathVersion("/foo", 100, enumToInt(OpCodes::CHECK))));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().check_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, MultiRequest) {
  initialize();

  Buffer::OwnedImpl create1 = encodeCreateRequest("/foo", "1", false, false, true);
  Buffer::OwnedImpl create2 = encodeCreateRequest("/bar", "1", false, false, true);
  std::vector<std::pair<int32_t, Buffer::OwnedImpl>> ops;
  ops.push_back(std::make_pair(enumToInt(OpCodes::CREATE), std::move(create1)));
  ops.push_back(std::make_pair(enumToInt(OpCodes::CREATE), std::move(create2)));
  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeMultiRequest(ops)));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().multi_rq_.value());
  EXPECT_EQ(2UL, config_->stats().create_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, ReconfigRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeReconfigRequest("s1", "s2", "s3", 1000)));

  expectSetDynamicMetadata({{"opname", "reconfig"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().reconfig_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetWatchesRequest) {
  initialize();

  const std::vector<std::string> dataw = {"/foo", "/bar"};
  const std::vector<std::string> existw = {"/foo1", "/bar1"};
  const std::vector<std::string> childw = {"/foo2", "/bar2"};

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeSetWatchesRequest(dataw, existw, childw)));

  expectSetDynamicMetadata({{"opname", "setwatches"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().setwatches_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CloseRequest) {
  initialize();

  Buffer::InstancePtr data(new Buffer::OwnedImpl(encodeCloseRequest()));

  expectSetDynamicMetadata({{"opname", "close"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*data, false));
  EXPECT_EQ(1UL, config_->stats().close_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
