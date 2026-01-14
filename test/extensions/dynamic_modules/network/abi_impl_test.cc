#include <vector>

#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

class DynamicModuleNetworkFilterAbiCallbackTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
        "test_filter", "", std::move(dynamic_module.value()), cluster_manager_, *stats_.rootScope(),
        main_thread_dispatcher_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    filter_ = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
    filter_->initializeInModuleFilter();

    ON_CALL(read_callbacks_, connection()).WillByDefault(testing::ReturnRef(connection_));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void TearDown() override {
    if (filter_) {
      filter_->onEvent(Network::ConnectionEvent::LocalClose);
    }
    filter_.reset();
    filter_config_.reset();
  }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  DynamicModuleNetworkFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleNetworkFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
};

// =============================================================================
// Tests for get_read_buffer_chunks with actual buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetReadBufferChunksWithData) {
  Buffer::OwnedImpl buffer("hello world");
  filter_->setCurrentReadBufferForTest(&buffer);

  size_t size =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr());
  EXPECT_GT(size, 0);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(size);
  bool success = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());
  EXPECT_TRUE(success);
  size_t total_length =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_size(filterPtr());
  EXPECT_EQ(11, total_length);
  EXPECT_GT(size, 0);

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetReadBufferChunksNullBuffer) {
  size_t size =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr());
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(0, total_length);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetReadBufferChunksEmptyBuffer) {
  Buffer::OwnedImpl empty_buffer;
  filter_->setCurrentReadBufferForTest(&empty_buffer);

  size_t size =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr());
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  bool success = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());
  EXPECT_TRUE(success);
  size_t total_length =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_size(filterPtr());
  EXPECT_EQ(0, total_length);

  filter_->setCurrentReadBufferForTest(nullptr);
}

// =============================================================================
// Tests for get_write_buffer_chunks with actual buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksWithData) {
  Buffer::OwnedImpl buffer("test data");
  filter_->setCurrentWriteBufferForTest(&buffer);

  size_t size =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr());
  EXPECT_GT(size, 0);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(size);
  bool success = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_TRUE(success);
  size_t total_length =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_size(filterPtr());
  EXPECT_EQ(9, total_length);
  EXPECT_GT(size, 0);

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksNullBuffer) {
  size_t size =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr());
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(0, total_length);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksEmptyBuffer) {
  Buffer::OwnedImpl empty_buffer;
  filter_->setCurrentWriteBufferForTest(&empty_buffer);

  size_t size =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr());
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  bool success = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_TRUE(success);
  size_t total_length =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_size(filterPtr());
  EXPECT_EQ(0, total_length);

  filter_->setCurrentWriteBufferForTest(nullptr);
}

// =============================================================================
// Tests for drain_read_buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainReadBufferWithData) {
  Buffer::OwnedImpl buffer("hello world");
  filter_->setCurrentReadBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_read_buffer(filterPtr(), 5);
  EXPECT_EQ(" world", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainReadBufferMoreThanLength) {
  Buffer::OwnedImpl buffer("hi");
  filter_->setCurrentReadBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_read_buffer(filterPtr(), 100);
  EXPECT_EQ(0, buffer.length());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainReadBufferNullBuffer) {
  envoy_dynamic_module_callback_network_filter_drain_read_buffer(filterPtr(), 10);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainReadBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentReadBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_read_buffer(filterPtr(), 0);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

// =============================================================================
// Tests for drain_write_buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainWriteBufferWithData) {
  Buffer::OwnedImpl buffer("hello world");
  filter_->setCurrentWriteBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_write_buffer(filterPtr(), 6);
  EXPECT_EQ("world", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainWriteBufferMoreThanLength) {
  Buffer::OwnedImpl buffer("hi");
  filter_->setCurrentWriteBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_write_buffer(filterPtr(), 100);
  EXPECT_EQ(0, buffer.length());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainWriteBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentWriteBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_drain_write_buffer(filterPtr(), 0);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DrainWriteBufferNullBuffer) {
  envoy_dynamic_module_callback_network_filter_drain_write_buffer(filterPtr(), 10);
}

// =============================================================================
// Tests for prepend/append read buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependReadBufferWithData) {
  Buffer::OwnedImpl buffer("world");
  filter_->setCurrentReadBufferForTest(&buffer);

  char data[] = "hello ";
  envoy_dynamic_module_type_module_buffer buf = {data, 6};
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), buf);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, AppendReadBufferWithData) {
  Buffer::OwnedImpl buffer("hello");
  filter_->setCurrentReadBufferForTest(&buffer);

  char data[] = " world";
  envoy_dynamic_module_type_module_buffer buf = {data, 6};
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), buf);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferNullBuffer) {
  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 4};
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), buf);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferNullData) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentReadBufferForTest(&buffer);

  envoy_dynamic_module_type_module_buffer buf = {nullptr, 4};
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), buf);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentReadBufferForTest(&buffer);

  char data[] = "x";
  envoy_dynamic_module_type_module_buffer buf = {data, 0};
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), buf);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

