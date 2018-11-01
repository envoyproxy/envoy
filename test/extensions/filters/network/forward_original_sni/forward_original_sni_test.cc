#include "common/stream_info/forward_requested_server_name.h"

#include "extensions/filters/network/forward_original_sni/config.h"
#include "extensions/filters/network/forward_original_sni/forward_original_sni.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ForwardOriginalSni {

// Test that a ForwardOriginalSni filter config works.
TEST(ForwardOriginalSni, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ForwardOriginalSniNetworkFilterConfigFactory factory;

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test that forward requested server name is set if SNI is available
TEST(ForwardOriginalSni, SetForwardRequestedServerNameOnlyIfSniIsPresent) {
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(filter_callbacks.connection_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  ON_CALL(Const(filter_callbacks.connection_), streamInfo()).WillByDefault(ReturnRef(stream_info));

  ForwardOriginalSniFilter filter;
  filter.initializeReadFilterCallbacks(filter_callbacks);

  // no sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return(EMPTY_STRING));
    filter.onNewConnection();

    EXPECT_FALSE(stream_info.filterState().hasData<ForwardRequestedServerName>(
        ForwardRequestedServerName::Key));
  }

  // with sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return("www.example.com"));
    filter.onNewConnection();

    EXPECT_TRUE(stream_info.filterState().hasaData<ForwardRequestedServerName>(
        ForwardRequestedServerName::Key));

    auto forward_requested_server_name =
        stream_info.filterState().getDataReadOnly<ForwardRequestedServerName>(
            ForwardRequestedServerName::Key);
    EXPECT_EQ(forward_requested_server_name.value(), "www.example.com");
  }
}

} // namespace ForwardOriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
