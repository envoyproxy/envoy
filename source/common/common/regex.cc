#include "source/common/common/regex.h"

#include "envoy/common/exception.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Regex {

CompiledGoogleReMatcher::CompiledGoogleReMatcher(const std::string& regex,
                                                 bool do_program_size_check)
    : regex_(regex, re2::RE2::Quiet) {
  if (!regex_.ok()) {
    throw EnvoyException(regex_.error());
  }

  if (do_program_size_check) {
    Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting();
    if (runtime) {
      Stats::Scope& root_scope = runtime->getRootScope();

      const uint32_t regex_program_size = static_cast<uint32_t>(regex_.ProgramSize());
      // TODO(perf): It would be more efficient to create the stats (program size histogram,
      // warning counter) on startup and not with each regex match.
      Stats::StatNameManagedStorage program_size_stat_name("re2.program_size",
                                                           root_scope.symbolTable());
      Stats::Histogram& program_size_stat = root_scope.histogramFromStatName(
          program_size_stat_name.statName(), Stats::Histogram::Unit::Unspecified);
      program_size_stat.recordValue(regex_program_size);

      Stats::StatNameManagedStorage warn_count_stat_name("re2.exceeded_warn_level",
                                                         root_scope.symbolTable());
      Stats::Counter& warn_count = root_scope.counterFromStatName(warn_count_stat_name.statName());

      const uint32_t max_program_size_error_level =
          runtime->snapshot().getInteger("re2.max_program_size.error_level", 100);
      if (regex_program_size > max_program_size_error_level) {
        throw EnvoyException(fmt::format("regex '{}' RE2 program size of {} > max program size of "
                                         "{} set for the error level threshold. Increase "
                                         "configured max program size if necessary.",
                                         regex, regex_program_size, max_program_size_error_level));
      }

      const uint32_t max_program_size_warn_level =
          runtime->snapshot().getInteger("re2.max_program_size.warn_level", UINT32_MAX);
      if (regex_program_size > max_program_size_warn_level) {
        warn_count.inc();
        ENVOY_LOG_MISC(
            warn,
            "regex '{}' RE2 program size of {} > max program size of {} set for the warn "
            "level threshold. Increase configured max program size if necessary.",
            regex, regex_program_size, max_program_size_warn_level);
      }
    }
  }
}

CompiledGoogleReMatcher::CompiledGoogleReMatcher(
    const envoy::type::matcher::v3::RegexMatcher& config)
    : CompiledGoogleReMatcher(config.regex(), !config.google_re2().has_max_program_size()) {
  const uint32_t regex_program_size = static_cast<uint32_t>(regex_.ProgramSize());

  // Check if the deprecated field max_program_size is set first, and follow the old logic if so.
  if (config.google_re2().has_max_program_size()) {
    const uint32_t max_program_size =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.google_re2(), max_program_size, 100);
    if (regex_program_size > max_program_size) {
      throw EnvoyException(fmt::format("regex '{}' RE2 program size of {} > max program size of "
                                       "{}. Increase configured max program size if necessary.",
                                       config.regex(), regex_program_size, max_program_size));
    }
  }
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
