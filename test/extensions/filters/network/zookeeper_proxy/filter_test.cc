#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/zookeeper_proxy/decoder.h"
#include "extensions/filters/network/zookeeper_proxy/filter.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/simulated_time_system.h"

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
    filter_ = std::make_unique<ZooKeeperFilter>(config_, time_system_);
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

  Buffer::OwnedImpl encodeConnectResponse(const bool readonly = false,
                                          const uint32_t session_timeout = 10,
                                          const uint32_t session_id = 200,
                                          const std::string& passwd = "") const {
    Buffer::OwnedImpl buffer;
    const uint32_t message_size = readonly ? 20 + passwd.length() + 1 : 20 + passwd.length();

    buffer.writeBEInt<uint32_t>(message_size);
    buffer.writeBEInt<uint32_t>(0); // Protocol version.
    buffer.writeBEInt<uint32_t>(session_timeout);
    buffer.writeBEInt<uint64_t>(session_id);
    addString(buffer, passwd);

    if (readonly) {
      const char readonly_flag = 0b1;
      buffer.add(std::string(1, readonly_flag));
    }

    return buffer;
  }

  Buffer::OwnedImpl encodeResponseHeader(const int32_t xid, const int64_t zxid,
                                         const int32_t error) const {
    Buffer::OwnedImpl buffer;
    const uint32_t message_size = 16;

    buffer.writeBEInt<uint32_t>(message_size);
    buffer.writeBEInt<uint32_t>(xid);
    buffer.writeBEInt<uint64_t>(zxid);
    buffer.writeBEInt<uint32_t>(error);

    return buffer;
  }

  Buffer::OwnedImpl encodeWatchEvent(const std::string& path, const int32_t event_type,
                                     const int32_t client_state) const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(28 + path.size());
    buffer.writeBEInt<uint32_t>(enumToSignedInt(XidCodes::WatchXid));
    buffer.writeBEInt<uint64_t>(1000);
    buffer.writeBEInt<uint32_t>(0);
    buffer.writeBEInt<uint32_t>(event_type);
    buffer.writeBEInt<uint32_t>(client_state);
    addString(buffer, path);

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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::Delete));
    // Path.
    addString(buffer, std::string(2 * 1024 * 1024, '*'));
    // Version.
    buffer.writeBEInt<int32_t>(-1);

    return buffer;
  }

  Buffer::OwnedImpl encodePing() const {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(8);
    buffer.writeBEInt<int32_t>(enumToSignedInt(XidCodes::PingXid));
    buffer.writeBEInt<uint32_t>(enumToInt(OpCodes::Ping));

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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::Close));

    return buffer;
  }

  Buffer::OwnedImpl encodeAuth(const std::string& scheme) const {
    const std::string credential = "p@sswd";
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<uint32_t>(28 + scheme.length() + credential.length());
    buffer.writeBEInt<int32_t>(enumToSignedInt(XidCodes::AuthXid));
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SetAuth));
    // Type.
    buffer.writeBEInt<int32_t>(0);
    addString(buffer, scheme);
    addString(buffer, credential);

    return buffer;
  }

  Buffer::OwnedImpl
  encodePathWatch(const std::string& path, const bool watch,
                  const int32_t opcode = enumToSignedInt(OpCodes::GetData)) const {
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
                                      const int32_t opcode = enumToSignedInt(OpCodes::GetData),
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
                      const int32_t opcode = enumToSignedInt(OpCodes::Create)) const {
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
      buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SetData));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::Delete));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SetAcl));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::Reconfig));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::SetWatches));
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
    buffer.writeBEInt<int32_t>(enumToSignedInt(OpCodes::Multi));

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

  using StrStrMap = std::map<std::string, std::string>;

  void expectSetDynamicMetadata(const std::vector<StrStrMap>& values) {
    EXPECT_CALL(filter_callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(stream_info_));

    auto& call = EXPECT_CALL(stream_info_, setDynamicMetadata(_, _));

    for (const auto& value : values) {
      call.WillOnce(Invoke([value](const std::string& key, const ProtobufWkt::Struct& obj) -> void {
        EXPECT_STREQ(key.c_str(), "envoy.filters.network.zookeeper_proxy");
        protoMapEq(obj, value);
      }));
    }
  }

  void testCreate(CreateFlags flags, const OpCodes opcode = OpCodes::Create) {
    initialize();
    Buffer::OwnedImpl data =
        encodeCreateRequest("/foo", "bar", flags, false, enumToSignedInt(opcode));
    std::string opname = "create";

    switch (opcode) {
    case OpCodes::CreateContainer:
      opname = "createcontainer";
      break;
    case OpCodes::CreateTtl:
      opname = "createttl";
      break;
    default:
      break;
    }

    expectSetDynamicMetadata(
        {{{"opname", opname}, {"path", "/foo"}, {"create_type", createFlagsToString(flags)}},
         {{"bytes", "35"}}});

    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));

    switch (opcode) {
    case OpCodes::Create:
      EXPECT_EQ(1UL, config_->stats().create_rq_.value());
      break;
    case OpCodes::CreateContainer:
      EXPECT_EQ(1UL, config_->stats().createcontainer_rq_.value());
      break;
    case OpCodes::CreateTtl:
      EXPECT_EQ(1UL, config_->stats().createttl_rq_.value());
      break;
    default:
      break;
    }

    EXPECT_EQ(35UL, config_->stats().request_bytes_.value());
    EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
  }

  void testRequest(Buffer::OwnedImpl& data, const std::vector<StrStrMap>& metadata_values,
                   const Stats::Counter& stat, const uint64_t request_bytes) {
    expectSetDynamicMetadata(metadata_values);
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
    EXPECT_EQ(1UL, stat.value());
    EXPECT_EQ(request_bytes, config_->stats().request_bytes_.value());
    EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
  }

  void testResponse(const std::vector<StrStrMap>& metadata_values, const Stats::Counter& stat,
                    uint32_t xid = 1000) {
    Buffer::OwnedImpl data = encodeResponseHeader(xid, 2000, 0);

    expectSetDynamicMetadata(metadata_values);
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(data, false));
    EXPECT_EQ(1UL, stat.value());
    EXPECT_EQ(20UL, config_->stats().response_bytes_.value());
    EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
    const auto histogram_name =
        fmt::format("test.zookeeper.{}_latency", metadata_values[0].find("opname")->second);
    EXPECT_NE(absl::nullopt, findHistogram(histogram_name));
  }

  Stats::HistogramOptConstRef findHistogram(const std::string& name) {
    Stats::StatNameManagedStorage storage(name, scope_.symbolTable());
    return scope_.findHistogram(storage.statName());
  }

  Stats::TestUtil::TestStore scope_;
  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<ZooKeeperFilter> filter_;
  std::string stat_prefix_{"test.zookeeper"};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(ZooKeeperFilterTest, Connect) {
  initialize();

  Buffer::OwnedImpl data = encodeConnect();

  testRequest(data, {{{"opname", "connect"}}, {{"bytes", "32"}}}, config_->stats().connect_rq_, 32);

  data = encodeConnectResponse();
  expectSetDynamicMetadata({{{"opname", "connect_response"},
                             {"protocol_version", "0"},
                             {"timeout", "10"},
                             {"readonly", "0"}},
                            {{"bytes", "24"}}});
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(data, false));
  EXPECT_EQ(1UL, config_->stats().connect_resp_.value());
  EXPECT_EQ(24UL, config_->stats().response_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
  EXPECT_NE(absl::nullopt, findHistogram("test.zookeeper.connect_response_latency"));
}