// =============================================================================
// Tests for prepend/append write buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependWriteBufferWithData) {
  Buffer::OwnedImpl buffer("world");
  filter_->setCurrentWriteBufferForTest(&buffer);

  char data[] = "hello ";
  envoy_dynamic_module_type_module_buffer buf = {data, 6};
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), buf);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, AppendWriteBufferWithData) {
  Buffer::OwnedImpl buffer("hello");
  filter_->setCurrentWriteBufferForTest(&buffer);

  char data[] = " world";
  envoy_dynamic_module_type_module_buffer buf = {data, 6};
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), buf);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferNullBuffer) {
  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 4};
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), buf);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferNullData) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentWriteBufferForTest(&buffer);

  envoy_dynamic_module_type_module_buffer buf = {nullptr, 4};
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), buf);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentWriteBufferForTest(&buffer);

  char data[] = "x";
  envoy_dynamic_module_type_module_buffer buf = {data, 0};
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), buf);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), buf);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

// =============================================================================
// Tests for write callback.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteWithData) {
  char data[] = "test data";
  envoy_dynamic_module_type_module_buffer buf = {data, 9};
  EXPECT_CALL(connection_, write(testing::_, false));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), buf, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteWithDataEndStream) {
  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 4};
  EXPECT_CALL(connection_, write(testing::_, true));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), buf, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteEndStreamOnly) {
  envoy_dynamic_module_type_module_buffer buf = {nullptr, 0};
  EXPECT_CALL(connection_, write(testing::_, true));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), buf, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteNullDataNotEndStream) {
  envoy_dynamic_module_type_module_buffer buf = {nullptr, 0};
  EXPECT_CALL(connection_, write(testing::_, testing::_)).Times(0);
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), buf, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteZeroLengthNotEndStream) {
  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 0};
  EXPECT_CALL(connection_, write(testing::_, testing::_)).Times(0);
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), buf, false);
}

// =============================================================================
// Tests for inject_read_data.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataWithData) {
  char data[] = "injected";
  envoy_dynamic_module_type_module_buffer buf = {data, 8};
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(testing::_, false));
  envoy_dynamic_module_callback_network_filter_inject_read_data(filterPtr(), buf, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataEmptyEndStream) {
  envoy_dynamic_module_type_module_buffer buf = {nullptr, 0};
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(testing::_, true));
  envoy_dynamic_module_callback_network_filter_inject_read_data(filterPtr(), buf, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 4};
  envoy_dynamic_module_callback_network_filter_inject_read_data(static_cast<void*>(filter.get()),
                                                                buf, false);
}

// =============================================================================
// Tests for inject_write_data.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataWithData) {
  char data[] = "injected";
  envoy_dynamic_module_type_module_buffer buf = {data, 8};
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(testing::_, false));
  envoy_dynamic_module_callback_network_filter_inject_write_data(filterPtr(), buf, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataEmptyEndStream) {
  envoy_dynamic_module_type_module_buffer buf = {nullptr, 0};
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(testing::_, true));
  envoy_dynamic_module_callback_network_filter_inject_write_data(filterPtr(), buf, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char data[] = "test";
  envoy_dynamic_module_type_module_buffer buf = {data, 4};
  envoy_dynamic_module_callback_network_filter_inject_write_data(static_cast<void*>(filter.get()),
                                                                 buf, false);
}

// =============================================================================
// Tests for continue_reading.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, ContinueReading) {
  EXPECT_CALL(read_callbacks_, continueReading());
  envoy_dynamic_module_callback_network_filter_continue_reading(filterPtr());
}

