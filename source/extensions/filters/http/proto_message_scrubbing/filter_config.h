#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/proto_message_scrubbing/extractor.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {

// The config for Proto Message Scrubbing filter. As a thread-safe class, it
// should be constructed only once and shared among filters for better
// performance.
class FilterConfig : public Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  explicit FilterConfig(const envoy::extensions::filters::http::proto_message_scrubbing::v3::
                            ProtoMessageScrubbingConfig& proto_config,
                        std::unique_ptr<ExtractorFactory> extractor_factory, Api::Api& api);

  const Extractor* findExtractor(absl::string_view proto_path) const;

private:
  void initDescriptorPool(Api::Api& api);

  void initExtractors(ExtractorFactory& extractor_factory);

  const envoy::extensions::filters::http::proto_message_scrubbing::v3::ProtoMessageScrubbingConfig&
      proto_config_;

  absl::flat_hash_map<std::string, std::unique_ptr<const Extractor>> proto_path_to_extractor_;

  std::unique_ptr<const Envoy::Protobuf::DescriptorPool> descriptor_pool_;
  std::unique_ptr<const google::grpc::transcoding::TypeHelper> type_helper_;
  std::unique_ptr<const TypeFinder> type_finder_;
};

using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
