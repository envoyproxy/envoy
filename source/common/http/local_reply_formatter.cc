#include "common/http/local_reply_formatter.h"

#include "envoy/http/codes.h"
#include "envoy/http/local_reply.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/access_log/access_log.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"
#include "common/http/headers.h"
#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"

namespace Envoy{
namespace Http{
namespace {


AccessLog::FilterPtr fromProto(const envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher& config,
                         Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                         ProtobufMessage::ValidationVisitor& validation_visitor) {
  switch (config.matcher_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher::kAndFilter:
    return AccessLog::FilterPtr{new Http::AndFilter(config.and_filter(), runtime, random, validation_visitor)};
  case envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher::kOrFilter:
    return AccessLog::FilterPtr{new Http::OrFilter(config.or_filter(), runtime, random, validation_visitor)};
  case envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher::kResponseFlagFilter:
    MessageUtil::validate(config, validation_visitor);
    return AccessLog::FilterPtr{new AccessLog::ResponseFlagFilter(config.response_flag_filter())};
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::list<std::pair<AccessLog::FilterPtr, Http::ResponseRewriter>> createMatcherRewriter(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& config,
    Server::Configuration::FactoryContext& context){

  if(config.has_local_reply_config() && !config.local_reply_config().mapper().empty()){
    std::list<std::pair<AccessLog::FilterPtr, Http::ResponseRewriter>> list_of_pairs;
    for(auto& config_pair: config.local_reply_config().mapper()){
      if (config_pair.has_matcher() && config_pair.has_rewriter()){

        std::pair<AccessLog::FilterPtr, Http::ResponseRewriter> pair = std::make_pair(
          fromProto(config_pair.matcher(), context.runtime(), context.random(), context.messageValidationVisitor()),
          Http::ResponseRewriter{PROTOBUF_GET_WRAPPED_OR_DEFAULT(config_pair.rewriter(), status_code, absl::optional<uint32_t>())}
        );

        list_of_pairs.emplace_back(std::move(pair));
      }
    }
    return list_of_pairs;
  }

  return std::list<std::pair<AccessLog::FilterPtr, Http::ResponseRewriter>>{};

}
}

    //Here we need to pass list of filters and rewrite
JsonFormatterImpl::JsonFormatterImpl(std::unordered_map<std::string, std::string> formatter,
                                     const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& config,
                                     Server::Configuration::FactoryContext& context)
                                     {
    formatter_ = std::make_unique<AccessLog::JsonFormatterImpl>(formatter);
    match_and_rewrite_ = createMatcherRewriter(config, context);

}

const std::string JsonFormatterImpl::format(const Http::HeaderMap* request_headers,
                                      const Http::HeaderMap* response_headers,
                                      const Http::HeaderMap* response_trailers,
                                      const absl::string_view& body,
                                      const StreamInfo::StreamInfo& stream_info) const {
  
  return formatter_->format(*request_headers, *response_headers, *response_trailers, stream_info, body);
}


void JsonFormatterImpl::rewriteMatchedResponse(const Http::HeaderMap* request_headers,
                                      const Http::HeaderMap* response_headers,
                                      const Http::HeaderMap* response_trailers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      Code& response_code) const{
  for(auto& pair: match_and_rewrite_){
    if(pair.first->evaluate(stream_info, *request_headers, *response_headers, *response_trailers)){
        response_code = static_cast<Http::Code>(pair.second.response_code_.value());
        break;
    }
  }
}

void JsonFormatterImpl::insertContentHeaders(const absl::string_view& body, 
                                      Http::HeaderMap* headers) const {
    headers->insertContentLength().value(body.size());
    headers->insertContentType().value(Headers::get().ContentTypeValues.Json);
}


Http::OperatorFilter::OperatorFilter(const Protobuf::RepeatedPtrField<envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher>& configs,
                               Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  for (const auto& config : configs) {
    filters_.emplace_back(fromProto(config, runtime, random, validation_visitor));
  }
}

Http::OrFilter::OrFilter(const envoy::config::filter::network::http_connection_manager::v2::OrFilter& config,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                   ProtobufMessage::ValidationVisitor& validation_visitor)
    : OperatorFilter(config.filters(), runtime, random, validation_visitor) {}

Http::AndFilter::AndFilter(const envoy::config::filter::network::http_connection_manager::v2::AndFilter& config,
                     Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                     ProtobufMessage::ValidationVisitor& validation_visitor)
    : OperatorFilter(config.filters(), runtime, random, validation_visitor) {}

bool Http::OrFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                        const Http::HeaderMap& response_headers,
                        const Http::HeaderMap& response_trailers) {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (result) {
      break;
    }
  }

  return result;
}

bool Http::AndFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                         const Http::HeaderMap& response_headers,
                         const Http::HeaderMap& response_trailers) {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (!result) {
      break;
    }
  }

  return result;
}

}
}