// =============================================================================
// Tests for close with all close types.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseFlushWrite) {
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite));
  envoy_dynamic_module_callback_network_filter_close(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_FlushWrite);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseNoFlush) {
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  envoy_dynamic_module_callback_network_filter_close(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_NoFlush);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseFlushWriteAndDelay) {
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWriteAndDelay));
  envoy_dynamic_module_callback_network_filter_close(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_FlushWriteAndDelay);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseAbort) {
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::Abort));
  envoy_dynamic_module_callback_network_filter_close(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_Abort);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseAbortReset) {
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::AbortReset));
  envoy_dynamic_module_callback_network_filter_close(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_AbortReset);
}

// =============================================================================
// Tests for get_connection_id.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetConnectionId) {
  EXPECT_CALL(connection_, id()).WillOnce(testing::Return(12345));
  uint64_t id = envoy_dynamic_module_callback_network_filter_get_connection_id(filterPtr());
  EXPECT_EQ(12345, id);
}

// =============================================================================
// Tests for get_remote_address.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRemoteAddressWithIp) {
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  auto connection_info_provider =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(result);
  EXPECT_EQ("1.2.3.4", absl::string_view(address_out.ptr, address_out.length));
  EXPECT_EQ(8080, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRemoteAddressNullIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

// =============================================================================
// Tests for get_local_address.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetLocalAddressWithIp) {
  auto address = Network::Utility::parseInternetAddressNoThrow("5.6.7.8", 9090);
  auto connection_info_provider =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(result);
  EXPECT_EQ("5.6.7.8", absl::string_view(address_out.ptr, address_out.length));
  EXPECT_EQ(9090, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetLocalAddressNullIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/local.sock");
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

// =============================================================================
// Tests for is_ssl.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, IsSslTrue) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(ssl));

  bool result = envoy_dynamic_module_callback_network_filter_is_ssl(filterPtr());
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, IsSslFalse) {
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(nullptr));

  bool result = envoy_dynamic_module_callback_network_filter_is_ssl(filterPtr());
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for disable_close.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DisableCloseTrue) {
  EXPECT_CALL(read_callbacks_, disableClose(true));
  envoy_dynamic_module_callback_network_filter_disable_close(filterPtr(), true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DisableCloseFalse) {
  EXPECT_CALL(read_callbacks_, disableClose(false));
  envoy_dynamic_module_callback_network_filter_disable_close(filterPtr(), false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, DisableCloseNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_callback_network_filter_disable_close(static_cast<void*>(filter.get()),
                                                             true);
}

// =============================================================================
// Tests for close_with_details.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseWithDetails) {
  const std::string details = "auth_failed";
  EXPECT_CALL(connection_.stream_info_,
              setConnectionTerminationDetails(absl::string_view(details)));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));

  envoy_dynamic_module_callback_network_filter_close_with_details(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_NoFlush,
      {const_cast<char*>(details.data()), details.size()});
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, CloseWithNullDetails) {
  EXPECT_CALL(connection_.stream_info_, setConnectionTerminationDetails(testing::_)).Times(0);
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));

  envoy_dynamic_module_callback_network_filter_close_with_details(
      filterPtr(), envoy_dynamic_module_type_network_connection_close_type_NoFlush, {nullptr, 0});
}

// =============================================================================
// Tests for get_requested_server_name.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRequestedServerName) {
  const std::string sni = "example.com";

  auto connection_info_provider = std::make_shared<NiceMock<Network::MockConnectionInfoProvider>>();
  EXPECT_CALL(*connection_info_provider, requestedServerName()).WillOnce(testing::Return(sni));
  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_requested_server_name(filterPtr(), &result);

  EXPECT_TRUE(ok);
  EXPECT_EQ(sni.size(), result.length);
  EXPECT_EQ(sni, std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRequestedServerNameEmpty) {
  // Test the case where SNI exists but is empty.
  const std::string empty_sni = "";

  auto connection_info_provider = std::make_shared<NiceMock<Network::MockConnectionInfoProvider>>();
  EXPECT_CALL(*connection_info_provider, requestedServerName())
      .WillOnce(testing::Return(empty_sni));
  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_requested_server_name(filterPtr(), &result);

  EXPECT_FALSE(ok);
  EXPECT_EQ(0, result.length);
  EXPECT_EQ(nullptr, result.ptr);
}

// =============================================================================
// Tests for get_direct_remote_address.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDirectRemoteAddress) {
  // MockConnection initializes with a default address. We verify the ABI function returns it.
  // The default remote address in MockConnection is typically 127.0.0.3:0 or similar.

  envoy_dynamic_module_type_envoy_buffer address_out;
  uint32_t port_out = 0;
  bool ok = envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
      filterPtr(), &address_out, &port_out);

  // Verify we got some address back (the mock's default).
  EXPECT_TRUE(ok);
  EXPECT_GT(address_out.length, 0);
  EXPECT_NE(nullptr, address_out.ptr);
  // Port might be 0 in the default mock.
}

