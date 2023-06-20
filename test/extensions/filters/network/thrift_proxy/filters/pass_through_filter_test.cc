#include <memory>
#include <string>

#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

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
  EXPECT_TRUE(filter_->passthroughSupported());

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

class ThriftPassThroughEncoderFilterTest : public testing::Test {
public:
  class Filter : public PassThroughEncoderFilter {
  public:
    EncoderFilterCallbacks* encoderFilterCallbacks() { return encoder_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->setEncoderFilterCallbacks(filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockEncoderFilterCallbacks> filter_callbacks_;
  ThriftProxy::MessageMetadataSharedPtr request_metadata_;
};

// Tests that each method returns ThriftProxy::FilterStatus::Continue.
TEST_F(ThriftPassThroughEncoderFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();

  EXPECT_EQ(&filter_callbacks_, filter_->encoderFilterCallbacks());
  EXPECT_TRUE(filter_->passthroughSupported());

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

class ThriftPassThroughBidirectionalFilterTest : public testing::Test {
public:
  class Filter : public PassThroughBidirectionalFilter {
  public:
    DecoderFilterCallbacks* decoderFilterCallbacks() { return decoder_callbacks_; }
    EncoderFilterCallbacks* encoderFilterCallbacks() { return encoder_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockEncoderFilterCallbacks> encoder_filter_callbacks_;
  NiceMock<MockDecoderFilterCallbacks> decoder_filter_callbacks_;
  ThriftProxy::MessageMetadataSharedPtr request_metadata_;
};

// Tests that each method returns ThriftProxy::FilterStatus::Continue.
TEST_F(ThriftPassThroughBidirectionalFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();

  EXPECT_EQ(&decoder_filter_callbacks_, filter_->decoderFilterCallbacks());
  EXPECT_TRUE(filter_->decodePassthroughSupported());
  EXPECT_TRUE(filter_->encodePassthroughSupported());

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeTransportBegin(request_metadata_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeMessageBegin(request_metadata_));
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeStructBegin(dummy_str));
  }
  {
    std::string dummy_str = "dummy";
    ThriftProxy::FieldType dummy_ft{ThriftProxy::FieldType::I32};
    int16_t dummy_id{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->decodeFieldBegin(dummy_str, dummy_ft, dummy_id));
  }
  {
    bool dummy_val{false};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeBoolValue(dummy_val));
  }
  {
    uint8_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeByteValue(dummy_val));
  }
  {
    int16_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeInt16Value(dummy_val));
  }
  {
    int32_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeInt32Value(dummy_val));
  }
  {
    int64_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeInt64Value(dummy_val));
  }
  {
    double dummy_val{0.0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeDoubleValue(dummy_val));
  }
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeStringValue(dummy_str));
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->decodeMapBegin(dummy_ft, dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeMapEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeListBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeListEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeSetBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeSetEnd());
  }
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeStructEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeFieldEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeMessageEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->decodeTransportEnd());

  // Encoding phase.
  EXPECT_EQ(&encoder_filter_callbacks_, filter_->encoderFilterCallbacks());

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeTransportBegin(request_metadata_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeMessageBegin(request_metadata_));
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeStructBegin(dummy_str));
  }
  {
    std::string dummy_str = "dummy";
    ThriftProxy::FieldType dummy_ft{ThriftProxy::FieldType::I32};
    int16_t dummy_id{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->encodeFieldBegin(dummy_str, dummy_ft, dummy_id));
  }
  {
    bool dummy_val{false};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeBoolValue(dummy_val));
  }
  {
    uint8_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeByteValue(dummy_val));
  }
  {
    int16_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeInt16Value(dummy_val));
  }
  {
    int32_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeInt32Value(dummy_val));
  }
  {
    int64_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeInt64Value(dummy_val));
  }
  {
    double dummy_val{0.0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeDoubleValue(dummy_val));
  }
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeStringValue(dummy_str));
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->encodeMapBegin(dummy_ft, dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeMapEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeListBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeListEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeSetBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeSetEnd());
  }
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeStructEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeFieldEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeMessageEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->encodeTransportEnd());

  EXPECT_NO_THROW(filter_->onDestroy());
}

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
