#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/add_body_filter.pb.h"
#include "test/integration/filters/add_body_filter.pb.validate.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class AddBodyFilterConfig {
public:
  AddBodyFilterConfig(
      test::integration::filters::AddBodyFilterConfig::FilterCallback where_to_add_body,
      uint32_t body_size,
      test::integration::filters::AddBodyFilterConfig::FilterCallback where_to_stop_and_buffer)
      : where_to_add_body_(where_to_add_body), body_size_(body_size),
        where_to_stop_and_buffer_(where_to_stop_and_buffer) {}

  const test::integration::filters::AddBodyFilterConfig::FilterCallback where_to_add_body_;
  const uint32_t body_size_;
  const test::integration::filters::AddBodyFilterConfig::FilterCallback where_to_stop_and_buffer_;
};

// A test filter that adds body data to a request/response without body payload.
class AddBodyStreamFilter : public Http::PassThroughFilter {
public:
  AddBodyStreamFilter(std::shared_ptr<AddBodyFilterConfig> config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    if (config_->where_to_add_body_ == test::integration::filters::AddBodyFilterConfig::DEFAULT) {
      if (end_stream) {
        Buffer::OwnedImpl body("body");
        headers.setContentLength(body.length());
        decoder_callbacks_->addDecodedData(body, false);
      } else {
        headers.removeContentLength();
      }
    } else if (config_->where_to_add_body_ ==
               test::integration::filters::AddBodyFilterConfig::DECODE_HEADERS) {
      Buffer::OwnedImpl body(std::string(config_->body_size_, 'a'));
      headers.setContentLength(body.length());
      decoder_callbacks_->addDecodedData(body, false);
    }

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    // decodeData is called for HTTP/3 where the FIN arrives separately from headers.
    if (config_->where_to_add_body_ == test::integration::filters::AddBodyFilterConfig::DEFAULT) {
      if (end_stream && data.length() == 0u) {
        data.add("body");
      }
    } else if (config_->where_to_add_body_ ==
               test::integration::filters::AddBodyFilterConfig::DECODE_DATA) {
      data.add(std::string(config_->body_size_, 'a'));
    }

    return config_->where_to_stop_and_buffer_ ==
                   test::integration::filters::AddBodyFilterConfig::DECODE_DATA
               ? Http::FilterDataStatus::StopIterationAndBuffer
               : Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    if (config_->where_to_add_body_ ==
        test::integration::filters::AddBodyFilterConfig::DECODE_TRAILERS) {
      Buffer::OwnedImpl body(std::string(config_->body_size_, 'a'));
      decoder_callbacks_->addDecodedData(body, false);
    }
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    // encodeData is called for HTTP/3 where the FIN arrives separately from headers.
    if (config_->where_to_add_body_ == test::integration::filters::AddBodyFilterConfig::DEFAULT) {
      if (end_stream && data.length() == 0) {
        data.add("body");
      }
    } else if (config_->where_to_add_body_ ==
               test::integration::filters::AddBodyFilterConfig::ENCODE_DATA) {
      data.add(std::string(config_->body_size_, 'a'));
    }

    return config_->where_to_stop_and_buffer_ ==
                   test::integration::filters::AddBodyFilterConfig::ENCODE_DATA
               ? Http::FilterDataStatus::StopIterationAndBuffer
               : Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    if (config_->where_to_add_body_ == test::integration::filters::AddBodyFilterConfig::DEFAULT) {
      if (end_stream) {
        Buffer::OwnedImpl body("body");
        headers.setContentLength(body.length());
        encoder_callbacks_->addEncodedData(body, false);
      }
    } else if (config_->where_to_add_body_ ==
               test::integration::filters::AddBodyFilterConfig::ENCODE_HEADERS) {
      Buffer::OwnedImpl body(std::string(config_->body_size_, 'a'));
      headers.setContentLength(body.length());
      encoder_callbacks_->addEncodedData(body, false);
    }

    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::shared_ptr<AddBodyFilterConfig> config_;
};

class AddBodyFilterFactory : public Extensions::HttpFilters::Common::DualFactoryBase<
                                 test::integration::filters::AddBodyFilterConfig> {
public:
  AddBodyFilterFactory() : DualFactoryBase("add-body-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::AddBodyFilterConfig& proto_config, const std::string&,
      DualInfo, Server::Configuration::ServerFactoryContext&) override {
    auto filter_config = std::make_shared<AddBodyFilterConfig>(
        proto_config.where_to_add_body(), proto_config.body_size(),
        proto_config.where_to_stop_and_buffer());
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<AddBodyStreamFilter>(filter_config));
    };
  }
};

using UpstreamAddBodyFilterFactory = AddBodyFilterFactory;

REGISTER_FACTORY(AddBodyFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAddBodyFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);
} // namespace Envoy
