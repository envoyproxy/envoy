#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"
#include "test/integration/filters/crash_filter.pb.h"
#include "test/integration/filters/crash_filter.pb.validate.h"

namespace Envoy {

class CrashFilterConfig {
public:
  CrashFilterConfig(bool crash_in_encode_headers, bool crash_in_encode_data,
                    bool crash_in_decode_headers, bool crash_in_decode_data,
                    bool crash_in_decode_trailers)
      : crash_in_encode_headers_(crash_in_encode_headers),
        crash_in_encode_data_(crash_in_encode_data),
        crash_in_decode_headers_(crash_in_decode_headers),
        crash_in_decode_data_(crash_in_decode_data),
        crash_in_decode_trailers_(crash_in_decode_trailers) {}

  const bool crash_in_encode_headers_;
  const bool crash_in_encode_data_;

  const bool crash_in_decode_headers_;
  const bool crash_in_decode_data_;
  const bool crash_in_decode_trailers_;
};

// A test filter that adds body data to a request/response without body payload.
class CrashFilter : public Http::PassThroughFilter {
public:
  CrashFilter(std::shared_ptr<CrashFilterConfig> config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    RELEASE_ASSERT(!config_->crash_in_decode_headers_, "Crash in decodeTrailers");
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    RELEASE_ASSERT(!config_->crash_in_decode_data_, "Crash in decodeData");
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    RELEASE_ASSERT(!config_->crash_in_decode_trailers_, "Crash in decodeTrailers");
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    RELEASE_ASSERT(!config_->crash_in_encode_data_, "Crash in encodeData");
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    RELEASE_ASSERT(!config_->crash_in_encode_headers_, "Crash in encodeHeaders");
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::shared_ptr<CrashFilterConfig> config_;
};

class CrashFilterFactory : public Extensions::HttpFilters::Common::DualFactoryBase<
                               test::integration::filters::CrashFilterConfig> {
public:
  CrashFilterFactory() : DualFactoryBase("crash-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::CrashFilterConfig& proto_config, const std::string&,
      DualInfo, Server::Configuration::ServerFactoryContext&) override {
    auto filter_config = std::make_shared<CrashFilterConfig>(
        proto_config.crash_in_encode_headers(), proto_config.crash_in_encode_data(),
        proto_config.crash_in_decode_headers(), proto_config.crash_in_decode_data(),
        proto_config.crash_in_decode_trailers());
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<CrashFilter>(filter_config));
    };
  }
};

using UpstreamCrashFilterFactory = CrashFilterFactory;

REGISTER_FACTORY(CrashFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamCrashFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);
} // namespace Envoy
