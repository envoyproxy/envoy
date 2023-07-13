#include <functional>
#include <memory>

#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"

#include "test/proto/apikeys.pb.h"

#include "devtools/build/runtime/get_runfiles_dir.h"
#include "experimental/users/taoxuy/grpc_field_extraction/cloudesf_cpe/extractor_test_base.h"
#include "gmock/gmock.h"
#include "google/api/control.proto.h"
#include "gtest/gtest.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "third_party/protobuf/descriptor.h"
#include "third_party/protobuf/util/type_resolver_util.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::EqualsProto;
using ::testing::Field;
using ::testing::status::StatusIs;

using CpeCreateApiKeyRequest = ::google::example::apikeys::v1::CreateApiKeyRequest;

// Extracted resource values for test assertions.
constexpr absl::string_view kGoodResource = "projects/cloud-api-proxy-test-client";
constexpr absl::string_view kGoodResourceWithLocation =
    "projects/cloud-api-proxy-test-client/locations/us-central1";
constexpr absl::string_view kLocation = "us-central1";

// Additional resource policy for testing multiple polices.
constexpr absl::string_view kAddionalResourcePolicy = R"pb(
  selector: "key.display_name"
  resource_type: "RESOURCE"
)pb";
constexpr absl::string_view kAdditionalGoodResource = "my-api-key";

class ExtractImplTest : public ::testing::Test {
protected:
  void PrepareService(std::function<void(Service*)> service_config_mutator_fn) {
    proto2::FileDescriptorSet descriptor_set;
    auto status =
        file::GetBinaryProto(devtools_build::GetDataDependencyFilepath(testing::kCpeDescriptorPath),
                             &descriptor_set, file::Defaults());
    ASSERT_OK(status);
    for (const auto& file : descriptor_set.file()) {
      ASSERT_NE(descriptor_pool_.BuildFile(file), nullptr);
    }

    type_helper_ = std::make_unique<google::grpc::transcoding::TypeHelper>(
        proto2::util::NewTypeResolverForDescriptorPool("type.googleapis.com", &descriptor_pool_));
  }

  std::unique_ptr<ResourceExtractor> CreateExtractorImpl(absl::string_view selector) const {
    return std::make_unique<ResourceExtractorImpl>(*descriptor_pool_.FindMethodByName(selector),
                                                   "parent", type_helper_.get());
  }

  void ProcessRequest(ResourceExtractor& extractor, absl::Cord& message) {
    google::protobuf::field_extraction::CordMessageData message_data(message);
    EXPECT_OK(extractor.ProcessRequest(message_data));
  }

  proto2::DescriptorPool descriptor_pool_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
  CpeCreateApiKeyRequest request_;
  absl::Cord raw_message_;
};

TEST_F(ExtractImplTest, ExtractCpeCreateApiKeyRequest) {
  PrepareService([](Service*) {});
  request_ = ParseTestProto(testing::kCpeRequestBody);
  request_.SerializeToCord(&raw_message_);

  auto extractor = CreateExtractorImpl(testing::kCpeSelector);
  proto2::io::CordInputStream cord_input_stream(&raw_message_);
  proto2::io::CodedInputStream coded_input_stream(&cord_input_stream);
  ProcessRequest(*extractor, raw_message_);
  // Test calling 2nd time, it should be no-op.
  ProcessRequest(*extractor, raw_message_);
  // EXPECT_THAT(extractor->GetResult(),
  //             Field(&ExtractionResult::resources,
  //                   ElementsAre(AllOf(Field(&ExtractedResource::names,
  //                                           ElementsAre(ResourceNameAndLocation{
  //                                               kGoodResource, ""}))))));
  CpeCreateApiKeyRequest out_msg;
  out_msg.ParseFromCord(raw_message_);
  EXPECT_THAT(out_msg, EqualsProto(request_));
}

} // namespace
} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