// =============================================================================
// Tests for get_ssl_uri_sans.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslUriSans) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans = {"spiffe://example.com/sa", "spiffe://example.com/sb"};
  EXPECT_CALL(connection_, ssl()).WillRepeatedly(testing::Return(ssl));
  EXPECT_CALL(*ssl, uriSanPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  // First get the size.
  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(2, count);

  // Allocate array and populate.
  std::vector<envoy_dynamic_module_type_envoy_buffer> buffers(count);
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(filterPtr(), buffers.data());

  EXPECT_TRUE(ok);
  EXPECT_EQ("spiffe://example.com/sa", std::string(buffers[0].ptr, buffers[0].length));
  EXPECT_EQ("spiffe://example.com/sb", std::string(buffers[1].ptr, buffers[1].length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslUriSansNoSsl) {
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(nullptr));

  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(0, count);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslUriSansEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans; // Empty vector
  EXPECT_CALL(connection_, ssl()).WillRepeatedly(testing::Return(ssl));
  EXPECT_CALL(*ssl, uriSanPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(0, count);

  // Can still return OK with empty array. This returns 0.
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(filterPtr(), nullptr);
  EXPECT_TRUE(ok);
}

// =============================================================================
// Tests for get_ssl_dns_sans.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslDnsSans) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans = {"example.com", "www.example.com"};
  EXPECT_CALL(connection_, ssl()).WillRepeatedly(testing::Return(ssl));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  // First get the size.
  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(2, count);

  // Allocate array and populate.
  std::vector<envoy_dynamic_module_type_envoy_buffer> buffers(count);
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(filterPtr(), buffers.data());

  EXPECT_TRUE(ok);
  EXPECT_EQ("example.com", std::string(buffers[0].ptr, buffers[0].length));
  EXPECT_EQ("www.example.com", std::string(buffers[1].ptr, buffers[1].length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslDnsSansEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans; // Empty vector
  EXPECT_CALL(connection_, ssl()).WillRepeatedly(testing::Return(ssl));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(0, count);

  // Can still call get with empty array. This returns 0.
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(filterPtr(), nullptr);
  EXPECT_TRUE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslDnsSansNoSsl) {
  // Test the case where there's no SSL connection at all.
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(nullptr));

  // Size function should return false and set size to 0.
  size_t count = envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(0, count);

  // Get function should return 0.
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(nullptr));
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(filterPtr(), nullptr);
  EXPECT_FALSE(ok);
}

// =============================================================================
// Tests for get_ssl_subject.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslSubject) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject = "CN=example.com";
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(ssl));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(testing::ReturnRef(subject));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_subject(filterPtr(), &result);

  EXPECT_TRUE(ok);
  EXPECT_EQ(subject.size(), result.length);
  EXPECT_EQ(subject, std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslSubjectNoSsl) {
  // Test the case where there's no SSL connection at all.
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(nullptr));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_subject(filterPtr(), &result);

  EXPECT_FALSE(ok);
  EXPECT_EQ(0, result.length);
  EXPECT_EQ(nullptr, result.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSslSubjectEmpty) {
  // Test the case where SSL exists but subject is empty.
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string empty_subject = "";
  EXPECT_CALL(connection_, ssl()).WillOnce(testing::Return(ssl));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(testing::ReturnRef(empty_subject));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_filter_get_ssl_subject(filterPtr(), &result);

  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for Filter State.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetFilterStateBytes) {
  const std::string key = "test.key";
  const std::string value = "test.value";

  // FilterState in MockStreamInfo is a real FilterState object, not a mock.
  // We just need to ensure it's accessible and then test the round-trip.
  bool ok = envoy_dynamic_module_callback_network_set_filter_state_bytes(
      filterPtr(), {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(value.data()), value.size()});
  EXPECT_TRUE(ok);

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result;
  ok = envoy_dynamic_module_callback_network_get_filter_state_bytes(
      filterPtr(), {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(value.size(), result.length);
  EXPECT_EQ(value, std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetFilterStateBytesNonExisting) {
  const std::string key = "nonexistent.key";

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_get_filter_state_bytes(
      filterPtr(), {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetFilterStateBytesEmptyValue) {
  // Test setting filter state with an empty value string.
  const std::string key = "test.key";
  const std::string empty_value = "";

  bool ok = envoy_dynamic_module_callback_network_set_filter_state_bytes(
      filterPtr(), {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(empty_value.data()), empty_value.size()});
  EXPECT_TRUE(ok);

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result;
  ok = envoy_dynamic_module_callback_network_get_filter_state_bytes(
      filterPtr(), {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for Dynamic Metadata.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetAndGetDynamicMetadataString) {
  const std::string ns = "test.ns";
  const std::string key = "test.key";
  const std::string value = "test.value";

  // Set up a mutable metadata object that the mock will return.
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  // Set the metadata.
  envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, {const_cast<char*>(value.data()), value.size()});

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(value.size(), result.length);
  EXPECT_EQ(value, std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetAndGetDynamicMetadataNumber) {
  const std::string ns = "test.ns";
  const std::string key = "number.key";
  double value = 123.45;

  // Set up a mutable metadata object that the mock will return.
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  // Set the metadata.
  envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, value);

  // Verify by reading it back.
  double result = 0.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_DOUBLE_EQ(value, result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDynamicMetadataStringNonExisting) {
  const std::string ns = "test.ns";
  const std::string key = "nonexistent.key";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));

  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDynamicMetadataStringWrongType) {
  const std::string ns = "test.ns";
  const std::string key = "number.key";

  // Set up metadata with a number value, not a string.
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  // Set as number first.
  envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, 123.45);

  // Try to get as string. It should fail because it's a number.
  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDynamicMetadataNumberNonExistingNamespace) {
  const std::string ns = "nonexistent.ns";
  const std::string key = "test.key";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));

  double result = 0.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDynamicMetadataNumberNonExistingKey) {
  const std::string ns = "test.ns";
  const std::string key = "nonexistent.key";

  // Set up metadata with a different key.
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  // Set a different key.
  const std::string other_key = "other.key";
  envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(other_key.data()), other_key.size()}, 456.78);

  // Try to get non-existent key.
  double result = 0.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetDynamicMetadataNumberWrongType) {
  const std::string ns = "test.ns";
  const std::string key = "string.key";

  // Set up metadata with a string value, not a number.
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  // Set as string first.
  const std::string value = "test.value";
  envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, {const_cast<char*>(value.data()), value.size()});

  // Try to get as number. It should fail because it's a string.
  double result = 0.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetDynamicMetadataStringEmptyValue) {
  // Test setting dynamic metadata with an empty string value.
  const std::string ns = "test.ns";
  const std::string key = "empty.key";
  const std::string empty_value = "";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(empty_value.data()), empty_value.size()});

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetDynamicMetadataNumberZero) {
  // Test setting dynamic metadata with a zero number value (boundary case).
  const std::string ns = "test.ns";
  const std::string key = "zero.key";
  const double zero_value = 0.0;

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, zero_value);

  // Verify by reading it back.
  double result = 999.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_DOUBLE_EQ(zero_value, result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetDynamicMetadataNumberNegative) {
  // Test setting dynamic metadata with a negative number value (boundary case).
  const std::string ns = "test.ns";
  const std::string key = "negative.key";
  const double negative_value = -123.456;

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(connection_.stream_info_, setDynamicMetadata(ns, testing::_))
      .WillRepeatedly(testing::SaveArg<1>(&(*metadata.mutable_filter_metadata())[ns]));

  envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, negative_value);

  // Verify by reading it back.
  double result = 0.0;
  bool ok = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
      filterPtr(), {const_cast<char*>(ns.data()), ns.size()},
      {const_cast<char*>(key.data()), key.size()}, &result);
  EXPECT_TRUE(ok);
  EXPECT_DOUBLE_EQ(negative_value, result);
}

