#include <fstream>
#include <sstream>
#include <string>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/extensions/filters/http/proto_api_scrubber/scrubber_test.pb.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "parser/parser.h"
#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

using FilterType = Envoy::Extensions::HttpFilters::ProtoApiScrubber::ProtoApiScrubberFilter;
using ConfigType = Envoy::Extensions::HttpFilters::ProtoApiScrubber::ProtoApiScrubberFilterConfig;
using ProtoConfig =
    envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;

namespace TestProto = test::extensions::filters::http::proto_api_scrubber;

namespace {

constexpr absl::string_view kScrubberTestDescriptorPath =
    "test/extensions/filters/http/proto_api_scrubber/scrubber_test.descriptor";

// Manually resolve the file path using Bazel's TEST_SRCDIR environment variable.
// This avoids the dependency on Envoy::TestEnvironment which causes crashes in benchmarks.
std::string readDescriptorContent() {
  std::string path;
  // Bazel sets this environment variable pointing to the runfiles root.
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    // Construct path: <runfiles_root>/envoy/<workspace_relative_path>
    path = std::string(srcdir) + "/envoy/" + std::string(kScrubberTestDescriptorPath);
  } else {
    // Fallback for manual local execution if needed.
    path = std::string(kScrubberTestDescriptorPath);
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.good()) {
    std::cerr << "Benchmark Error: Could not open descriptor file at: " << path << std::endl;
    // Fail hard if data dependency is missing.
    std::abort();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

class FilterBenchmarkFixture {
public:
  FilterBenchmarkFixture(int num_entries, bool add_rules) {
    std::string descriptor_bytes = readDescriptorContent();

    ProtoConfig config;
    config.mutable_descriptor_set()->mutable_data_source()->set_inline_bytes(descriptor_bytes);
    config.set_filtering_mode(ProtoConfig::OVERRIDE);

    if (add_rules) {
      auto* method_config = config.mutable_restrictions()->mutable_method_restrictions();
      auto& method_rules = (*method_config)
          ["/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"];

      for (int i = 0; i < 2; ++i) {
        // Configure rules for both Request (i=0) and Response (i=1).
        bool is_response = (i == 1);

        // Rules targeting Map values (Primitive and Message types).
        addRule(method_rules, "tags.value", is_response);
        addRule(method_rules, "deep_map.value.secret", is_response);
        addRule(method_rules, "int_map.value", is_response);
        addRule(method_rules, "object_map.value.secret", is_response);
        addRule(method_rules, "full_scrub_map.value", is_response);

        // Rule targeting a 2-level deep nested field inside a map value.
        addRule(method_rules, "deep_map.value.internal_details.deep_secret", is_response);

        // Rules targeting repeated fields and `oneof` fields.
        addRule(method_rules, "repeated_secrets", is_response);
        addRule(method_rules, "repeated_messages.secret", is_response);
        addRule(method_rules, "choice_a_string", is_response);
        addRule(method_rules, "choice_b_int", is_response);
      }
    }

    auto config_or_error = ConfigType::create(config, context_);
    RELEASE_ASSERT(config_or_error.ok(), std::string(config_or_error.status().message()));
    config_ = config_or_error.value();
    payload_ = generatePayload(num_entries);

    using testing::Return;
    ON_CALL(callbacks_, decoderBufferLimit()).WillByDefault(Return(100 * 1024 * 1024));
    ON_CALL(encoder_callbacks_, encoderBufferLimit()).WillByDefault(Return(100 * 1024 * 1024));
  }

  void addRule(envoy::extensions::filters::http::proto_api_scrubber::v3::MethodRestrictions& config,
               const std::string& field_path, bool is_response) {
    auto* rules = is_response ? config.mutable_response_field_restrictions()
                              : config.mutable_request_field_restrictions();

    xds::type::matcher::v3::Matcher matcher;
    auto* entry = matcher.mutable_matcher_list()->add_matchers();

    // Configure CEL Matcher (Always True).
    auto* single = entry->mutable_predicate()->mutable_single_predicate();
    single->mutable_input()->set_name("envoy.matching.inputs.cel_data_input");
    xds::type::matcher::v3::HttpAttributesCelMatchInput input_config;
    single->mutable_input()->mutable_typed_config()->PackFrom(input_config);

    auto* custom_match = single->mutable_custom_match();
    custom_match->set_name("envoy.matching.matchers.cel_matcher");
    xds::type::matcher::v3::CelMatcher cel_matcher;
    auto parse_status = google::api::expr::parser::Parse("true");
    RELEASE_ASSERT(parse_status.ok(), "Failed to parse CEL expression");
    *cel_matcher.mutable_expr_match()->mutable_cel_expr_parsed() = *parse_status;
    custom_match->mutable_typed_config()->PackFrom(cel_matcher);

    // Configure Action (RemoveField).
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
    entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(remove_action);
    entry->mutable_on_match()->mutable_action()->set_name("remove_field");

    *(*rules)[field_path].mutable_matcher() = matcher;
  }

  Buffer::OwnedImpl generatePayload(int count) {
    TestProto::ScrubRequest request;

    for (int i = 0; i < count; ++i) {
      std::string key = absl::StrCat("k_", i);

      // Populate Primitive Maps.
      (*request.mutable_tags())[key] = "sensitive_value";
      (*request.mutable_int_map())[key] = i * 100;
      (*request.mutable_safe_int_map())[key] = i;

      // Populate Nested Message Map (deep_map).
      auto& partial_val = (*request.mutable_deep_map())[key];
      // "super_secret" is the sensitive value we verify is scrubbed.
      partial_val.set_secret("super_secret");
      partial_val.set_public_field("public_data");
      partial_val.set_other_info("misc");

      // Populate 2-level deep nested field.
      partial_val.mutable_internal_details()->set_deep_secret("core");

      // Populate Object Map.
      auto& obj_val = (*request.mutable_object_map())[key];
      obj_val.set_secret("val");
      obj_val.set_public_field("val");

      // Populate Full Scrub Map.
      auto& full_val = (*request.mutable_full_scrub_map())[key];
      full_val.set_secret("val");

      // Populate Repeated Fields.
      request.add_repeated_secrets("sensitive_list_item");
      auto* repeated_msg = request.add_repeated_messages();
      repeated_msg->set_secret("hush");
      repeated_msg->set_public_field("loud");
    }

    // Populate OneOf field.
    if (count % 2 == 0) {
      request.set_choice_a_string("selected_string");
    } else {
      request.set_choice_b_int(999);
    }
    return *Grpc::Common::serializeToGrpcFrame(request);
  }

  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<const ConfigType> config_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Buffer::OwnedImpl payload_;
};

// Validates correct filter behavior (scrubbing vs passthrough) before running the benchmark loop.
// Returns true if valid, false (and sets state error) if validation fails.
bool verifyFilterBehavior(benchmark::State& state, FilterBenchmarkFixture& fixture,
                          bool is_response, bool expect_scrubbing) {
  auto filter = std::make_unique<FilterType>(*fixture.config_);
  filter->setDecoderFilterCallbacks(fixture.callbacks_);
  filter->setEncoderFilterCallbacks(fixture.encoder_callbacks_);

  Http::TestRequestHeaderMapImpl req_headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                               {"content-type", "application/grpc"}};

  // Headers Phase.
  if (filter->decodeHeaders(req_headers, false) != Http::FilterHeadersStatus::Continue) {
    state.SkipWithError("Setup: decodeHeaders failed");
    return false;
  }
  if (is_response) {
    if (filter->encodeHeaders(resp_headers, false) != Http::FilterHeadersStatus::Continue) {
      state.SkipWithError("Setup: encodeHeaders failed");
      return false;
    }
  }

  // Data Phase.
  Buffer::OwnedImpl temp_data;
  temp_data.add(fixture.payload_.toString());

  Http::FilterDataStatus status;
  if (is_response) {
    status = filter->encodeData(temp_data, true);
  } else {
    status = filter->decodeData(temp_data, true);
  }

  // Verify Status Code.
  if (status != Http::FilterDataStatus::Continue) {
    state.SkipWithError("Invalid Status: Expected Continue");
    return false;
  }

  // Verify Scrubbing Correctness.
  // "super_secret" is the value in `deep_map` which should ALWAYS be scrubbed if rules are active.
  std::string output = temp_data.toString();
  bool secret_found = (output.find("super_secret") != std::string::npos);
  if (expect_scrubbing && secret_found) {
    state.SkipWithError("Correctness Fail: Scrubbing enabled but secret found in output!");
    return false;
  }
  if (!expect_scrubbing && !secret_found) {
    state.SkipWithError("Correctness Fail: Passthrough enabled but secret missing from output!");
    return false;
  }

  return true;
}

// Measures the latency of processing a unary request when no fields match the scrubbing rules.
// This establishes the baseline overhead of the filter's traversal logic.
static void BM_Request_Unary_Passthrough(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), false);
  // Perform validation to ensure setup is correct.
  if (!verifyFilterBehavior(state, fixture, false, false))
    return;

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  for (auto _ : state) {
    auto filter = std::make_unique<FilterType>(*fixture.config_);
    filter->setDecoderFilterCallbacks(fixture.callbacks_);
    filter->decodeHeaders(headers, false);
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->decodeData(iteration_data, true);
    benchmark::DoNotOptimize(status);
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Request_Unary_Passthrough)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Measures the latency of processing a unary request when fields (including Maps) are actively
// scrubbed. This measures the cost of path normalization, match evaluation, and field removal.
static void BM_Request_Unary_Scrubbing(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), true);
  if (!verifyFilterBehavior(state, fixture, false, true))
    return;

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  for (auto _ : state) {
    auto filter = std::make_unique<FilterType>(*fixture.config_);
    filter->setDecoderFilterCallbacks(fixture.callbacks_);
    filter->decodeHeaders(headers, false);
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->decodeData(iteration_data, true);
    benchmark::DoNotOptimize(status);
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Request_Unary_Scrubbing)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Measures the latency of processing a streaming request with active scrubbing.
// In streaming mode, data frames may be processed incrementally.
static void BM_Request_Streaming_Scrubbing(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), true);
  if (!verifyFilterBehavior(state, fixture, false, true))
    return;

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  auto filter = std::make_unique<FilterType>(*fixture.config_);
  filter->setDecoderFilterCallbacks(fixture.callbacks_);
  filter->decodeHeaders(headers, false);

  for (auto _ : state) {
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->decodeData(iteration_data, false);
    benchmark::DoNotOptimize(status);
  }
  Buffer::OwnedImpl empty;
  filter->decodeData(empty, true);
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Request_Streaming_Scrubbing)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Measures the latency of processing a unary response when no fields match the scrubbing rules.
static void BM_Response_Unary_Passthrough(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), false);
  if (!verifyFilterBehavior(state, fixture, true, false))
    return;

  Http::TestRequestHeaderMapImpl req_headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                               {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  for (auto _ : state) {
    auto filter = std::make_unique<FilterType>(*fixture.config_);
    filter->setDecoderFilterCallbacks(fixture.callbacks_);
    filter->setEncoderFilterCallbacks(fixture.encoder_callbacks_);
    filter->decodeHeaders(req_headers, true);
    filter->encodeHeaders(resp_headers, false);
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->encodeData(iteration_data, true);
    benchmark::DoNotOptimize(status);
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Response_Unary_Passthrough)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Measures the latency of processing a unary response with active scrubbing enabled.
static void BM_Response_Unary_Scrubbing(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), true);
  if (!verifyFilterBehavior(state, fixture, true, true))
    return;

  Http::TestRequestHeaderMapImpl req_headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                               {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  for (auto _ : state) {
    auto filter = std::make_unique<FilterType>(*fixture.config_);
    filter->setDecoderFilterCallbacks(fixture.callbacks_);
    filter->setEncoderFilterCallbacks(fixture.encoder_callbacks_);
    filter->decodeHeaders(req_headers, true);
    filter->encodeHeaders(resp_headers, false);
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->encodeData(iteration_data, true);
    benchmark::DoNotOptimize(status);
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Response_Unary_Scrubbing)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Measures the latency of processing a streaming response with active scrubbing enabled.
static void BM_Response_Streaming_Scrubbing(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), true);
  if (!verifyFilterBehavior(state, fixture, true, true))
    return;

  Http::TestRequestHeaderMapImpl req_headers{
      {":path", "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub"},
      {"content-type", "application/grpc"}};
  Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                               {"content-type", "application/grpc"}};
  std::string raw_payload = fixture.payload_.toString();

  auto filter = std::make_unique<FilterType>(*fixture.config_);
  filter->setDecoderFilterCallbacks(fixture.callbacks_);
  filter->setEncoderFilterCallbacks(fixture.encoder_callbacks_);
  filter->decodeHeaders(req_headers, true);
  filter->encodeHeaders(resp_headers, false);

  for (auto _ : state) {
    Buffer::OwnedImpl iteration_data;
    iteration_data.add(raw_payload);
    auto status = filter->encodeData(iteration_data, false);
    benchmark::DoNotOptimize(status);
  }
  Buffer::OwnedImpl empty;
  filter->encodeData(empty, true);
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Response_Streaming_Scrubbing)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

