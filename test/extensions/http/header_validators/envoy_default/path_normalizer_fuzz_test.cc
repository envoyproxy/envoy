#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "test/extensions/http/header_validators/envoy_default/path_normalizer_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/stream_info/mocks.h"

namespace Envoy {

// Fuzz the path normalizer.
DEFINE_PROTO_FUZZER(const test::extensions::http::header_validators::envoy_default::PathNormalizerFuzzTestCase& input) {
  auto header_map = Http::RequestHeaderMapImpl::create();
  Http::HeaderString method;
  Http::HeaderString path;
  method.setCopyUnvalidatedForTestOnly(input.method());
  path.setCopyUnvalidatedForTestOnly(input.path());
  
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  Extensions::Http::HeaderValidators::EnvoyDefault::Http1HeaderValidator validator(input.config(), Http::Protocol::Http11, stream_info);
  // The character set of the :path and :method headers is validated before normalization.
  // Here we will just not run the test with invalid values
  if (!validator.validateMethodHeader(method) || (!(method == "CONNECT" && path.empty()) && !validator.validatePathHeaderCharacters(path))) {
    return;
  }

  header_map->setMethod(method.getStringView());
  header_map->setPath(path.getStringView());

  Extensions::Http::HeaderValidators::EnvoyDefault::PathNormalizer normalizer(input.config());
  auto result = normalizer.normalizePathUri(*header_map);
  if (result.ok() || result.action() == Extensions::Http::HeaderValidators::EnvoyDefault::PathNormalizer::PathNormalizationResult::Action::Redirect) {
    // Additional sanity checks on the normalized path
    RELEASE_ASSERT(header_map->path().size() <= input.path().size(), "Normalized path is always shorter or the same length.");
    RELEASE_ASSERT(header_map->method() == input.method(), ":method should not change.");
    auto original_query_or_fragment = input.path().find_first_of("?#");
    auto normalized_query_or_fragment = header_map->path().find_first_of("?#");
    RELEASE_ASSERT((original_query_or_fragment != std::string::npos && normalized_query_or_fragment != absl::string_view::npos) ||
                   (original_query_or_fragment == std::string::npos && normalized_query_or_fragment == absl::string_view::npos), 
                   "Query/fragment must be present or absent in both original and normalized paths");
    if (original_query_or_fragment != std::string::npos) {
      RELEASE_ASSERT(input.path().substr(original_query_or_fragment) == header_map->path().substr(normalized_query_or_fragment), "Original and normalized query/path should be the same");
    }

    if (!input.config().uri_path_normalization_options().skip_merging_slashes()) {
      RELEASE_ASSERT(header_map->path().substr(0, normalized_query_or_fragment).find("//") == absl::string_view::npos, ":path must not contain adjacent slashes.");
    }
  }
}

} // namespace Envoy