// =============================================================================
// Tests for socket options.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetAndGetSocketOptionInt) {
  const int64_t level = 1;
  const int64_t name = 2;
  const int64_t value = 12345;
  envoy_dynamic_module_callback_network_set_socket_option_int(
      filterPtr(), level, name, envoy_dynamic_module_type_socket_option_state_Prebind, value);

  int64_t result = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_network_get_socket_option_int(
      filterPtr(), level, name, envoy_dynamic_module_type_socket_option_state_Prebind, &result));
  EXPECT_EQ(value, result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, SetAndGetSocketOptionBytes) {
  const int64_t level = 3;
  const int64_t name = 4;
  const std::string value = "socket-bytes";
  envoy_dynamic_module_callback_network_set_socket_option_bytes(
      filterPtr(), level, name, envoy_dynamic_module_type_socket_option_state_Bound,
      {value.data(), value.size()});

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_network_get_socket_option_bytes(
      filterPtr(), level, name, envoy_dynamic_module_type_socket_option_state_Bound, &result));
  EXPECT_EQ(value, std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSocketOptionIntMissing) {
  int64_t value = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_network_get_socket_option_int(
      filterPtr(), 99, 100, envoy_dynamic_module_type_socket_option_state_Prebind, &value));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSocketOptionBytesMissing) {
  envoy_dynamic_module_type_envoy_buffer value_out;
  EXPECT_FALSE(envoy_dynamic_module_callback_network_get_socket_option_bytes(
      filterPtr(), 99, 100, envoy_dynamic_module_type_socket_option_state_Bound, &value_out));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, ListSocketOptions) {
  // Add two options.
  envoy_dynamic_module_callback_network_set_socket_option_int(
      filterPtr(), 10, 11, envoy_dynamic_module_type_socket_option_state_Prebind, 7);
  const std::string bytes_val = "opt-bytes";
  envoy_dynamic_module_callback_network_set_socket_option_bytes(
      filterPtr(), 12, 13, envoy_dynamic_module_type_socket_option_state_Listening,
      {bytes_val.data(), bytes_val.size()});

  const size_t size = envoy_dynamic_module_callback_network_get_socket_options_size(filterPtr());
  EXPECT_EQ(2, size);

  std::vector<envoy_dynamic_module_type_socket_option> options(size);
  envoy_dynamic_module_callback_network_get_socket_options(filterPtr(), options.data());

  // Verify first option (int).
  EXPECT_EQ(10, options[0].level);
  EXPECT_EQ(11, options[0].name);
  EXPECT_EQ(envoy_dynamic_module_type_socket_option_value_type_Int, options[0].value_type);
  EXPECT_EQ(7, options[0].int_value);

  // Verify second option (bytes).
  EXPECT_EQ(12, options[1].level);
  EXPECT_EQ(13, options[1].name);
  EXPECT_EQ(envoy_dynamic_module_type_socket_option_value_type_Bytes, options[1].value_type);
  EXPECT_EQ(bytes_val, std::string(options[1].byte_value.ptr, options[1].byte_value.length));
}

