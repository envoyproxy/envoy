#include <string>

#include "source/common/common/utility.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/router/router_filter_interface.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

class UpstreamRequestTest : public testing::Test {
public:
  UpstreamRequestTest() {
    HttpTestUtility::addDefaultHeaders(downstream_request_header_map_);
    ON_CALL(router_filter_interface_, downstreamHeaders())
        .WillByDefault(Return(&downstream_request_header_map_));
  }

  Http::TestRequestHeaderMapImpl downstream_request_header_map_{};
  NiceMock<MockRouterFilterInterface> router_filter_interface_;
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};
};

TEST_F(UpstreamRequestTest, CopyDynamicMetaData) {
  Event::SimulatedTimeSystem test_time_;
  StreamInfo::StreamInfoImpl l4_stream_info(test_time_, nullptr);
  auto connection = router_filter_interface_.callbacks_.connection();
  auto data_value = 8888;
  ProtobufWkt::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  auto val = ProtobufWkt::Value();
  val.set_number_value(data_value);
  fields.insert({"envoy.common.test", val});
  const_cast<Network::Connection*>(connection)
      ->streamInfo()
      .setDynamicMetadata("envoy.common.shared_with_upstream", metadata);
  upstream_request_.copyDynamicMetaDataFromL4Downstream(l4_stream_info);
  auto down2up =
      l4_stream_info.dynamicMetadata().filter_metadata().find("envoy.common.shared_with_upstream");
  auto id = std::to_string(connection->id());
  EXPECT_TRUE(down2up != l4_stream_info.dynamicMetadata().filter_metadata().end());
  EXPECT_TRUE(down2up->second.fields().find(id) != down2up->second.fields().end());
  const ProtobufWkt::Struct& data = down2up->second.fields().find(id)->second.struct_value();
  EXPECT_TRUE(data.fields().find("envoy.common.test") != data.fields().end());
  EXPECT_TRUE(data.fields().find("envoy.common.test")->second.number_value() == data_value);
}

TEST_F(UpstreamRequestTest, CopyDynamicMetaData1) {
  Event::SimulatedTimeSystem test_time_;
  StreamInfo::StreamInfoImpl l4_stream_info(test_time_, nullptr);
  auto connection = router_filter_interface_.callbacks_.connection();

  /*There is a existing metadata in upstream metadata*/
  auto data_value1 = 8888;
  ProtobufWkt::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  auto val = ProtobufWkt::Value();
  val.set_number_value(data_value1);
  fields.insert({"envoy.common.test", val});
  ProtobufWkt::Struct metadata1;
  auto& fields1 = *metadata1.mutable_fields();
  ProtobufWkt::Value val1;
  val1 = ValueUtil::structValue(metadata);
  auto existing_id = "1000";
  fields1.insert({existing_id, val1});
  l4_stream_info.setDynamicMetadata("envoy.common.shared_with_upstream", metadata1);

  /*copy the metadata from another downstream connection*/
  auto data_value2 = 9999;
  ProtobufWkt::Struct metadata2;
  auto& fields2 = *metadata2.mutable_fields();
  auto val2 = ProtobufWkt::Value();
  val2.set_number_value(data_value2);
  fields2.insert({"envoy.common.test", val2});
  const_cast<Network::Connection*>(connection)
      ->streamInfo()
      .setDynamicMetadata("envoy.common.shared_with_upstream", metadata2);

  upstream_request_.copyDynamicMetaDataFromL4Downstream(l4_stream_info);
  auto down2up =
      l4_stream_info.dynamicMetadata().filter_metadata().find("envoy.common.shared_with_upstream");
  EXPECT_TRUE(down2up != l4_stream_info.dynamicMetadata().filter_metadata().end());

  auto id = std::to_string(connection->id());
  EXPECT_TRUE(down2up->second.fields().find(id) != down2up->second.fields().end());
  const ProtobufWkt::Struct& data = down2up->second.fields().find(id)->second.struct_value();
  EXPECT_TRUE(data.fields().find("envoy.common.test") != data.fields().end());
  EXPECT_TRUE(data.fields().find("envoy.common.test")->second.number_value() == data_value2);

  EXPECT_TRUE(down2up->second.fields().find(existing_id) != down2up->second.fields().end());
  const ProtobufWkt::Struct& data1 =
      down2up->second.fields().find(existing_id)->second.struct_value();
  EXPECT_TRUE(data1.fields().find("envoy.common.test") != data1.fields().end());
  EXPECT_TRUE(data1.fields().find("envoy.common.test")->second.number_value() == data_value1);
}

// UpstreamRequest is responsible processing for passing 101 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode101UpgradeHeaders) {
  auto upgrade_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "101"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(upgrade_headers), false);
}

// UpstreamRequest is responsible for ignoring non-{100,101} 1xx headers.
TEST_F(UpstreamRequestTest, IgnoreOther1xxHeaders) {
  auto other_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "102"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _)).Times(0);
  upstream_request_.decodeHeaders(std::move(other_headers), false);
}

// UpstreamRequest is responsible processing for passing 200 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode200UpgradeHeaders) {
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(response_headers), false);
}

// UpstreamRequest dumpState without allocating memory.
TEST_F(UpstreamRequestTest, DumpsStateWithoutAllocatingMemory) {
  // Set up router filter
  auto connection_info_provider =
      router_filter_interface_.client_connection_.stream_info_.downstream_connection_info_provider_;
  connection_info_provider->setRemoteAddress(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));
  connection_info_provider->setLocalAddress(
      Network::Utility::parseInternetAddressAndPort("5.6.7.8:5678"));
  connection_info_provider->setDirectRemoteAddressForTest(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));

  // Dump State
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  Stats::TestUtil::MemoryTest memory_test;
  upstream_request_.dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check Contents
  EXPECT_THAT(ostream.contents(), HasSubstr("UpstreamRequest "));
  EXPECT_THAT(ostream.contents(), HasSubstr("addressProvider: \n  ConnectionInfoSetterImpl "));
  EXPECT_THAT(ostream.contents(), HasSubstr("request_headers: \n"));
}

} // namespace
} // namespace Router
} // namespace Envoy
