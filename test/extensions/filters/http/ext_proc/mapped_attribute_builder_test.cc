#include "envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3/mapped_attribute_builder.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/mapped_attribute_builder.h"

#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using Envoy::Http::ExternalProcessing::MappedAttributeBuilder;
using MappedAttributeBuilderProto = ::envoy::extensions::http::ext_proc::
    processing_request_modifiers::mapped_attribute_builder::v3::MappedAttributeBuilder;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class MappedAttributeBuilderTest : public testing::Test {
protected:
  void SetUp() override {
    auto builder_ptr = Filters::Common::Expr::createBuilder({});
    expr_builder_ =
        std::make_shared<Filters::Common::Expr::BuilderInstance>(std::move(builder_ptr));
  }

  void initialize(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, proto_config_);
    builder_ =
        std::make_unique<MappedAttributeBuilder>(proto_config_, expr_builder_, factory_context_);
    EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  }

  MappedAttributeBuilderProto proto_config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder_;
  std::unique_ptr<ProcessingRequestModifier> builder_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<::Envoy::Http::MockStreamEncoderFilterCallbacks> callbacks_;
  envoy::config::core::v3::Metadata metadata_;
};

#if defined(USE_CEL_PARSER)

TEST_F(MappedAttributeBuilderTest, TwoKeysWithSameValue) {
  initialize(R"EOF(
  mapped_request_attributes:
    "remapped.path": "request.path"
    "remapped.uri": "request.path"
    "remapped.address": "source.address"
  )EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/foo"}, {":method", "POST"}};
  stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4"));
  stream_info_.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("5.6.7.8"));

  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::INBOUND,
      &callbacks_,
      &request_headers,
      nullptr,
      nullptr,
  };
  envoy::service::ext_proc::v3::ProcessingRequest req;
  EXPECT_TRUE(builder_->modifyRequest(params, req));

  const auto& attributes = req.attributes().at("envoy.filters.http.ext_proc");
  EXPECT_EQ(3, attributes.fields_size());
  EXPECT_TRUE(attributes.fields().contains("remapped.path"));
  EXPECT_EQ("/foo", attributes.fields().at("remapped.path").string_value());
  EXPECT_TRUE(attributes.fields().contains("remapped.uri"));
  EXPECT_EQ("/foo", attributes.fields().at("remapped.uri").string_value());
  EXPECT_TRUE(attributes.fields().contains("remapped.address"));
  EXPECT_EQ("1.2.3.4:0", attributes.fields().at("remapped.address").string_value());
}

TEST_F(MappedAttributeBuilderTest, CelFilterState) {
  initialize(R"EOF(
  mapped_request_attributes:
    "filter_state_key": "filter_state['fs_key']"
  )EOF");

  Http::TestRequestHeaderMapImpl request_headers;
  stream_info_.filter_state_->setData("fs_key",
                                      std::make_unique<Router::StringAccessorImpl>("fs_value"),
                                      StreamInfo::FilterState::StateType::ReadOnly);

  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::INBOUND,
      &callbacks_,
      &request_headers,
      nullptr,
      nullptr,
  };
  envoy::service::ext_proc::v3::ProcessingRequest req;
  EXPECT_TRUE(builder_->modifyRequest(params, req));

  const auto& attributes = req.attributes().at("envoy.filters.http.ext_proc");
  EXPECT_EQ(1, attributes.fields_size());
  EXPECT_TRUE(attributes.fields().contains("filter_state_key"));
  EXPECT_EQ("fs_value", attributes.fields().at("filter_state_key").string_value());
}

TEST_F(MappedAttributeBuilderTest, CelDynamicMetadata) {
  initialize(R"EOF(
  mapped_request_attributes:
    "metadata_key": "metadata.filter_metadata['envoy.testing_namespace']['testing_key']"
  )EOF");

  Protobuf::Value metadata_value;
  metadata_value.set_string_value("metadata_value");
  Protobuf::Struct metadata;
  metadata.mutable_fields()->insert({"testing_key", metadata_value});
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.testing_namespace"] = metadata;

  Http::TestRequestHeaderMapImpl request_headers;
  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::INBOUND,
      &callbacks_,
      &request_headers,
      nullptr,
      nullptr,
  };
  envoy::service::ext_proc::v3::ProcessingRequest req;
  EXPECT_TRUE(builder_->modifyRequest(params, req));

  const auto& attributes = req.attributes().at("envoy.filters.http.ext_proc");
  EXPECT_EQ(1, attributes.fields_size());
  EXPECT_TRUE(attributes.fields().contains("metadata_key"));
  EXPECT_EQ("metadata_value", attributes.fields().at("metadata_key").string_value());
}

TEST_F(MappedAttributeBuilderTest, ModifiedOnceForInbound) {
  initialize(R"EOF(
  mapped_request_attributes:
    "key": "request.path"
  )EOF");

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::INBOUND,
      &callbacks_,
      &request_headers,
      nullptr,
      nullptr,
  };
  envoy::service::ext_proc::v3::ProcessingRequest req;

  // First call should succeed and modify the request
  EXPECT_TRUE(builder_->modifyRequest(params, req));
  EXPECT_EQ(1, req.attributes().at("envoy.filters.http.ext_proc").fields_size());

  // Second call should do nothing and return false
  envoy::service::ext_proc::v3::ProcessingRequest req2;
  EXPECT_FALSE(builder_->modifyRequest(params, req2));
  EXPECT_EQ(0, req2.attributes_size());
}

TEST_F(MappedAttributeBuilderTest, ModifiedOnceForOutbound) {
  initialize(R"EOF(
  mapped_response_attributes:
    "key": "response.code"
  )EOF");

  EXPECT_CALL(stream_info_, responseCode()).WillRepeatedly(Return(200));

  Http::TestResponseHeaderMapImpl response_headers;
  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::OUTBOUND,
      &callbacks_,
      nullptr,
      &response_headers,
      nullptr,
  };

  envoy::service::ext_proc::v3::ProcessingRequest req;
  EXPECT_TRUE(builder_->modifyRequest(params, req));
  const auto& attributes = req.attributes().at("envoy.filters.http.ext_proc");
  EXPECT_EQ(1, attributes.fields_size());
  EXPECT_TRUE(attributes.fields().contains("key"));
  EXPECT_EQ(200, attributes.fields().at("key").number_value());

  // Second call should do nothing and return false
  envoy::service::ext_proc::v3::ProcessingRequest req2;
  EXPECT_FALSE(builder_->modifyRequest(params, req2));
  EXPECT_EQ(0, req2.attributes_size());
}

TEST_F(MappedAttributeBuilderTest, CelEvalFailure) {
  initialize(R"EOF(
  mapped_request_attributes:
    "key": "nonexistent_attribute"
  )EOF");

  Http::TestRequestHeaderMapImpl request_headers;
  ProcessingRequestModifier::Params params{
      envoy::config::core::v3::TrafficDirection::INBOUND,
      &callbacks_,
      &request_headers,
      nullptr,
      nullptr,
  };
  envoy::service::ext_proc::v3::ProcessingRequest req;
  // Should still return true because modification was attempted.
  EXPECT_TRUE(builder_->modifyRequest(params, req));

  const auto& attributes = req.attributes().at("envoy.filters.http.ext_proc");
  EXPECT_EQ(0, attributes.fields_size());
}

#endif

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