TEST_F(ZooKeeperFilterTest, ConnectReadonly) {
  initialize();

  Buffer::OwnedImpl data = encodeConnect(true);

  testRequest(data, {{{"opname", "connect_readonly"}}, {{"bytes", "33"}}},
              config_->stats().connect_readonly_rq_, 33);

  data = encodeConnectResponse(true);
  expectSetDynamicMetadata({{{"opname", "connect_response"},
                             {"protocol_version", "0"},
                             {"timeout", "10"},
                             {"readonly", "1"}},
                            {{"bytes", "25"}}});
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(data, false));
  EXPECT_EQ(1UL, config_->stats().connect_resp_.value());
  EXPECT_EQ(25UL, config_->stats().response_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
  EXPECT_NE(absl::nullopt, findHistogram("test.zookeeper.connect_response_latency"));
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

  Buffer::OwnedImpl data = encodePathLongerThanBuffer("/foo", enumToSignedInt(OpCodes::Sync));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_error_.value());
}

TEST_F(ZooKeeperFilterTest, PingRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePing();

  testRequest(data, {{{"opname", "ping"}}, {{"bytes", "12"}}}, config_->stats().ping_rq_, 12);
  testResponse({{{"opname", "ping_response"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().ping_resp_, enumToSignedInt(XidCodes::PingXid));
}

TEST_F(ZooKeeperFilterTest, AuthRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeAuth("digest");

  testRequest(data, {{{"opname", "auth"}}, {{"bytes", "36"}}},
              scope_.counter("test.zookeeper.auth.digest_rq"), 36);
  testResponse({{{"opname", "auth_response"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().auth_resp_, enumToSignedInt(XidCodes::AuthXid));
}

TEST_F(ZooKeeperFilterTest, GetDataRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", true);

  testRequest(data,
              {{{"opname", "getdata"}, {"path", "/foo"}, {"watch", "true"}}, {{"bytes", "21"}}},
              config_->stats().getdata_rq_, 21);
  testResponse({{{"opname", "getdata_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().getdata_resp_);
}

TEST_F(ZooKeeperFilterTest, GetDataRequestEmptyPath) {
  initialize();

  // It's valid to see an empty string as the path, which gets treated as /
  // by the server.
  Buffer::OwnedImpl data = encodePathWatch("", true);

  testRequest(data, {{{"opname", "getdata"}, {"path", ""}, {"watch", "true"}}, {{"bytes", "17"}}},
              config_->stats().getdata_rq_, 17);
  testResponse({{{"opname", "getdata_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().getdata_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequestPersistent) { testCreate(CreateFlags::Persistent); }

TEST_F(ZooKeeperFilterTest, CreateRequestPersistentSequential) {
  testCreate(CreateFlags::PersistentSequential);
  testResponse({{{"opname", "create_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().create_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequestEphemeral) { testCreate(CreateFlags::Ephemeral); }

TEST_F(ZooKeeperFilterTest, CreateRequestEphemeralSequential) {
  testCreate(CreateFlags::EphemeralSequential);
  testResponse({{{"opname", "create_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().create_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequestContainer) {
  testCreate(CreateFlags::Container, OpCodes::CreateContainer);
  testResponse(
      {{{"opname", "createcontainer_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().createcontainer_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequestTTL) {
  testCreate(CreateFlags::PersistentWithTtl, OpCodes::CreateTtl);
  testResponse(
      {{{"opname", "createttl_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().createttl_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequestTTLSequential) {
  testCreate(CreateFlags::PersistentSequentialWithTtl, OpCodes::CreateTtl);
  testResponse(
      {{{"opname", "createttl_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().createttl_resp_);
}

TEST_F(ZooKeeperFilterTest, CreateRequest2) {
  initialize();

  Buffer::OwnedImpl data = encodeCreateRequest("/foo", "bar", CreateFlags::Persistent, false,
                                               enumToSignedInt(OpCodes::Create2));

  testRequest(
      data,
      {{{"opname", "create2"}, {"path", "/foo"}, {"create_type", "persistent"}}, {{"bytes", "35"}}},
      config_->stats().create2_rq_, 35);
  testResponse({{{"opname", "create2_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().create2_resp_);
}

TEST_F(ZooKeeperFilterTest, SetRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeSetRequest("/foo", "bar", -1);

  testRequest(data, {{{"opname", "setdata"}, {"path", "/foo"}}, {{"bytes", "31"}}},
              config_->stats().setdata_rq_, 31);
  testResponse({{{"opname", "setdata_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().setdata_resp_);
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::GetChildren));

  testRequest(
      data, {{{"opname", "getchildren"}, {"path", "/foo"}, {"watch", "false"}}, {{"bytes", "21"}}},
      config_->stats().getchildren_rq_, 21);
  testResponse(
      {{{"opname", "getchildren_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().getchildren_resp_);
}

TEST_F(ZooKeeperFilterTest, GetChildrenRequest2) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::GetChildren2));

  testRequest(
      data, {{{"opname", "getchildren2"}, {"path", "/foo"}, {"watch", "false"}}, {{"bytes", "21"}}},
      config_->stats().getchildren2_rq_, 21);
  testResponse(
      {{{"opname", "getchildren2_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().getchildren2_resp_);
}

TEST_F(ZooKeeperFilterTest, DeleteRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeDeleteRequest("/foo", -1);

  testRequest(data,
              {{{"opname", "delete"}, {"path", "/foo"}, {"version", "-1"}}, {{"bytes", "24"}}},
              config_->stats().delete_rq_, 24);
  testResponse({{{"opname", "delete_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().delete_resp_);
}

TEST_F(ZooKeeperFilterTest, ExistsRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathWatch("/foo", false, enumToSignedInt(OpCodes::Exists));

  testRequest(data,
              {{{"opname", "exists"}, {"path", "/foo"}, {"watch", "false"}}, {{"bytes", "21"}}},
              config_->stats().exists_rq_, 21);
  testResponse({{{"opname", "exists_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().exists_resp_);
}

TEST_F(ZooKeeperFilterTest, GetAclRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GetAcl));

  testRequest(data, {{{"opname", "getacl"}, {"path", "/foo"}}, {{"bytes", "20"}}},
              config_->stats().getacl_rq_, 20);
  testResponse({{{"opname", "getacl_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().getacl_resp_);
}

TEST_F(ZooKeeperFilterTest, SetAclRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeSetAclRequest("/foo", "digest", "passwd", -1);

  testRequest(data,
              {{{"opname", "setacl"}, {"path", "/foo"}, {"version", "-1"}}, {{"bytes", "52"}}},
              config_->stats().setacl_rq_, 52);
  testResponse({{{"opname", "setacl_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().setacl_resp_);
}

TEST_F(ZooKeeperFilterTest, SyncRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::Sync));

  testRequest(data, {{{"opname", "sync"}, {"path", "/foo"}}, {{"bytes", "20"}}},
              config_->stats().sync_rq_, 20);
  testResponse({{{"opname", "sync_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().sync_resp_);
}

TEST_F(ZooKeeperFilterTest, GetEphemeralsRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GetEphemerals));

  testRequest(data, {{{"opname", "getephemerals"}, {"path", "/foo"}}, {{"bytes", "20"}}},
              config_->stats().getephemerals_rq_, 20);
  testResponse(
      {{{"opname", "getephemerals_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().getephemerals_resp_);
}

TEST_F(ZooKeeperFilterTest, GetAllChildrenNumberRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePath("/foo", enumToSignedInt(OpCodes::GetAllChildrenNumber));

  testRequest(data, {{{"opname", "getallchildrennumber"}, {"path", "/foo"}}, {{"bytes", "20"}}},
              config_->stats().getallchildrennumber_rq_, 20);
  testResponse({{{"opname", "getallchildrennumber_resp"}, {"zxid", "2000"}, {"error", "0"}},
                {{"bytes", "20"}}},
               config_->stats().getallchildrennumber_resp_);
}

TEST_F(ZooKeeperFilterTest, CheckRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", 100, enumToSignedInt(OpCodes::Check));

  expectSetDynamicMetadata({{{"bytes", "24"}}});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().check_rq_.value());
  EXPECT_EQ(24UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());

  testResponse({{{"opname", "check_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().check_resp_);
}

TEST_F(ZooKeeperFilterTest, MultiRequest) {
  initialize();

  Buffer::OwnedImpl create1 = encodeCreateRequest("/foo", "1", CreateFlags::Persistent, true);
  Buffer::OwnedImpl create2 = encodeCreateRequest("/bar", "1", CreateFlags::Persistent, true);
  Buffer::OwnedImpl check1 = encodePathVersion("/foo", 100, enumToSignedInt(OpCodes::Check), true);
  Buffer::OwnedImpl set1 = encodeSetRequest("/bar", "2", -1, true);

  std::vector<std::pair<int32_t, Buffer::OwnedImpl>> ops;
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::Create), std::move(create1)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::Create), std::move(create2)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::Check), std::move(check1)));
  ops.push_back(std::make_pair(enumToSignedInt(OpCodes::SetData), std::move(set1)));

  Buffer::OwnedImpl data = encodeMultiRequest(ops);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().multi_rq_.value());
  EXPECT_EQ(128UL, config_->stats().request_bytes_.value());
  EXPECT_EQ(2UL, config_->stats().create_rq_.value());
  EXPECT_EQ(1UL, config_->stats().setdata_rq_.value());
  EXPECT_EQ(1UL, config_->stats().check_rq_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());

  testResponse({{{"opname", "multi_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().multi_resp_);
}

TEST_F(ZooKeeperFilterTest, ReconfigRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeReconfigRequest("s1", "s2", "s3", 1000);

  testRequest(data, {{{"opname", "reconfig"}}, {{"bytes", "38"}}}, config_->stats().reconfig_rq_,
              38);
  testResponse({{{"opname", "reconfig_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().reconfig_resp_);
}

TEST_F(ZooKeeperFilterTest, SetWatchesRequestControlXid) {
  initialize();

  const std::vector<std::string> dataw = {"/foo", "/bar"};
  const std::vector<std::string> existw = {"/foo1", "/bar1"};
  const std::vector<std::string> childw = {"/foo2", "/bar2"};

  Buffer::OwnedImpl data =
      encodeSetWatchesRequest(dataw, existw, childw, enumToSignedInt(XidCodes::SetWatchesXid));

  testRequest(data, {{{"opname", "setwatches"}}, {{"bytes", "76"}}},
              config_->stats().setwatches_rq_, 76);
  testResponse(
      {{{"opname", "setwatches_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().setwatches_resp_, enumToSignedInt(XidCodes::SetWatchesXid));
}

TEST_F(ZooKeeperFilterTest, SetWatchesRequest) {
  initialize();

  const std::vector<std::string> dataw = {"/foo", "/bar"};
  const std::vector<std::string> existw = {"/foo1", "/bar1"};
  const std::vector<std::string> childw = {"/foo2", "/bar2"};

  Buffer::OwnedImpl data = encodeSetWatchesRequest(dataw, existw, childw);

  testRequest(data, {{{"opname", "setwatches"}}, {{"bytes", "76"}}},
              config_->stats().setwatches_rq_, 76);
  testResponse(
      {{{"opname", "setwatches_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().setwatches_resp_);
}

TEST_F(ZooKeeperFilterTest, CheckWatchesRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", enumToSignedInt(WatcherType::Children),
                                             enumToSignedInt(OpCodes::CheckWatches));

  testRequest(data, {{{"opname", "checkwatches"}, {"path", "/foo"}}, {{"bytes", "24"}}},
              config_->stats().checkwatches_rq_, 24);
  testResponse(
      {{{"opname", "checkwatches_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().checkwatches_resp_);
}

TEST_F(ZooKeeperFilterTest, RemoveWatchesRequest) {
  initialize();

  Buffer::OwnedImpl data = encodePathVersion("/foo", enumToSignedInt(WatcherType::Data),
                                             enumToSignedInt(OpCodes::RemoveWatches));

  testRequest(data, {{{"opname", "removewatches"}, {"path", "/foo"}}, {{"bytes", "24"}}},
              config_->stats().removewatches_rq_, 24);
  testResponse(
      {{{"opname", "removewatches_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
      config_->stats().removewatches_resp_);
}

TEST_F(ZooKeeperFilterTest, CloseRequest) {
  initialize();

  Buffer::OwnedImpl data = encodeCloseRequest();

  testRequest(data, {{{"opname", "close"}}, {{"bytes", "12"}}}, config_->stats().close_rq_, 12);
  testResponse({{{"opname", "close_resp"}, {"zxid", "2000"}, {"error", "0"}}, {{"bytes", "20"}}},
               config_->stats().close_resp_);
}

TEST_F(ZooKeeperFilterTest, WatchEvent) {
  initialize();

  Buffer::OwnedImpl data = encodeWatchEvent("/foo", 1, 0);
  expectSetDynamicMetadata({{{"opname", "watch_event"},
                             {"event_type", "1"},
                             {"client_state", "0"},
                             {"zxid", "1000"},
                             {"error", "0"}},
                            {{"bytes", "36"}}});
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(data, false));
  EXPECT_EQ(1UL, config_->stats().watch_event_.value());
  EXPECT_EQ(36UL, config_->stats().response_bytes_.value());
  EXPECT_EQ(0UL, config_->stats().decoder_error_.value());
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
