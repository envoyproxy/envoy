#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

using ProtoRule = envoy::extensions::filters::http::header_to_metadata::v3::Config::Rule;
using ValueType = envoy::extensions::filters::http::header_to_metadata::v3::Config::ValueType;
using ValueEncode = envoy::extensions::filters::http::header_to_metadata::v3::Config::ValueEncode;

class Rule {
public:
  Rule(const std::string& header, const ProtoRule& rule);
  const ProtoRule& rule() const { return rule_; }
  const Regex::CompiledMatcherPtr& regexRewrite() const { return regex_rewrite_; }
  const std::string& regexSubstitution() const { return regex_rewrite_substitution_; }
  const Http::LowerCaseString& header() const { return header_; }

private:
  const Http::LowerCaseString header_;
  const ProtoRule rule_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
};

using HeaderToMetadataRules = std::vector<Rule>;

// TODO(yangminzhu): Make MAX_HEADER_VALUE_LEN configurable.
const uint32_t MAX_HEADER_VALUE_LEN = 8 * 1024;

/**
 *  Encapsulates the filter configuration with STL containers and provides an area for any custom
 *  configuration logic.
 */
class Config : public ::Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  Config(const envoy::extensions::filters::http::header_to_metadata::v3::Config config,
         bool per_route = false);

  const HeaderToMetadataRules& requestRules() const { return request_rules_; }
  const HeaderToMetadataRules& responseRules() const { return response_rules_; }
  bool doResponse() const { return response_set_; }
  bool doRequest() const { return request_set_; }

private:
  using ProtobufRepeatedRule = Protobuf::RepeatedPtrField<ProtoRule>;

  /**
   *  configToVector is a helper function for converting from configuration (protobuf types) into
   *  STL containers for usage elsewhere.
   *
   *  @param config A protobuf repeated field of metadata that specifies what headers to convert to
   *         metadata
   *  @param vector A vector that will be populated with the configuration data from config
   *  @return true if any configuration data was added to the vector, false otherwise. Can be used
   *          to validate whether the configuration was empty.
   */
  static bool configToVector(const ProtobufRepeatedRule&, HeaderToMetadataRules&);

  const std::string& decideNamespace(const std::string& nspace) const;

  HeaderToMetadataRules request_rules_;
  HeaderToMetadataRules response_rules_;
  bool response_set_;
  bool request_set_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Header-To-Metadata examines request/response headers and either copies or
 * moves the values into request metadata based on configuration information.
 */
class HeaderToMetadataFilter : public Http::StreamFilter,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  HeaderToMetadataFilter(const ConfigSharedPtr config);
  ~HeaderToMetadataFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  friend class HeaderToMetadataTest;

  using StructMap = std::map<std::string, ProtobufWkt::Struct>;

  const ConfigSharedPtr config_;
  mutable const Config* effective_config_{nullptr};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  /**
   *  writeHeaderToMetadata encapsulates (1) searching for the header and (2) writing it to the
   *  request metadata.
   *  @param headers the map of key-value headers to look through. These could be response or
   *                 request headers depending on whether this is called from the encode state or
   *                 decode state.
   *  @param rules the header-to-metadata mapping set in configuration.
   *  @param callbacks the callback used to fetch the StreamInfo (which is then used to get
   *                   metadata). Callable with both encoder_callbacks_ and decoder_callbacks_.
   */
  void writeHeaderToMetadata(Http::HeaderMap& headers, const HeaderToMetadataRules& rules,
                             Http::StreamFilterCallbacks& callbacks);
  bool addMetadata(StructMap&, const std::string&, const std::string&, absl::string_view, ValueType,
                   ValueEncode) const;
  const std::string& decideNamespace(const std::string& nspace) const;
  const Config* getConfig() const;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