// =============================================================================
// Tests for send_http_callout.
// =============================================================================

class DynamicModuleNetworkFilterHttpCalloutTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
        "test_filter", "", std::move(dynamic_module.value()), cluster_manager_, *stats_.rootScope(),
        main_thread_dispatcher_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    filter_ = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
    filter_->initializeInModuleFilter();

    ON_CALL(read_callbacks_, connection()).WillByDefault(testing::ReturnRef(connection_));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void TearDown() override {
    if (filter_) {
      filter_->onEvent(Network::ConnectionEvent::LocalClose);
    }
    filter_.reset();
  }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  DynamicModuleNetworkFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleNetworkFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
};

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutClusterNotFound) {
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("nonexistent_cluster"))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"nonexistent_cluster", 19}, headers.data(), headers.size(),
      {nullptr, 0}, 5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound, result);
  EXPECT_EQ(0, callout_id);
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutMissingRequiredHeaders) {
  uint64_t callout_id = 0;
  // Missing :method header.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders, result);
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutCannotCreateRequest) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest, result);
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutSuccess) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(&request));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "POST", .value_length = 4},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/api/v1/data", .value_length = 12},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "api.example.com", .value_length = 15},
      {.key_ptr = "content-type",
       .key_length = 12,
       .value_ptr = "application/json",
       .value_length = 16},
  };

  const char* body_data = R"({"key": "value"})";
  envoy_dynamic_module_type_module_buffer body = {body_data, strlen(body_data)};

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), body, 5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_GT(callout_id, 0);

  EXPECT_CALL(request, cancel());
  filter_.reset();
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutSuccessWithCallback) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_GT(callout_id, 0);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a successful response. Note: on_network_filter_http_callout_done_ is nullptr
  // for network_no_op module, so the callback will silently return without calling the module.
  Http::ResponseMessagePtr response =
      std::make_unique<Http::ResponseMessageImpl>(Http::ResponseHeaderMapImpl::create());
  response->headers().setStatus(200);
  response->body().add("response body");
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onSuccess(request, std::move(response));

  filter_.reset();
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutFailureReset) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a failure with Reset reason.
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  filter_.reset();
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, SendHttpCalloutFailureExceedResponseBufferLimit) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a failure with ExceedResponseBufferLimit reason.
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onFailure(request, Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);

  filter_.reset();
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, OnBeforeFinalizeUpstreamSpanNoop) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  ASSERT_NE(nullptr, captured_callback);

  // No-op path: should be safe to call and not crash.
  Envoy::Tracing::MockSpan span;
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onBeforeFinalizeUpstreamSpan(span, nullptr);

  EXPECT_CALL(request, cancel());
  filter_.reset();
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRemoteAddressNullProvider) {
  // Return a provider whose remote address is null to hit address == nullptr.
  NiceMock<Network::MockConnectionInfoProvider> cip;
  Network::Address::InstanceConstSharedPtr null_addr;
  EXPECT_CALL(cip, remoteAddress()).WillOnce(testing::ReturnRef(null_addr));
  EXPECT_CALL(connection_, connectionInfoProvider()).WillOnce(testing::ReturnRef(cip));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSocketOptionIntNullOut) {
  auto state = envoy_dynamic_module_type_socket_option_state_Prebind;
  // null output pointer should return false
  bool ok = envoy_dynamic_module_callback_network_get_socket_option_int(filterPtr(), 1, 2, state,
                                                                        nullptr);
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetSocketOptionBytesNullOut) {
  auto state = envoy_dynamic_module_type_socket_option_state_Prebind;
  bool ok = envoy_dynamic_module_callback_network_get_socket_option_bytes(filterPtr(), 1, 2, state,
                                                                          nullptr);
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleNetworkFilterHttpCalloutTest, FilterDestructionCancelsPendingCallouts) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(&request));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_network_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);

  EXPECT_CALL(request, cancel());
  // Destroy the filter. This should cancel all pending callouts.
  filter_.reset();
}

