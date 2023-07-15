#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

class FilterConfig : public Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  explicit FilterConfig(
      const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
          proto_config, std::unique_ptr<ExtractorFactory> extractor_factory, Api::Api& api);


  const Extractor* FindExtractor(absl::string_view proto_path)const ;

private:

  void initDescriptorPool(Api::Api& );
  const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig
      proto_config_;
  absl::flat_hash_map<std::string, std::unique_ptr<Extractor>> proto_path_to_extractor_;
  google::protobuf::DescriptorPool descriptor_pool_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
