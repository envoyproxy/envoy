#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "contrib/istio/filters/network/metadata_exchange/source/metadata_exchange.h"
#include "contrib/istio/filters/network/metadata_exchange/source/metadata_exchange_initial_header.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Protobuf::util::MessageDifferencer;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetadataExchange {
namespace {

MATCHER_P(MapEq, rhs, "") { return MessageDifferencer::Equals(arg, rhs); }

void constructProxyHeaderData(::Envoy::Buffer::OwnedImpl& serialized_header,
                              Envoy::Protobuf::Any& proxy_header,
                              MetadataExchangeInitialHeader* initial_header) {
  std::string serialized_proxy_header = proxy_header.SerializeAsString();
  memset(initial_header, 0, sizeof(MetadataExchangeInitialHeader));
  initial_header->magic = absl::ghtonl(MetadataExchangeInitialHeader::magic_number);
  initial_header->data_size = absl::ghtonl(serialized_proxy_header.length());
  serialized_header.add(::Envoy::Buffer::OwnedImpl{absl::string_view(
      reinterpret_cast<const char*>(initial_header), sizeof(MetadataExchangeInitialHeader))});
  serialized_header.add(::Envoy::Buffer::OwnedImpl{serialized_proxy_header});
}

} // namespace

class MetadataExchangeFilterTest : public testing::Test {
public:
  MetadataExchangeFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() { initialize(absl::flat_hash_set<std::string>()); }

  void initialize(absl::flat_hash_set<std::string> additional_labels) {
    config_ = std::make_shared<MetadataExchangeConfig>(
        stat_prefix_, "istio2", FilterDirection::Downstream, false, additional_labels, context_,
        *scope_.rootScope());
    filter_ = std::make_unique<MetadataExchangeFilter>(config_, local_info_);
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_filter_callbacks_);
    metadata_node_.set_id("test");
    auto node_metadata_map = metadata_node_.mutable_metadata()->mutable_fields();
    (*node_metadata_map)["namespace"].set_string_value("default");
    (*node_metadata_map)["labels"].set_string_value("{app, details}");
    EXPECT_CALL(read_filter_callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(local_info_, node()).WillRepeatedly(ReturnRef(metadata_node_));
  }

  void initializeStructValues() {
    (*details_value_.mutable_fields())["namespace"].set_string_value("default");
    (*details_value_.mutable_fields())["labels"].set_string_value("{app, details}");

    (*productpage_value_.mutable_fields())["namespace"].set_string_value("default");
    (*productpage_value_.mutable_fields())["labels"].set_string_value("{app, productpage}");
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Envoy::Protobuf::Struct details_value_;
  Envoy::Protobuf::Struct productpage_value_;
  MetadataExchangeConfigSharedPtr config_;
  std::unique_ptr<MetadataExchangeFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test.metadataexchange"};
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_filter_callbacks_;
  Network::MockConnection connection_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Node metadata_node_;
};

TEST_F(MetadataExchangeFilterTest, MetadataExchangeFound) {
  initialize();
  initializeStructValues();

  EXPECT_CALL(read_filter_callbacks_.connection_, nextProtocol()).WillRepeatedly(Return("istio2"));

  ::Envoy::Buffer::OwnedImpl data;
  MetadataExchangeInitialHeader initial_header;
  Envoy::Protobuf::Any productpage_any_value;
  productpage_any_value.set_type_url("type.googleapis.com/google.protobuf.Struct");
  *productpage_any_value.mutable_value() = productpage_value_.SerializeAsString();
  constructProxyHeaderData(data, productpage_any_value, &initial_header);
  ::Envoy::Buffer::OwnedImpl world{"world"};
  data.add(world);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(data.toString(), "world");

  EXPECT_EQ(0UL, config_->stats().initial_header_not_found_.value());
  EXPECT_EQ(0UL, config_->stats().header_not_found_.value());
  EXPECT_EQ(1UL, config_->stats().alpn_protocol_found_.value());
}

TEST_F(MetadataExchangeFilterTest, MetadataExchangeAdditionalLabels) {
  initialize({"role"});
  initializeStructValues();

  EXPECT_CALL(read_filter_callbacks_.connection_, nextProtocol()).WillRepeatedly(Return("istio2"));

  ::Envoy::Buffer::OwnedImpl data;
  MetadataExchangeInitialHeader initial_header;
  Envoy::Protobuf::Any productpage_any_value;
  productpage_any_value.set_type_url("type.googleapis.com/google.protobuf.Struct");
  *productpage_any_value.mutable_value() = productpage_value_.SerializeAsString();
  constructProxyHeaderData(data, productpage_any_value, &initial_header);
  ::Envoy::Buffer::OwnedImpl world{"world"};
  data.add(world);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(data.toString(), "world");

  EXPECT_EQ(0UL, config_->stats().initial_header_not_found_.value());
  EXPECT_EQ(0UL, config_->stats().header_not_found_.value());
  EXPECT_EQ(1UL, config_->stats().alpn_protocol_found_.value());
}

TEST_F(MetadataExchangeFilterTest, MetadataExchangeNotFound) {
  initialize();

  EXPECT_CALL(read_filter_callbacks_.connection_, nextProtocol()).WillRepeatedly(Return("istio"));

  ::Envoy::Buffer::OwnedImpl data{};
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(1UL, config_->stats().alpn_protocol_not_found_.value());
}

} // namespace MetadataExchange
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
