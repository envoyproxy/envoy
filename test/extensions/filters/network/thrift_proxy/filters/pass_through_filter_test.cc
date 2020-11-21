#include <memory>
#include <string>

#include "extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

using namespace Envoy::Extensions::NetworkFilters;

class ThriftPassThroughDecoderFilterTest : public testing::Test {
public:
  class Filter : public PassThroughDecoderFilter {
  public:
    DecoderFilterCallbacks* decoderFilterCallbacks() { return decoder_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockDecoderFilterCallbacks> filter_callbacks_;
  ThriftProxy::MessageMetadataSharedPtr request_metadata_;
};

// Tests that each method returns ThriftProxy::FilterStatus::Continue.
TEST_F(ThriftPassThroughDecoderFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();

  EXPECT_EQ(&filter_callbacks_, filter_->decoderFilterCallbacks());

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(request_metadata_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->structBegin(dummy_str));
  }
  {
    std::string dummy_str = "dummy";
    ThriftProxy::FieldType dummy_ft{ThriftProxy::FieldType::I32};
    int16_t dummy_id{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->fieldBegin(dummy_str, dummy_ft, dummy_id));
  }
  {
    bool dummy_val{false};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->boolValue(dummy_val));
  }
  {
    uint8_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->byteValue(dummy_val));
  }
  {
    int16_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int16Value(dummy_val));
  }
  {
    int32_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int32Value(dummy_val));
  }
  {
    int64_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int64Value(dummy_val));
  }
  {
    double dummy_val{0.0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->doubleValue(dummy_val));
  }
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->stringValue(dummy_str));
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->mapBegin(dummy_ft, dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->mapEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->listBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->listEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->setBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->setEnd());
  }
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->structEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->fieldEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportEnd());

  EXPECT_NO_THROW(filter_->onDestroy());
}

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
