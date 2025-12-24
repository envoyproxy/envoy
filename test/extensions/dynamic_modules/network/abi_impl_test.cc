#include <vector>

#include "source/common/network/address_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

class DynamicModuleNetworkFilterAbiCallbackTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status =
        newDynamicModuleNetworkFilterConfig("test_filter", "", std::move(dynamic_module.value()));
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    filter_ = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
    filter_->initializeInModuleFilter();

    ON_CALL(read_callbacks_, connection()).WillByDefault(testing::ReturnRef(connection_));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void TearDown() override { filter_.reset(); }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

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

  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr(), &size);
  EXPECT_TRUE(ok);
  EXPECT_GT(size, 0);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(size);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(11, total_length);
  EXPECT_GT(size, 0);

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetReadBufferChunksNullBuffer) {
  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr(), &size);
  EXPECT_FALSE(ok);
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(0, total_length);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetReadBufferChunksEmptyBuffer) {
  Buffer::OwnedImpl empty_buffer;
  filter_->setCurrentReadBufferForTest(&empty_buffer);

  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(filterPtr(), &size);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(0, total_length);

  filter_->setCurrentReadBufferForTest(nullptr);
}

// =============================================================================
// Tests for get_write_buffer_chunks with actual buffer.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksWithData) {
  Buffer::OwnedImpl buffer("test data");
  filter_->setCurrentWriteBufferForTest(&buffer);

  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr(), &size);
  EXPECT_TRUE(ok);
  EXPECT_GT(size, 0);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(size);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(9, total_length);
  EXPECT_GT(size, 0);

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksNullBuffer) {
  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr(), &size);
  EXPECT_FALSE(ok);
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

  EXPECT_EQ(0, total_length);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetWriteBufferChunksEmptyBuffer) {
  Buffer::OwnedImpl empty_buffer;
  filter_->setCurrentWriteBufferForTest(&empty_buffer);

  size_t size = 0;
  bool ok =
      envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(filterPtr(), &size);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> result_buffer(1);
  size_t total_length = envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
      filterPtr(), result_buffer.data());

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
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), data, 6);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, AppendReadBufferWithData) {
  Buffer::OwnedImpl buffer("hello");
  filter_->setCurrentReadBufferForTest(&buffer);

  char data[] = " world";
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), data, 6);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferNullBuffer) {
  char data[] = "test";
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), data, 4);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), data, 4);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferNullData) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentReadBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), nullptr, 4);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), nullptr, 4);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentReadBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendReadBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentReadBufferForTest(&buffer);

  char data[] = "x";
  envoy_dynamic_module_callback_network_filter_prepend_read_buffer(filterPtr(), data, 0);
  envoy_dynamic_module_callback_network_filter_append_read_buffer(filterPtr(), data, 0);
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
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), data, 6);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, AppendWriteBufferWithData) {
  Buffer::OwnedImpl buffer("hello");
  filter_->setCurrentWriteBufferForTest(&buffer);

  char data[] = " world";
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), data, 6);
  EXPECT_EQ("hello world", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferNullBuffer) {
  char data[] = "test";
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), data, 4);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), data, 4);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferNullData) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentWriteBufferForTest(&buffer);

  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), nullptr, 4);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), nullptr, 4);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, PrependAppendWriteBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  filter_->setCurrentWriteBufferForTest(&buffer);

  char data[] = "x";
  envoy_dynamic_module_callback_network_filter_prepend_write_buffer(filterPtr(), data, 0);
  envoy_dynamic_module_callback_network_filter_append_write_buffer(filterPtr(), data, 0);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentWriteBufferForTest(nullptr);
}

// =============================================================================
// Tests for write callback.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteWithData) {
  char data[] = "test data";
  EXPECT_CALL(connection_, write(testing::_, false));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), data, 9, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteWithDataEndStream) {
  char data[] = "test";
  EXPECT_CALL(connection_, write(testing::_, true));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), data, 4, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteEndStreamOnly) {
  EXPECT_CALL(connection_, write(testing::_, true));
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), nullptr, 0, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteNullDataNotEndStream) {
  EXPECT_CALL(connection_, write(testing::_, testing::_)).Times(0);
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), nullptr, 0, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, WriteZeroLengthNotEndStream) {
  char data[] = "test";
  EXPECT_CALL(connection_, write(testing::_, testing::_)).Times(0);
  envoy_dynamic_module_callback_network_filter_write(filterPtr(), data, 0, false);
}

// =============================================================================
// Tests for inject_read_data.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataWithData) {
  char data[] = "injected";
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(testing::_, false));
  envoy_dynamic_module_callback_network_filter_inject_read_data(filterPtr(), data, 8, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataEmptyEndStream) {
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(testing::_, true));
  envoy_dynamic_module_callback_network_filter_inject_read_data(filterPtr(), nullptr, 0, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectReadDataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char data[] = "test";
  envoy_dynamic_module_callback_network_filter_inject_read_data(static_cast<void*>(filter.get()),
                                                                data, 4, false);
}

// =============================================================================
// Tests for inject_write_data.
// =============================================================================

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataWithData) {
  char data[] = "injected";
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(testing::_, false));
  envoy_dynamic_module_callback_network_filter_inject_write_data(filterPtr(), data, 8, false);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataEmptyEndStream) {
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(testing::_, true));
  envoy_dynamic_module_callback_network_filter_inject_write_data(filterPtr(), nullptr, 0, true);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, InjectWriteDataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char data[] = "test";
  envoy_dynamic_module_callback_network_filter_inject_write_data(static_cast<void*>(filter.get()),
                                                                 data, 4, false);
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

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  uint32_t port_out = 0;
  size_t len = envoy_dynamic_module_callback_network_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_GT(len, 0);
  EXPECT_NE(nullptr, address_out);
  EXPECT_EQ(8080, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetRemoteAddressNullIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  uint32_t port_out = 0;
  size_t len = envoy_dynamic_module_callback_network_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_EQ(0, len);
  EXPECT_EQ(nullptr, address_out);
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

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  uint32_t port_out = 0;
  size_t len = envoy_dynamic_module_callback_network_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_GT(len, 0);
  EXPECT_NE(nullptr, address_out);
  EXPECT_EQ(9090, port_out);
}

TEST_F(DynamicModuleNetworkFilterAbiCallbackTest, GetLocalAddressNullIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/local.sock");
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  EXPECT_CALL(connection_, connectionInfoProvider())
      .WillOnce(testing::ReturnRef(*connection_info_provider));

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  uint32_t port_out = 0;
  size_t len = envoy_dynamic_module_callback_network_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_EQ(0, len);
  EXPECT_EQ(nullptr, address_out);
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

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
