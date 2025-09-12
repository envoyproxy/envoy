#include "source/extensions/propagators/opentelemetry/propagator_factory.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/common/common/utility.h"
#include "source/extensions/propagators/opentelemetry/w3c/trace_context_propagator.h"
#include "source/extensions/propagators/opentelemetry/b3/propagator.h"
#include "source/extensions/propagators/opentelemetry/w3c/baggage_propagator.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

constexpr absl::string_view kOtelPropagatorsEnv = "OTEL_PROPAGATORS";

CompositePropagatorPtr
PropagatorFactory::createPropagators(const std::vector<std::string>& propagator_names,
                                     Api::Api& api) {
  // Priority: explicit config > OTEL_PROPAGATORS env var > default
  if (!propagator_names.empty()) {
    return createPropagators(propagator_names);
  }

  // Try to read from OTEL_PROPAGATORS environment variable
  envoy::config::core::v3::DataSource ds;
  ds.set_environment_variable(kOtelPropagatorsEnv);

  std::string env_value = "";
  TRY_NEEDS_AUDIT {
    env_value = THROW_OR_RETURN_VALUE(Config::DataSource::read(ds, true, api), std::string);
  }
  END_TRY catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "Failed to read OTEL_PROPAGATORS environment variable: {}. Using defaults.",
              e.what());
  }

  if (!env_value.empty()) {
    std::vector<std::string> env_propagators = parseOtelPropagatorsEnv(env_value);
    if (!env_propagators.empty()) {
      ENVOY_LOG(info, "Using propagators from OTEL_PROPAGATORS environment variable: [{}]",
                absl::StrJoin(env_propagators, ", "));
      return createPropagators(env_propagators);
    }
  }

  // Fall back to default
  ENVOY_LOG(
      debug,
      "No propagators specified in config or OTEL_PROPAGATORS, using default W3C Trace Context");
  return createDefaultPropagators();
}

CompositePropagatorPtr
PropagatorFactory::createPropagators(const std::vector<std::string>& propagator_names) {
  std::vector<TextMapPropagatorPtr> propagators;

  for (const auto& name : propagator_names) {
    auto propagator = createPropagator(name);
    if (propagator) {
      propagators.push_back(std::move(propagator));
    } else {
      ENVOY_LOG(warn, "Unknown propagator name: {}. Ignoring.", name);
    }
  }

  if (propagators.empty()) {
    ENVOY_LOG(info, "No valid propagators specified, using default W3C Trace Context");
    return createDefaultPropagators();
  }

  return std::make_unique<CompositePropagator>(std::move(propagators));
}

CompositePropagatorPtr PropagatorFactory::createDefaultPropagators() {
  std::vector<TextMapPropagatorPtr> propagators;
  propagators.push_back(std::make_unique<W3CTraceContextPropagator>());
  return std::make_unique<CompositePropagator>(std::move(propagators));
}

TextMapPropagatorPtr PropagatorFactory::createPropagator(const std::string& name) {
  if (name == "tracecontext") {
    return std::make_unique<W3CTraceContextPropagator>();
  } else if (name == "b3") {
    return std::make_unique<B3Propagator>();
  } else if (name == "baggage") {
    return std::make_unique<BaggagePropagator>();
  }

  return nullptr;
}

std::vector<std::string> PropagatorFactory::parseOtelPropagatorsEnv(const std::string& env_value) {
  std::vector<std::string> propagators;

  // Split by comma and trim whitespace, following OTEL spec
  for (const auto& token : StringUtil::splitToken(env_value, ",")) {
    std::string trimmed = std::string(StringUtil::trim(token));
    if (!trimmed.empty()) {
      propagators.push_back(trimmed);
    }
  }

  return propagators;
}

// Static members for global propagator support
CompositePropagatorPtr PropagatorFactory::global_propagator_;
std::once_flag PropagatorFactory::global_propagator_once_;

CompositePropagator& PropagatorFactory::getGlobalTextMapPropagator() {
  std::call_once(global_propagator_once_, []() {
    if (!global_propagator_) {
      global_propagator_ = createDefaultPropagators();
    }
  });
  return *global_propagator_;
}

void PropagatorFactory::setGlobalTextMapPropagator(CompositePropagatorPtr propagator) {
  if (!propagator) {
    throw std::invalid_argument("Global propagator cannot be null");
  }
  global_propagator_ = std::move(propagator);
}

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
