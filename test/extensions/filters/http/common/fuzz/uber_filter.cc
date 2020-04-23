#include "test/extensions/filters/http/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

UberFilterFuzzer::UberFilterFuzzer() {
  // Need to set for both a decoder filter and an encoder/decoder filter.
  ON_CALL(filter_callback_, addStreamDecoderFilter(_))
      .WillByDefault(Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
        filter_ = filter;
        filter_->setDecoderFilterCallbacks(callbacks_);
      }));
  ON_CALL(filter_callback_, addStreamFilter(_))
      .WillByDefault(Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
        filter_ = filter;
        filter_->setDecoderFilterCallbacks(callbacks_);
      }));
  // Set expectations for particular filters that may get fuzzed.
  perFilterSetup();
}

void UberFilterFuzzer::decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data) {
  bool end_stream = false;

  auto headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(data.headers());
  if (headers.Path() == nullptr) {
    headers.setPath("/foo");
  }
  if (headers.Method() == nullptr) {
    headers.setMethod("GET");
  }
  if (headers.Host() == nullptr) {
    headers.setHost("foo.com");
  }

  if (data.data().empty() && !data.has_trailers()) {
    end_stream = true;
  }
  ENVOY_LOG_MISC(debug, "Decoding headers: {} ", data.headers().DebugString());
  const auto& headersStatus = filter->decodeHeaders(headers, end_stream);
  if (headersStatus != Http::FilterHeadersStatus::Continue &&
      headersStatus != Http::FilterHeadersStatus::StopIteration) {
    return;
  }

  for (int i = 0; i < data.data().size(); i++) {
    if (i == data.data().size() - 1 && !data.has_trailers()) {
      end_stream = true;
    }
    Buffer::OwnedImpl buffer(data.data().Get(i));
    ENVOY_LOG_MISC(debug, "Decoding data: {} ", buffer.toString());
    if (filter->decodeData(buffer, end_stream) != Http::FilterDataStatus::Continue) {
      return;
    }
  }

  if (data.has_trailers()) {
    ENVOY_LOG_MISC(debug, "Decoding trailers: {} ", data.trailers().DebugString());
    auto trailers = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
    filter->decodeTrailers(trailers);
  }
}

void UberFilterFuzzer::fuzz(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
        proto_config,
    const test::fuzz::HttpData& data) {
  try {
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedHttpFilterConfigFactory>(proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    // Clean-up config with filter-specific logic.
    cleanFuzzedConfig(proto_config.name(), message.get());
    cb_ = factory.createFilterFactoryFromProto(*message, "stats", factory_context_);
    cb_(filter_callback_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
    return;
  }

  decode(filter_.get(), data);
  reset();
}

void UberFilterFuzzer::reset() {
  if (filter_ != nullptr) {
    filter_->onDestroy();
  }
  filter_.reset();
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
