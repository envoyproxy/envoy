#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();

  // This creates the filter config and runs the fuzzed data against the filter.
  void fuzz(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                proto_config,
            const test::fuzz::HttpData& downstream_data, const test::fuzz::HttpData& upstream_data);

  // This executes the filter decoders/encoders with the fuzzed data.
  template <class FilterType> void runData(FilterType* filter, const test::fuzz::HttpData& data);

  // This executes the access logger with the fuzzed headers/trailers.
  void accessLog(AccessLog::Instance* access_logger, const StreamInfo::StreamInfo& stream_info);

  // For fuzzing proto data, guide the mutator to useful 'Any' types.
  static void guideAnyProtoType(test::fuzz::HttpData* mutable_data, uint choice);

  // Resets cached data (request headers, etc.). Should be called for each fuzz iteration.
  void reset();

protected:
  // Set-up filter specific mock expectations in constructor.
  void perFilterSetup();
  // Filter specific input cleanup.
  void cleanFuzzedConfig(absl::string_view filter_name, Protobuf::Message* message);

  // Parses http or proto body into chunks.
  static std::vector<std::string> parseHttpData(const test::fuzz::HttpData& data);

  // Templated functions to validate and send headers/data/trailers for decoders/encoders.
  // General functions are deleted, but templated specializations for encoders/decoders are defined
  // in the cc file.
  template <class FilterType>
  Http::FilterHeadersStatus sendHeaders(FilterType* filter, const test::fuzz::HttpData& data,
                                        bool end_stream) = delete;

  template <class FilterType>
  Http::FilterDataStatus sendData(FilterType* filter, Buffer::Instance& buffer,
                                  bool end_stream) = delete;

  template <class FilterType>
  void sendTrailers(FilterType* filter, const test::fuzz::HttpData& data) = delete;

private:
  // This keeps track of when a filter will stop decoding due to direct responses.
  bool enabled_ = true;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  Http::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

  // Mocked callbacks.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  // Filter constructed from the config.
  Http::StreamDecoderFilterSharedPtr decoder_filter_;
  Http::StreamEncoderFilterSharedPtr encoder_filter_;
  AccessLog::InstanceSharedPtr access_logger_;

  // Headers/trailers need to be saved for the lifetime of the the filter,
  // so save them as member variables.
  // TODO(nareddyt): Use for access logging in a followup PR.
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
