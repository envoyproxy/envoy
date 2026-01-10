#include "source/common/common/regex.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/regex_engines/v3/google_re2.pb.h"
#include "envoy/extensions/regex_engines/v3/google_re2.pb.validate.h"
#include "envoy/type/matcher/v3/regex.pb.h"
#include "envoy/type/matcher/v3/regex.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/runtime/runtime_features.h"

#include "re2/re2.h"

namespace Envoy {
namespace Regex {

namespace {

constexpr int64_t kRe2DefaultMaxMemBytes = 8ll << 20;   // 8MiB (RE2 default)
constexpr int64_t kRe2HardMaxMemBytes = 256ll << 20;    // hard safety cap for compilation
constexpr int64_t kBytesPerProgramInstEstimate = 1024;  // conservative bound; avoids tight coupling to RE2 internals

uint32_t runtimeMaxProgramSizeErrorLevelOrDefault() {
  if (Runtime::isRuntimeInitialized()) {
    return Runtime::getInteger("re2.max_program_size.error_level", 100);
  }
  return 100;
}

uint32_t runtimeMaxProgramSizeWarnLevelOrDefault() {
  if (Runtime::isRuntimeInitialized()) {
    return Runtime::getInteger("re2.max_program_size.warn_level", UINT32_MAX);
  }
  return UINT32_MAX;
}

int64_t clampCompilationMaxMemFromProgramSize(uint32_t max_program_size) {
  // If the caller has no meaningful ceiling, fall back to a safe hard cap.
  if (max_program_size == UINT32_MAX) {
    return kRe2HardMaxMemBytes;
  }

  // Bound compilation memory as a function of configured program-size ceilings.
  const int64_t scaled =
      static_cast<int64_t>(max_program_size) * kBytesPerProgramInstEstimate;

  // Ensure we never go below RE2’s default, and never exceed the hard safety cap.
  return std::min(kRe2HardMaxMemBytes, std::max(kRe2DefaultMaxMemBytes, scaled));
}

re2::RE2::Options makeQuietRe2OptionsWithMaxMem(int64_t max_mem_bytes) {
  re2::RE2::Options options;
  // Match "Quiet" behavior (no stderr logging) while allowing us to set max_mem.
  options.set_log_errors(false);
  options.set_max_mem(max_mem_bytes);
  return options;
}

} // namespace

absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
CompiledGoogleReMatcher::createAndSizeCheck(const std::string& regex) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<CompiledGoogleReMatcher>(
      new CompiledGoogleReMatcher(regex, creation_status, true));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
CompiledGoogleReMatcher::create(const envoy::type::matcher::v3::RegexMatcher& config) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<CompiledGoogleReMatcher>(
      new CompiledGoogleReMatcher(config, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
CompiledGoogleReMatcher::create(const xds::type::matcher::v3::RegexMatcher& config) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<CompiledGoogleReMatcher>(
      new CompiledGoogleReMatcher(config, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

REGISTER_FACTORY(GoogleReEngineFactory, EngineFactory);

CompiledGoogleReMatcher::CompiledGoogleReMatcher(const std::string& regex,
                                                 absl::Status& creation_status,
                                                 bool do_program_size_check)
    : regex_(regex,
             makeQuietRe2OptionsWithMaxMem(clampCompilationMaxMemFromProgramSize(
                 do_program_size_check ? runtimeMaxProgramSizeErrorLevelOrDefault() : UINT32_MAX))) {
  if (!regex_.ok()) {
    creation_status = absl::InvalidArgumentError(regex_.error());
    return;
  }

  // Preserve existing behavior, but use safe defaults when runtime isn’t initialized yet.
  if (do_program_size_check) {
    const uint32_t regex_program_size = static_cast<uint32_t>(regex_.ProgramSize());

    const uint32_t max_program_size_error_level = runtimeMaxProgramSizeErrorLevelOrDefault();
    if (regex_program_size > max_program_size_error_level) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("regex '{}' RE2 program size of {} > max program size of "
                      "{} set for the error level threshold. Increase "
                      "configured max program size if necessary.",
                      regex, regex_program_size, max_program_size_error_level));
    }

    const uint32_t max_program_size_warn_level = runtimeMaxProgramSizeWarnLevelOrDefault();
    if (regex_program_size > max_program_size_warn_level) {
      ENVOY_LOG_MISC(warn,
                     "regex '{}' RE2 program size of {} > max program size of {} set for the warn "
                     "level threshold. Increase configured max program size if necessary.",
                     regex, regex_program_size, max_program_size_warn_level);
    }
  }
}

CompiledGoogleReMatcher::CompiledGoogleReMatcher(
    const envoy::type::matcher::v3::RegexMatcher& config, absl::Status& creation_status)
    : regex_(config.regex(),
             makeQuietRe2OptionsWithMaxMem(clampCompilationMaxMemFromProgramSize(
                 config.google_re2().has_max_program_size()
                     ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.google_re2(), max_program_size, 100)
                     : runtimeMaxProgramSizeErrorLevelOrDefault()))) {
  if (!regex_.ok()) {
    creation_status = absl::InvalidArgumentError(regex_.error());
    return;
  }

  const uint32_t regex_program_size = static_cast<uint32_t>(regex_.ProgramSize());

  // Preserve old behavior: if deprecated max_program_size is set, follow that logic.
  if (config.google_re2().has_max_program_size()) {
    const uint32_t max_program_size =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.google_re2(), max_program_size, 100);
    if (regex_program_size > max_program_size) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("regex '{}' RE2 program size of {} > max program size of "
                      "{}. Increase configured max program size if necessary.",
                      config.regex(), regex_program_size, max_program_size));
    }
    return;
  }

  // Otherwise, follow the runtime threshold behavior (same as createAndSizeCheck path).
  const uint32_t max_program_size_error_level = runtimeMaxProgramSizeErrorLevelOrDefault();
  if (regex_program_size > max_program_size_error_level) {
    creation_status = absl::InvalidArgumentError(
        fmt::format("regex '{}' RE2 program size of {} > max program size of "
                    "{} set for the error level threshold. Increase "
                    "configured max program size if necessary.",
                    config.regex(), regex_program_size, max_program_size_error_level));
  }

  const uint32_t max_program_size_warn_level = runtimeMaxProgramSizeWarnLevelOrDefault();
  if (regex_program_size > max_program_size_warn_level) {
    ENVOY_LOG_MISC(warn,
                   "regex '{}' RE2 program size of {} > max program size of {} set for the warn "
                   "level threshold. Increase configured max program size if necessary.",
                   config.regex(), regex_program_size, max_program_size_warn_level);
  }
}

absl::StatusOr<CompiledMatcherPtr> GoogleReEngine::matcher(const std::string& regex) const {
  return CompiledGoogleReMatcher::createAndSizeCheck(regex);
}

EnginePtr GoogleReEngineFactory::createEngine(const Protobuf::Message&,
                                              Server::Configuration::ServerFactoryContext&) {
  return std::make_shared<GoogleReEngine>();
}

ProtobufTypes::MessagePtr GoogleReEngineFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::regex_engines::v3::GoogleRE2>();
}

} // namespace Regex
} // namespace Envoy
