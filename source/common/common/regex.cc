#include "common/common/regex.h"

#include "envoy/common/exception.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/utility.h"

#include "re2/re2.h"

namespace Envoy {
namespace Regex {
namespace {

class CompiledStdMatcher : public CompiledMatcher {
public:
  CompiledStdMatcher(std::regex&& regex) : regex_(std::move(regex)) {}

  // CompiledMatcher
  bool match(absl::string_view value) const override {
    try {
      return std::regex_match(value.begin(), value.end(), regex_);
    } catch (const std::regex_error& e) {
      return false;
    }
  }

  // CompiledMatcher
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override {
    try {
      return std::regex_replace(std::string(value), regex_, std::string(substitution));
    } catch (const std::regex_error& e) {
      return std::string(value);
    }
  }

private:
  const std::regex regex_;
};

class CompiledGoogleReMatcher : public CompiledMatcher {
public:
  CompiledGoogleReMatcher(const envoy::type::matcher::v3::RegexMatcher& config)
      : regex_(config.regex(), re2::RE2::Quiet), runtime_(Runtime::LoaderSingleton::getExisting()) {
    if (!regex_.ok()) {
      throw EnvoyException(regex_.error());
    }

    const uint32_t regex_program_size = static_cast<uint32_t>(regex_.ProgramSize());

    // Check if the deprecated field max_program_size is set first, and follow that logic if so.
    if (config.google_re2().has_max_program_size()) {
      const uint32_t max_program_size =
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.google_re2(), max_program_size, 100);
      if (regex_program_size > max_program_size) {
        throw EnvoyException(fmt::format("regex '{}' RE2 program size of {} > max program size of "
                                         "{}. Increase configured max program size if necessary.",
                                         config.regex(), regex_program_size, max_program_size));
      }
      return;
    }

    if (runtime_) {
      // Check the error level threshold for the max program size.
      if (config.google_re2().has_max_program_size_error_level()) {
        // If the default value isn't set by the user, it defaults to 100 to emulate the old
        // behavior.
        uint32_t max_program_size_error_level =
            config.google_re2().max_program_size_error_level().default_value();
        if (max_program_size_error_level == 0) {
          max_program_size_error_level = 100;
        }
        if (!config.google_re2().max_program_size_error_level().runtime_key().empty()) {
          max_program_size_error_level = runtime_->snapshot().getInteger(
              config.google_re2().max_program_size_error_level().runtime_key(),
              max_program_size_error_level);
        }
        if (regex_program_size > max_program_size_error_level) {
          // Increment stat.
          throw EnvoyException(
              fmt::format("regex '{}' RE2 program size of {} > max program size of "
                          "{} set for the error level threshold. Increase configured max program "
                          "size if necessary.",
                          config.regex(), regex_program_size, max_program_size_error_level));
        }
      }

      // Check the warn level threshold for the max program size.
      if (config.google_re2().has_max_program_size_warn_level()) {
        // If the default value isn't set by the user, no check is enforced by it (unlimited).
        uint32_t max_program_size_warn_level =
            config.google_re2().max_program_size_warn_level().default_value();
        if (max_program_size_warn_level == 0) {
          max_program_size_warn_level = UINT32_MAX;
        }
        if (!config.google_re2().max_program_size_warn_level().runtime_key().empty()) {
          max_program_size_warn_level = runtime_->snapshot().getInteger(
              config.google_re2().max_program_size_warn_level().runtime_key(),
              max_program_size_warn_level);
        }
        if (regex_program_size > max_program_size_warn_level) {
          // Increment stat.
          ENVOY_LOG_MISC(
              warn,
              "regex '{}' RE2 program size of {} > max program size of {} set for the warning "
              "level threshold. Increase configured max program size if necessary.",
              config.regex(), regex_program_size, max_program_size_warn_level);
        }
      }
    }
  }

  // CompiledMatcher
  bool match(absl::string_view value) const override {
    return re2::RE2::FullMatch(re2::StringPiece(value.data(), value.size()), regex_);
  }

  // CompiledMatcher
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override {
    std::string result = std::string(value);
    re2::RE2::GlobalReplace(&result, regex_,
                            re2::StringPiece(substitution.data(), substitution.size()));
    return result;
  }

private:
  const re2::RE2 regex_;
  Runtime::Loader* runtime_;
};

} // namespace

CompiledMatcherPtr Utility::parseRegex(const envoy::type::matcher::v3::RegexMatcher& matcher) {
  // Google Re is the only currently supported engine.
  ASSERT(matcher.has_google_re2());
  return std::make_unique<CompiledGoogleReMatcher>(matcher);
}

CompiledMatcherPtr Utility::parseStdRegexAsCompiledMatcher(const std::string& regex,
                                                           std::regex::flag_type flags) {
  return std::make_unique<CompiledStdMatcher>(parseStdRegex(regex, flags));
}

std::regex Utility::parseStdRegex(const std::string& regex, std::regex::flag_type flags) {
  // TODO(zuercher): In the future, PGV (https://github.com/envoyproxy/protoc-gen-validate)
  // annotations may allow us to remove this in favor of direct validation of regular
  // expressions.
  try {
    return std::regex(regex, flags);
  } catch (const std::regex_error& e) {
    throw EnvoyException(fmt::format("Invalid regex '{}': {}", regex, e.what()));
  }
}

} // namespace Regex
} // namespace Envoy
