#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"
#include "test/integration/filters/non_terminal_encoding_filter.pb.h"
#include "test/integration/filters/non_terminal_encoding_filter.pb.validate.h"
#include "test/integration/utility.h"

namespace Envoy {

class NonTerminalEncodingFilterConfig {
public:
  NonTerminalEncodingFilterConfig(
      test::integration::filters::NonTerminalEncodingFilterConfig::WhereToStartEncoding
          where_to_start_encoding,
      test::integration::filters::NonTerminalEncodingFilterConfig::HowToEncode encode_body,
      test::integration::filters::NonTerminalEncodingFilterConfig::HowToEncode encode_trailers)
      : where_to_start_encoding_(where_to_start_encoding), encode_body_(encode_body),
        encode_trailers_(encode_trailers) {}

  const test::integration::filters::NonTerminalEncodingFilterConfig::WhereToStartEncoding
      where_to_start_encoding_;
  const test::integration::filters::NonTerminalEncodingFilterConfig::HowToEncode encode_body_;
  const test::integration::filters::NonTerminalEncodingFilterConfig::HowToEncode encode_trailers_;
};

// A test filter that encodes a response in a decodeXXX handler.
class NonTerminalEncodingFilter : public Http::PassThroughFilter {
public:
  NonTerminalEncodingFilter(std::shared_ptr<NonTerminalEncodingFilterConfig> config)
      : config_(config) {}

  void startEncoding() {
    const bool end_stream =
        config_->encode_body_ ==
            test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING &&
        config_->encode_trailers_ ==
            test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING;
    Http::ResponseHeaderMapPtr response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
        Http::TestResponseHeaderMapImpl{{":status", "200"}});
    decoder_callbacks_->encodeHeaders(std::move(response_headers), end_stream, "");
    if (config_->encode_body_ !=
        test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING) {
      encodeBody();
    } else if (config_->encode_trailers_ !=
               test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING) {
      encodeResponseTrailers();
    }
  }

  void encodeBody() {
    auto encode_lambda = [this]() -> void {
      const bool end_stream =
          config_->encode_trailers_ ==
          test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING;
      Buffer::OwnedImpl buffer("encoded body");
      decoder_callbacks_->encodeData(buffer, end_stream);
      if (config_->encode_trailers_ !=
          test::integration::filters::NonTerminalEncodingFilterConfig::SKIP_ENCODING) {
        encodeResponseTrailers();
      }
    };
    if (config_->encode_body_ ==
        test::integration::filters::NonTerminalEncodingFilterConfig::SYNCHRONOUSLY) {
      encode_lambda();
    } else {
      decoder_callbacks_->dispatcher().post(encode_lambda);
    }
  }

  void encodeResponseTrailers() {
    auto encode_lambda = [this]() -> void {
      Http::ResponseTrailerMapPtr response_trailers =
          std::make_unique<Http::TestResponseTrailerMapImpl>(
              Http::TestResponseTrailerMapImpl{{"foo", "bar"}});
      decoder_callbacks_->encodeTrailers(std::move(response_trailers));
    };
    if (config_->encode_trailers_ ==
        test::integration::filters::NonTerminalEncodingFilterConfig::SYNCHRONOUSLY) {
      encode_lambda();
    } else {
      decoder_callbacks_->dispatcher().post(encode_lambda);
    }
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    if (config_->where_to_start_encoding_ ==
        test::integration::filters::NonTerminalEncodingFilterConfig::DECODE_HEADERS) {
      startEncoding();
    }

    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    if (config_->where_to_start_encoding_ ==
        test::integration::filters::NonTerminalEncodingFilterConfig::DECODE_DATA) {
      startEncoding();
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    if (config_->where_to_start_encoding_ ==
        test::integration::filters::NonTerminalEncodingFilterConfig::DECODE_TRAILERS) {
      startEncoding();
    }
    return Http::FilterTrailersStatus::StopIteration;
  }

private:
  const std::shared_ptr<NonTerminalEncodingFilterConfig> config_;
};

class NonTerminalEncodingFilterFactory
    : public Extensions::HttpFilters::Common::DualFactoryBase<
          test::integration::filters::NonTerminalEncodingFilterConfig> {
public:
  NonTerminalEncodingFilterFactory() : DualFactoryBase("non-terminal-encoding-filter") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const test::integration::filters::NonTerminalEncodingFilterConfig& proto_config,
      const std::string&, DualInfo, Server::Configuration::ServerFactoryContext&) override {
    auto filter_config = std::make_shared<NonTerminalEncodingFilterConfig>(
        proto_config.where_to_start_encoding(), proto_config.encode_body(),
        proto_config.encode_trailers());
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<NonTerminalEncodingFilter>(filter_config));
    };
  }
};

using UpstreamNonTerminalEncodingFilterFactory = NonTerminalEncodingFilterFactory;

REGISTER_FACTORY(NonTerminalEncodingFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamNonTerminalEncodingFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);
} // namespace Envoy