// Raw Protobuf Round-Trip (Control Group).
// Measures the theoretical minimum cost of Parsing and Serializing the payload
// using the raw Google Protobuf library, bypassing all Envoy filter logic.
static void BM_Raw_Proto_RoundTrip(benchmark::State& state) {
  FilterBenchmarkFixture fixture(state.range(0), false);

  // Get the full gRPC frame
  std::string grpc_payload = fixture.payload_.toString();

  // Strip the 5-byte gRPC header (1 byte flag + 4 bytes length)
  // to get the actual serialized proto bytes.
  if (grpc_payload.size() <= 5) {
    state.SkipWithError("Payload too small for gRPC header");
    return;
  }
  std::string raw_proto_bytes = grpc_payload.substr(5);

  for (auto _ : state) {
    TestProto::ScrubRequest temp_msg;

    // Parse (Deserialization).
    bool parse_ok = temp_msg.ParseFromString(raw_proto_bytes);
    if (!parse_ok) {
      state.SkipWithError("Raw Parse Failed");
      break;
    }
    benchmark::DoNotOptimize(parse_ok);

    // Serialize.
    std::string out;
    bool ser_ok = temp_msg.SerializeToString(&out);
    benchmark::DoNotOptimize(ser_ok);
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Raw_Proto_RoundTrip)->RangeMultiplier(10)->Range(10, 10000)->Complexity();

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
