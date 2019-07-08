#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/zookeeper_proxy/decoder.h"
#include "extensions/filters/network/zookeeper_proxy/filter.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

bool protoMapEq(const ProtobufWkt::Struct& obj, const std::map<std::string, std::string>& rhs) {
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEq, rhs, "") { return protoMapEq(arg, rhs); }

class ZooKeeperFilterTest : public testing::Test {
public:
  ZooKeeperFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<ZooKeeperFilterConfig>(stat_prefix_, 1048576, scope_);
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

  Buffer::OwnedImpl encodeTooBigMessage() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(1048577);

    return buffer;
  }

  Buffer::OwnedImpl encodeBiggerThanLengthMessage() const {
    Buffer::OwnedImpl buffer;

    // Craft a delete request with a path that's longer than
    // the declared message length.
    buffer.writeBEInt<int32_t>(50);
    buffer.writeBEInt<int32_t>(1000);
    // Opcode.
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::DELETE));
    // Path.
    addString(buffer, std::string(2 * 1024 * 1024, '*'));
    // Version.
    buffer.writeBEInt<int32_t>(-1);

    return buffer;
  }

  Buffer::OwnedImpl encodePing() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(enumToSignedInt(XidCodes::PING_XID));
    buffer.writeBEInt<uint32_t>(enumToInt(OpCodes::PING));

    return buffer;
  }

  Buffer::OwnedImpl encodeUnknownOpcode() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<uint32_t>(200);

    return buffer;
  }

  Buffer::OwnedImpl encodeCloseRequest() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::CLOSE));

    return buffer;
  }

  Buffer::OwnedImpl encodeAuth(const std::string& scheme) const {
    const std::string credential = "p@sswd";
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(28 + scheme.length() + credential.length());
    buffer.writeBEInt<int32_t>(enumToSignedInt(XidCodes::AUTH_XID));
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SETAUTH));
    // Type.
    buffer.writeBEInt<int32_t>(0);
    addString(buffer, scheme);
    addString(buffer, credential);

    return buffer;
  }

  Buffer::OwnedImpl
  encodePathWatch(const std::string& path, const bool watch,
                  const int32_t opcode = enumToSignedInt(OpCodes::GETDATA)) const {
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
                                      const int32_t opcode = enumToSignedInt(OpCodes::GETDATA),
                                      const bool txn = false) const {
    Buffer::OwnedImpl buffer;

    if (!txn) {
      buffer.writeBEInt<int32_t>(16 + path.length());
      buffer.writeBEInt<int32_t>(1000);
      buffer.writeBEInt<int32_t>(opcode);
    }

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

  Buffer::OwnedImpl encodePathLongerThanBuffer(const std::string& path,
                                               const int32_t opcode) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(8 + path.length());
    buffer.writeBEInt<int32_t>(1000);
    buffer.writeBEInt<int32_t>(opcode);
    buffer.writeBEInt<uint32_t>(path.length() * 2);
    buffer.add(path);

    return buffer;
  }

  Buffer::OwnedImpl
  encodeCreateRequest(const std::string& path, const std::string& data, const CreateFlags flags,
                      const bool txn = false,
                      const int32_t opcode = enumToSignedInt(OpCodes::CREATE)) const {
    Buffer::OwnedImpl buffer;

    if (!txn) {
      buffer.writeBEInt<int32_t>(24 + path.length() + data.length());
      buffer.writeBEInt<int32_t>(1000);
      buffer.writeBEInt<int32_t>(opcode);
    }

    // Path.
    addString(buffer, path);
    // Data.
    addString(buffer, data);
    // Acls.
    buffer.writeBEInt<int32_t>(0);
    // Flags.
    buffer.writeBEInt<int32_t>(static_cast<int32_t>(flags));

    return buffer;
  }

  Buffer::OwnedImpl encodeSetRequest(const std::string& path, const std::string& data,
                                     const int32_t version, const bool txn = false) const {
    Buffer::OwnedImpl buffer;

    if (!txn) {
      buffer.writeBEInt<int32_t>(20 + path.length() + data.length());
      buffer.writeBEInt<int32_t>(1000);
      buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SETDATA));
    }

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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::DELETE));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SETACL));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::RECONFIG));
    addString(buffer, joining);
    addString(buffer, leaving);
    addString(buffer, new_members);
    buffer.writeBEInt<int64_t>(config_id);

    return buffer;
  }

  Buffer::OwnedImpl encodeSetWatchesRequest(const std::vector<std::string>& dataw,
                                            const std::vector<std::string>& existw,
                                            const std::vector<std::string>& childw,
                                            int32_t xid = 1000) const {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl watches_buffer;

    addStrings(watches_buffer, dataw);
    addStrings(watches_buffer, existw);
    addStrings(watches_buffer, childw);

    buffer.writeBEInt<int32_t>(8 + watches_buffer.length());
    buffer.writeBEInt<int32_t>(xid);
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SETWATCHES));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::MULTI));

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

  void expectSetDynamicMetadata(const std::map<std::string, std::string>& first,
                                const std::map<std::string, std::string>& second) {
    EXPECT_CALL(filter_callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, setDynamicMetadata(_, _))
        .WillOnce(Invoke([first](const std::string& key, const ProtobufWkt::Struct& obj) -> void {
          EXPECT_STREQ(key.c_str(), "envoy.filters.network.zookeeper_proxy");
          protoMapEq(obj, first);
        }))
        .WillOnce(Invoke([second](const std::string& key, const ProtobufWkt::Struct& obj) -> void {
          EXPECT_STREQ(key.c_str(), "envoy.filters.network.zookeeper_proxy");
          protoMapEq(obj, second);
        }));
  }

  void testCreate(CreateFlags flags, const OpCodes opcode = OpCodes::CREATE) {
    initialize();
    Buffer::OwnedImpl data =
        encodeCreateRequest("/foo", "bar", flags, false, enumToSignedInt(opcode));
    std::string opname = "create";

    switch (opcode) {
    case OpCodes::CREATECONTAINER:
      opname = "createcontainer";
      break;
    case OpCodes::CREATETTL:
      opname = "createttl";
      break;
    default:
      break;
    }

    expectSetDynamicMetadata(
        {{"opname", opname}, {"path", "/foo"}, {"create_type", createFlagsToString(flags)}},
        {{"bytes", "35"}});

    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));

    switch (opcode) {
    case OpCodes::CREATE:
      EXPECT_EQ(1UL, config_->stats().create_rq_.value());
      break;
    case OpCodes::CREATECONTAINER:
      EXPECT_EQ(1UL, config_->stats().createcontainer_rq_.value());
      break;
    case OpCodes::CREATETTL:
      EXPECT_EQ(1UL, config_->stats().createttl_rq_.value());
      break;
    default:
      break;
    }

    EXPECT_EQ(35UL, config_->stats().request_bytes_.value());
    EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
  }

  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<ZooKeeperFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test.zookeeper"};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(ZooKeeperFilterTest, Connect) {
  initialize();

  Buffer::OwnedImpl data = encodeConnect();

  expectSetDynamicMetadata({{"opname", "connect"}}, {{"bytes", "32"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(32UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, ConnectReadonly) {
  initialize();

  Buffer::OwnedImpl data = encodeConnect(true);

  expectSetDynamicMetadata({{"opname", "connect_readonly"}}, {{"bytes", "33"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(1UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(33UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, Fallback) {
  initialize();

  Buffer::OwnedImpl data = encodeBadMessage();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(0UL, config_->stats().connect_rq_.value());
  EXPECT_EQ(0UL, config_->stats().connect_readonly_rq_.value());
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PacketTooBig) {
  initialize();

  Buffer::OwnedImpl data = encodeTooBigMessage();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PacketBiggerThanLength) {
  initialize();

  Buffer::OwnedImpl data = encodeBiggerThanLengthMessage();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, UnknownOpcode) {
  initialize();

  Buffer::OwnedImpl data = encodeUnknownOpcode();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, BufferSmallerThanStringLength) {
  initialize();

  Buffer::OwnedImpl data = encodePathLongerThanBuffer("/foo", enumToSignedInt(OpCodes::SYNC));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PingRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePing();

  expectSetDynamicMetadata({{"opname", "ping"}}, {{"bytes", "12"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().ping_rq_.value());
  EXPECT_EQ(12UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, AuthRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeAuth("digest");

  expectSetDynamicMetadata({{"opname", "auth"}}, {{"bytes", "36"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(scope_.counter("test.zookeeper.auth.digest_rq").value(), 1);
  EXPECT_EQ(36UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetDataRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", true);

  expectSetDynamicMetadata({{"opname", "getdata"}, {"path", "/foo"}, {"watch", "true"}},
                           {{"bytes", "21"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(21UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(1UL, config_->stats().getdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetDataRequestEmptyPath) {
  initialize();

  // It's valid to see an empty string as the path, which gets treated as /
  // by the server.
  Buffer::OwnedImpl data = encodePathWatch("", true);

  expectSetDynamicMetadata({{"opname", "getdata"}, {"path", ""}, {"watch", "true"}},
                           {{"bytes", "17"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(17UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(1UL, config_->stats().getdata_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CreateRequestPersistent) { testCreate(CreateFlags::PERSISTENT); }

TEST_F(ZooKeeperFilterTest, CreateRequestPersistentSequential) {
  testCreate(CreateFlags::PERSISTENT_SEQUENTIAL);
}

TEST_F(ZooKeeperFilterTest, CreateRequestEphemeral) { testCreate(CreateFlags::EPHEMERAL); }

TEST_F(ZooKeeperFilterTest, CreateRequestEphemeralSequential) {
  testCreate(CreateFlags::EPHEMERAL_SEQUENTIAL);
}

TEST_F(ZooKeeperFilterTest, CreateRequestContainer) {
  testCreate(CreateFlags::CONTAINER, OpCodes::CREATECONTAINER);
}

TEST_F(ZooKeeperFilterTest, CreateRequestTTL) {
  testCreate(CreateFlags::PERSISTENT_WITH_TTL, OpCodes::CREATETTL);
}

TEST_F(ZooKeeperFilterTest, CreateRequestTTLSequential) {
  testCreate(CreateFlags::PERSISTENT_SEQUENTIAL_WITH_TTL);
}

TEST_F(ZooKeeperFilterTest, CreateRequest2) {
  initialize();

  Buffer::OwnedImpl data = encodeCreateRequest("/foo", "bar", CreateFlags::PERSISTENT, false,
                                               enumToSignedInt(OpCodes::CREATE2));

  expectSetDynamicMetadata({{"opname", "create2"}, {"path", "/foo"}, {"create_type", "persistent"}},
                           {{"bytes", "35"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().create2_rq_.value());
  EXPECT_EQ(35UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeSetRequest("/foo", "bar", -1);

  expectSetDynamicMetadata({{"opname", "setdata"}, {"path", "/foo"}}, {{"bytes", "31"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().setdata_rq_.value());
  EXPECT_EQ(31UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::GETCHILDREN));

  expectSetDynamicMetadata({{"opname", "getchildren"}, {"path", "/foo"}, {"watch", "false"}},
                           {{"bytes", "21"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().getchildren_rq_.value());
  EXPECT_EQ(21UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest2) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::GETCHILDREN2));

  expectSetDynamicMetadata({{"opname", "getchildren2"}, {"path", "/foo"}, {"watch", "false"}},
                           {{"bytes", "21"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().getchildren2_rq_.value());
  EXPECT_EQ(21UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, DeleteRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeDeleteRequest("/foo", -1);

  expectSetDynamicMetadata({{"opname", "remove"}, {"path", "/foo"}, {"version", "-1"}},
                           {{"bytes", "24"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().remove_rq_.value());
  EXPECT_EQ(24UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, ExistsRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::EXISTS));

  expectSetDynamicMetadata({{"opname", "exists"}, {"path", "/foo"}, {"watch", "false"}},
                           {{"bytes", "21"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().exists_rq_.value());
  EXPECT_EQ(21UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetAclRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GETACL));

  expectSetDynamicMetadata({{"opname", "getacl"}, {"path", "/foo"}}, {{"bytes", "20"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().getacl_rq_.value());
  EXPECT_EQ(20UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetAclRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeSetAclRequest("/foo", "digest", "passwd", -1);

  expectSetDynamicMetadata({{"opname", "setacl"}, {"path", "/foo"}, {"version", "-1"}},
                           {{"bytes", "52"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().setacl_rq_.value());
  EXPECT_EQ(52UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SyncRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::SYNC));

  expectSetDynamicMetadata({{"opname", "sync"}, {"path", "/foo"}}, {{"bytes", "20"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().sync_rq_.value());
  EXPECT_EQ(20UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetEphemeralsRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GETEPHEMERALS));

  expectSetDynamicMetadata({{"opname", "getephemerals"}, {"path", "/foo"}}, {{"bytes", "20"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().getephemerals_rq_.value());
  EXPECT_EQ(20UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, GetAllChildrenNumberRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GETALLCHILDRENNUMBER));

  expectSetDynamicMetadata({{"opname", "getallchildrennumber"}, {"path", "/foo"}},
                           {{"bytes", "20"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().getallchildrennumber_rq_.value());
  EXPECT_EQ(20UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CheckRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", 100, enumToSignedInt(OpCodes::CHECK));

  expectSetDynamicMetadata({{"bytes", "24"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().check_rq_.value());
  EXPECT_EQ(24UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, MultiRequest) {
  initialize();

  Buffer::OwnedImpl create1 = encodeCreateRequest("/foo", "1", CreateFlags::PERSISTENT, true);
  Buffer::OwnedImpl create2 = encodeCreateRequest("/bar", "1", CreateFlags::PERSISTENT, true);
  Buffer::OwnedImpl check1 = encodePathVersion("/foo", 100, enumToSignedInt(OpCodes::CHECK), true);
  Buffer::OwnedImpl set1 = encodeSetRequest("/bar", "2", -1, true);

  std::vector<std::pair<int32_t, Buffer::OwnedImpl>> ops;
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::CREATE), std::move(create1)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::CREATE), std::move(create2)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::CHECK), std::move(check1)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::SETDATA), std::move(set1)));

  Buffer::OwnedImpl data = encodeMultiRequest(ops);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().multi_rq_.value());
  EXPECT_EQ(128UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(2UL, config_->stats().create_rq_.value());
  EXPECT_EQ(1UL, config_->stats().setdata_rq_.value());
  EXPECT_EQ(1UL, config_->stats().check_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, ReconfigRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeReconfigRequest("s1", "s2", "s3", 1000);

  expectSetDynamicMetadata({{"opname", "reconfig"}}, {{"bytes", "38"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().reconfig_rq_.value());
  EXPECT_EQ(38UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetWatchesRequestControlXid) {
  initialize();

  const std::vector<std::string> dataw = {"/foo", "/bar"};
  const std::vector<std::string> existw = {"/foo1", "/bar1"};
  const std::vector<std::string> childw = {"/foo2", "/bar2"};

  Buffer::OwnedImpl data =
      encodeSetWatchesRequest(dataw, existw, childw, enumToSignedInt(XidCodes::SET_WATCHES_XID));

  expectSetDynamicMetadata({{"opname", "setwatches"}}, {{"bytes", "76"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().setwatches_rq_.value());
  EXPECT_EQ(76UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, SetWatchesRequest) {
  initialize();

  const std::vector<std::string> dataw = {"/foo", "/bar"};
  const std::vector<std::string> existw = {"/foo1", "/bar1"};
  const std::vector<std::string> childw = {"/foo2", "/bar2"};

  Buffer::OwnedImpl data = encodeSetWatchesRequest(dataw, existw, childw);

  expectSetDynamicMetadata({{"opname", "setwatches"}}, {{"bytes", "76"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().setwatches_rq_.value());
  EXPECT_EQ(76UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CheckWatchesRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", enumToSignedInt(WatcherType::CHILDREN),
                                             enumToSignedInt(OpCodes::CHECKWATCHES));

  expectSetDynamicMetadata({{"opname", "checkwatches"}, {"path", "/foo"}}, {{"bytes", "24"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().checkwatches_rq_.value());
  EXPECT_EQ(24UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, RemoveWatchesRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", enumToSignedInt(WatcherType::DATA),
                                             enumToSignedInt(OpCodes::REMOVEWATCHES));

  expectSetDynamicMetadata({{"opname", "removewatches"}, {"path", "/foo"}}, {{"bytes", "24"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().removewatches_rq_.value());
  EXPECT_EQ(24UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, CloseRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeCloseRequest();

  expectSetDynamicMetadata({{"opname", "close"}}, {{"bytes", "12"}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().close_rq_.value());
  EXPECT_EQ(12UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
