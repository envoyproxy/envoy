#pragma once

#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_evaluator.h"
#include "envoy/http/header_map.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Router {

class HeaderParser;
using HeaderParserPtr = std::unique_ptr<HeaderParser>;

using HeaderAppendAction = envoy::config::core::v3::HeaderValueOption::HeaderAppendAction;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;
using HeaderValue = envoy::config::core::v3::HeaderValue;

struct HeadersToAddEntry {
  HeadersToAddEntry(const HeaderValue& header_value, HeaderAppendAction append_action);
  HeadersToAddEntry(const HeaderValueOption& header_value_option);

  std::string original_value_;
  bool add_if_empty_ = false;

  Formatter::FormatterPtr formatter_;
  HeaderAppendAction append_action_;
};

/**
 * HeaderParser manipulates Http::HeaderMap instances. Headers to be added are pre-parsed to select
 * between a constant value implementation and a dynamic value implementation based on
 * StreamInfo::StreamInfo fields.
 */
class HeaderParser : public Http::HeaderEvaluator {
public:
  /*
   * @param headers_to_add defines the headers to add during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr
  configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add);

  /*
   * @param headers_to_add defines headers to add during calls to evaluateHeaders.
   * @param append_action defines action taken to append/overwrite the given value for an existing
   * header or to only add this header if it's absent.
   * @return HeaderParserPtr a configured HeaderParserPtr.
   */
  static HeaderParserPtr
  configure(const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers_to_add,
            HeaderAppendAction append_action);

  /*
   * @param headers_to_add defines headers to add during calls to evaluateHeaders
   * @param headers_to_remove defines headers to remove during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr
  configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add,
            const Protobuf::RepeatedPtrField<std::string>& headers_to_remove);

  static const HeaderParser& defaultParser() {
    static HeaderParser* instance = new HeaderParser();
    return *instance;
  }

  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::HttpFormatterContext& context,
                       const StreamInfo::StreamInfo& stream_info) const override;

  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::HttpFormatterContext& context,
                       const StreamInfo::StreamInfo* stream_info) const;

  /**
   * Helper methods to evaluate methods without explicitly passing request and response headers.
   * The method will try to fetch request headers from steam_info. Response headers will always be
   * empty.
   */
  void evaluateHeaders(Http::HeaderMap& headers, const StreamInfo::StreamInfo& stream_info) const {
    evaluateHeaders(headers, {stream_info.getRequestHeaders()}, &stream_info);
  }
  void evaluateHeaders(Http::HeaderMap& headers, const StreamInfo::StreamInfo* stream_info) const {
    evaluateHeaders(headers,
                    Formatter::HttpFormatterContext{
                        stream_info == nullptr ? nullptr : stream_info->getRequestHeaders()},
                    stream_info);
  }

  /*
   * Same as evaluateHeaders, but returns the modifications that would have been made rather than
   * modifying an existing HeaderMap.
   * @param stream_info contains additional information about the request.
   * @param do_formatting whether or not to evaluate configured transformations; if false, returns
   * original values instead.
   */
  Http::HeaderTransforms getHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                             bool do_formatting = true) const;

  static std::string translateMetadataFormat(const std::string& header_value);
  static std::string translatePerRequestState(const std::string& header_value);

protected:
  HeaderParser() = default;

private:
  std::vector<std::pair<Http::LowerCaseString, HeadersToAddEntry>> headers_to_add_;
  std::vector<Http::LowerCaseString> headers_to_remove_;
};

} // namespace Router
} // namespace Envoy