// =============================================================================
// Tests for upstream host access.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostAddressWithHost) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto address = Network::Utility::parseInternetAddressNoThrow("10.0.0.1", 8080);
  EXPECT_CALL(*host, address()).WillRepeatedly(testing::Return(address));
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillRepeatedly(testing::Return(host));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(result);
  EXPECT_EQ("10.0.0.1", absl::string_view(address_out.ptr, address_out.length));
  EXPECT_EQ(8080, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostAddressNoHost) {
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillOnce(testing::Return(nullptr));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostAddressNonIpAddress) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/upstream.sock");
  EXPECT_CALL(*host, address()).WillRepeatedly(testing::Return(pipe));
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillRepeatedly(testing::Return(host));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostHostnameWithHost) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  std::string hostname = "backend.example.com";
  EXPECT_CALL(*host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillRepeatedly(testing::Return(host));

  envoy_dynamic_module_type_envoy_buffer hostname_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
      filterPtr(), &hostname_out);

  EXPECT_TRUE(result);
  EXPECT_EQ(hostname, absl::string_view(hostname_out.ptr, hostname_out.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostHostnameNoHost) {
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillOnce(testing::Return(nullptr));

  envoy_dynamic_module_type_envoy_buffer hostname_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
      filterPtr(), &hostname_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, hostname_out.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostHostnameEmpty) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  std::string empty_hostname;
  EXPECT_CALL(*host, hostname()).WillRepeatedly(testing::ReturnRef(empty_hostname));
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillRepeatedly(testing::Return(host));

  envoy_dynamic_module_type_envoy_buffer hostname_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
      filterPtr(), &hostname_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, hostname_out.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostHostnameNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer hostname_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
      static_cast<void*>(filter.get()), &hostname_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, hostname_out.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostClusterWithHost) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  std::string cluster_name = "my_backend_cluster";
  EXPECT_CALL(cluster_info, name()).WillRepeatedly(testing::ReturnRef(cluster_name));
  EXPECT_CALL(*host, cluster()).WillRepeatedly(testing::ReturnRef(cluster_info));
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillRepeatedly(testing::Return(host));

  envoy_dynamic_module_type_envoy_buffer cluster_name_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
      filterPtr(), &cluster_name_out);

  EXPECT_TRUE(result);
  EXPECT_EQ(cluster_name, absl::string_view(cluster_name_out.ptr, cluster_name_out.length));
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostClusterNoHost) {
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillOnce(testing::Return(nullptr));

  envoy_dynamic_module_type_envoy_buffer cluster_name_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
      filterPtr(), &cluster_name_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, cluster_name_out.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetUpstreamHostClusterNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer cluster_name_out = {nullptr, 0};
  bool result = envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
      static_cast<void*>(filter.get()), &cluster_name_out);

  EXPECT_FALSE(result);
  EXPECT_EQ(nullptr, cluster_name_out.ptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, HasUpstreamHostTrue) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillOnce(testing::Return(host));

  bool result = envoy_dynamic_module_callback_network_filter_has_upstream_host(filterPtr());
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, HasUpstreamHostFalse) {
  EXPECT_CALL(read_callbacks_, upstreamHost()).WillOnce(testing::Return(nullptr));

  bool result = envoy_dynamic_module_callback_network_filter_has_upstream_host(filterPtr());
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, HasUpstreamHostNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  bool result = envoy_dynamic_module_callback_network_filter_has_upstream_host(
      static_cast<void*>(filter.get()));
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for startUpstreamSecureTransport (StartTLS).
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, StartUpstreamSecureTransportSuccess) {
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));

  bool result =
      envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(filterPtr());
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, StartUpstreamSecureTransportFailure) {
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(false));

  bool result =
      envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(filterPtr());
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, StartUpstreamSecureTransportNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  bool result = envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
      static_cast<void*>(filter.get()));
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for network filter scheduler callbacks.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, NetworkFilterSchedulerNewDelete) {
  // Set up the dispatcher for the filter via connection.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(connection_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto scheduler = envoy_dynamic_module_callback_network_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  envoy_dynamic_module_callback_network_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, NetworkFilterSchedulerCommit) {
  // Set up the dispatcher for the filter via connection.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(connection_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto scheduler = envoy_dynamic_module_callback_network_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Expect the callback to be posted.
  EXPECT_CALL(worker_dispatcher, post(_));

  envoy_dynamic_module_callback_network_filter_scheduler_commit(scheduler, 12345);

  // Clean up.
  envoy_dynamic_module_callback_network_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, NetworkFilterConfigSchedulerNewDelete) {
  auto scheduler = envoy_dynamic_module_callback_network_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  envoy_dynamic_module_callback_network_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, NetworkFilterConfigSchedulerCommit) {
  auto scheduler = envoy_dynamic_module_callback_network_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Expect the callback to be posted.
  EXPECT_CALL(main_thread_dispatcher_, post(_));

  envoy_dynamic_module_callback_network_filter_config_scheduler_commit(scheduler, 54321);

  // Clean up.
  envoy_dynamic_module_callback_network_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, NetworkFilterSchedulerCommitInvokesOnScheduled) {
  // Set up the dispatcher for the filter via connection.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(connection_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto scheduler = envoy_dynamic_module_callback_network_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback and invoke it to verify onScheduled is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(worker_dispatcher, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_network_filter_scheduler_commit(scheduler, 789);

  // Invoke the captured callback to simulate the dispatcher running the event.
  // This should call filter_->onScheduled(789), which invokes the module's on_scheduled hook.
  // Since the no_op module's on_scheduled is a no-op, we just verify it doesn't crash.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_network_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest,
       NetworkFilterConfigSchedulerCommitInvokesOnScheduled) {
  auto scheduler = envoy_dynamic_module_callback_network_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback and invoke it to verify onScheduled is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(main_thread_dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_network_filter_config_scheduler_commit(scheduler, 999);

  // Invoke the captured callback to simulate the dispatcher running the event.
  // This should call filter_config_->onScheduled(999), which invokes the module's
  // on_config_scheduled hook. Since the no_op module's hook is a no-op, we just verify it doesn't
  // crash.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_network_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest,
       NetworkFilterSchedulerCommitAfterFilterDestroyedDoesNotCrash) {
  // Set up the dispatcher for the filter via connection.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(connection_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto scheduler = envoy_dynamic_module_callback_network_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(worker_dispatcher, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_network_filter_scheduler_commit(scheduler, 123);

  // Destroy the filter before invoking the callback.
  filter_.reset();

  // The callback should not crash even though the filter is destroyed.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_network_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest,
       NetworkFilterConfigSchedulerCommitAfterConfigDestroyedDoesNotCrash) {
  auto scheduler = envoy_dynamic_module_callback_network_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(main_thread_dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_network_filter_config_scheduler_commit(scheduler, 456);

  // Destroy the filter and config before invoking the callback.
  filter_.reset();
  filter_config_.reset();

  // The callback should not crash even though the config is destroyed.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_network_filter_config_scheduler_delete(scheduler);
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
