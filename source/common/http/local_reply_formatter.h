#pragma once

#include <unordered_map>

#include "envoy/http/codes.h"
#include "envoy/http/local_reply.h"
#include "envoy/access_log/access_log.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "common/protobuf/protobuf.h"

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"


namespace Envoy{
namespace Http{

struct ResponseRewriter {
  absl::optional<uint32_t> response_code_;
};


class JsonFormatterImpl : public Formatter {
public:
  JsonFormatterImpl(std::unordered_map<std::string, std::string> formatter,
                    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& config,
                    Server::Configuration::FactoryContext& context);

  // Formatter::format
const std::string format(const Http::HeaderMap* request_headers,
                     const Http::HeaderMap* response_headers,
                     const Http::HeaderMap* response_trailers,
                     const absl::string_view& body,
                     const StreamInfo::StreamInfo& stream_info) const override;

void insertContentHeaders(const absl::string_view& body,
                     Http::HeaderMap* headers) const override;

void rewriteMatchedResponse(const Http::HeaderMap* request_headers,
                     const Http::HeaderMap* response_headers,
                     const Http::HeaderMap* response_trailers,
                     const StreamInfo::StreamInfo& stream_info,
                     Code& response_code) const override;

private:
  AccessLog::FormatterPtr formatter_;
  std::list<std::pair<AccessLog::FilterPtr, Http::ResponseRewriter>> match_and_rewrite_;
};


/**
 * Base operator filter, compose other filters with operation
 */
class OperatorFilter : public AccessLog::Filter {
public:
  OperatorFilter(const Protobuf::RepeatedPtrField<
                     envoy::config::filter::network::http_connection_manager::v2::ResponseMatcher>& configs,
                 Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                 ProtobufMessage::ValidationVisitor& validation_visitor);

protected:
  std::vector<AccessLog::FilterPtr> filters_;
};

/**
 * *And* operator filter, apply logical *and* operation to all of the sub filters.
 */
class AndFilter : public OperatorFilter {
public:
  AndFilter(const envoy::config::filter::network::http_connection_manager::v2::AndFilter& config, Runtime::Loader& runtime,
            Runtime::RandomGenerator& random,
            ProtobufMessage::ValidationVisitor& validation_visitor);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                const Http::HeaderMap& response_headers,
                const Http::HeaderMap& response_trailers) override;
};

/**
 * *Or* operator filter, apply logical *or* operation to all of the sub filters.
 */
class OrFilter : public OperatorFilter {
public:
  OrFilter(const envoy::config::filter::network::http_connection_manager::v2::OrFilter& config, Runtime::Loader& runtime,
           Runtime::RandomGenerator& random,
           ProtobufMessage::ValidationVisitor& validation_visitor);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                const Http::HeaderMap& response_headers,
                const Http::HeaderMap& response_trailers) override;
};

}
}

