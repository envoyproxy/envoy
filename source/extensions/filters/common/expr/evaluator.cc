#include "source/extensions/filters/common/expr/evaluator.h"

#include "envoy/common/exception.h"
#include "envoy/singleton/manager.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "extensions/regex_functions.h"
#include "extensions/strings.h"

#include "cel/expr/syntax.pb.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

namespace {

#define ACTIVATION_TOKENS(_f)                                                                      \
  _f(Request) _f(Response) _f(Connection) _f(Upstream) _f(Source) _f(Destination) _f(Metadata)     \
      _f(FilterState) _f(XDS) _f(UpstreamFilterState)

#define _DECLARE(_t) _t,
enum class ActivationToken { ACTIVATION_TOKENS(_DECLARE) };
#undef _DECLARE

using ActivationLookupTable = absl::flat_hash_map<absl::string_view, ActivationToken>;

#define _PAIR(_t) {_t, ActivationToken::_t},
const ActivationLookupTable& getActivationTokens() {
  CONSTRUCT_ON_FIRST_USE(ActivationLookupTable, {ACTIVATION_TOKENS(_PAIR)});
#undef _PAIR
}

} // namespace

absl::optional<CelValue> StreamActivation::FindValue(absl::string_view name,
                                                     Protobuf::Arena* arena) const {
  const auto& tokens = getActivationTokens();
  const auto token = tokens.find(name);
  if (token == tokens.end()) {
    return {};
  }
  if (token->second == ActivationToken::XDS) {
    return CelValue::CreateMap(
        Protobuf::Arena::Create<XDSWrapper>(arena, *arena, activation_info_, local_info_));
  }
  if (activation_info_ == nullptr) {
    return {};
  }
  const StreamInfo::StreamInfo& info = *activation_info_;
  switch (token->second) {
  case ActivationToken::Request:
    return CelValue::CreateMap(
        Protobuf::Arena::Create<RequestWrapper>(arena, *arena, activation_request_headers_, info));
  case ActivationToken::Response:
    return CelValue::CreateMap(Protobuf::Arena::Create<ResponseWrapper>(
        arena, *arena, activation_response_headers_, activation_response_trailers_, info));
  case ActivationToken::Connection:
    return CelValue::CreateMap(Protobuf::Arena::Create<ConnectionWrapper>(arena, *arena, info));
  case ActivationToken::Upstream:
    return CelValue::CreateMap(Protobuf::Arena::Create<UpstreamWrapper>(arena, *arena, info));
  case ActivationToken::Source:
    return CelValue::CreateMap(Protobuf::Arena::Create<PeerWrapper>(arena, *arena, info, false));
  case ActivationToken::Destination:
    return CelValue::CreateMap(Protobuf::Arena::Create<PeerWrapper>(arena, *arena, info, true));
  case ActivationToken::Metadata:
    return CelProtoWrapper::CreateMessage(&info.dynamicMetadata(), arena);
  case ActivationToken::FilterState:
    return CelValue::CreateMap(
        Protobuf::Arena::Create<FilterStateWrapper>(arena, *arena, info.filterState()));
  case ActivationToken::XDS:
    return {};
  case ActivationToken::UpstreamFilterState:
    if (info.upstreamInfo().has_value() &&
        info.upstreamInfo().value().get().upstreamFilterState() != nullptr) {
      return CelValue::CreateMap(Protobuf::Arena::Create<FilterStateWrapper>(
          arena, *arena, *info.upstreamInfo().value().get().upstreamFilterState()));
    }
  }
  return {};
}

void StreamActivation::resetActivation() const {
  local_info_ = nullptr;
  activation_info_ = nullptr;
  activation_request_headers_ = nullptr;
  activation_response_headers_ = nullptr;
  activation_response_trailers_ = nullptr;
}

ActivationPtr createActivation(const LocalInfo::LocalInfo* local_info,
                               const StreamInfo::StreamInfo& info,
                               const Http::RequestHeaderMap* request_headers,
                               const Http::ResponseHeaderMap* response_headers,
                               const Http::ResponseTrailerMap* response_trailers) {
  return std::make_unique<StreamActivation>(local_info, info, request_headers, response_headers,
                                            response_trailers);
}

BuilderConstPtr createBuilder(OptRef<const envoy::config::core::v3::CelExpressionConfig> config,
                              Protobuf::Arena* arena) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  google::api::expr::runtime::InterpreterOptions options;

  // Security-oriented defaults.
  options.enable_comprehension = false;
  options.enable_regex = true;
  options.regex_max_program_size = 100;
  options.enable_qualified_identifier_rewrites = true;

  // Resolve options from configuration or fall back to security-oriented defaults.
  bool enable_string_functions = false;
  if (config.has_value()) {
    options.enable_string_conversion = config->enable_string_conversion();
    options.enable_string_concat = config->enable_string_concat();
    enable_string_functions = config->enable_string_functions();
  } else {
    options.enable_string_conversion = false;
    options.enable_string_concat = false;
  }
  options.enable_list_concat = false;

  // Performance-oriented defaults.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_cel_regex_precompilation")) {
    options.enable_regex_precompilation = true;
  }

  // Enable constant folding with arena if provided for RBAC backward compatibility optimization.
  if (arena != nullptr) {
    options.constant_folding = true;
    options.constant_arena = arena;
  }

  auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
  auto register_status =
      google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry(), options);
  if (!register_status.ok()) {
    throw CelException(
        absl::StrCat("failed to register built-in functions: ", register_status.message()));
  }
  auto ext_register_status =
      cel::extensions::RegisterRegexFunctions(builder->GetRegistry(), options);
  if (!ext_register_status.ok()) {
    throw CelException(absl::StrCat("failed to register extension regex functions: ",
                                    ext_register_status.message()));
  }
  // Register string extension functions only if enabled in configuration.
  if (enable_string_functions) {
    auto string_register_status =
        cel::extensions::RegisterStringsFunctions(builder->GetRegistry(), options);
    if (!string_register_status.ok()) {
      throw CelException(absl::StrCat("failed to register extension string functions: ",
                                      string_register_status.message()));
    }
  }
  return builder;
}

BuilderInstanceSharedConstPtr BuilderCache::getOrCreateBuilder(
    OptRef<const envoy::config::core::v3::CelExpressionConfig> config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  ConfigHash hash = 0;
  if (config.has_value()) {
    // Use MessageUtil::hash for proto hashing.
    hash = MessageUtil::hash(config.ref());
  }

  auto it = builders_.find(hash);
  if (it != builders_.end()) {
    auto locked_builder = it->second.lock();
    if (locked_builder) {
      return locked_builder;
    }
  }

  // Create new builder with the configuration.
  auto builder = createBuilder(config);
  auto instance = std::make_shared<BuilderInstance>(std::move(builder), shared_from_this());
  // Store as weak_ptr to allow release after xDS unload.
  builders_[hash] = instance;
  return instance;
}

SINGLETON_MANAGER_REGISTRATION(builder_cache);

BuilderInstanceSharedConstPtr
getBuilder(Server::Configuration::CommonFactoryContext& context,
           OptRef<const envoy::config::core::v3::CelExpressionConfig> config) {
  auto cache = context.singletonManager().getTyped<BuilderCache>(
      SINGLETON_MANAGER_REGISTERED_NAME(builder_cache),
      [] { return std::make_shared<BuilderCache>(); });
  return cache->getOrCreateBuilder(config);
}

absl::StatusOr<CompiledExpression>
CompiledExpression::Create(Server::Configuration::CommonFactoryContext& context,
                           const cel::expr::Expr& expr,
                           OptRef<const envoy::config::core::v3::CelExpressionConfig> config) {
  auto builder = getBuilder(context, config);
  return Create(builder, expr);
}

absl::StatusOr<CompiledExpression>
CompiledExpression::Create(const BuilderInstanceSharedConstPtr& builder,
                           const cel::expr::Expr& expr) {
  std::vector<absl::Status> warnings;
  CompiledExpression out = CompiledExpression(builder, expr);
  auto cel_expression_status = out.builder_->builder().CreateExpression(
      &out.source_expr_, &cel::expr::SourceInfo::default_instance(), &warnings);
  if (!cel_expression_status.ok()) {
    return cel_expression_status.status();
  }
  out.expr_ = std::move(cel_expression_status.value());
  return out;
}

absl::StatusOr<CompiledExpression>
CompiledExpression::Create(const BuilderInstanceSharedConstPtr& builder,
                           const xds::type::v3::CelExpression& xds_expr) {
  // First try to get expression from the new CEL canonical format.
  if (xds_expr.has_cel_expr_checked()) {
    return Create(builder, xds_expr.cel_expr_checked().expr());
  } else if (xds_expr.has_cel_expr_parsed()) {
    return Create(builder, xds_expr.cel_expr_parsed().expr());
  }
  // Fallback to handling legacy formats for backward compatibility.
  switch (xds_expr.expr_specifier_case()) {
  case xds::type::v3::CelExpression::ExprSpecifierCase::kParsedExpr:
    return Create(builder, xds_expr.parsed_expr().expr());
  case xds::type::v3::CelExpression::ExprSpecifierCase::kCheckedExpr:
    return Create(builder, xds_expr.checked_expr().expr());
  default:
    return absl::InvalidArgumentError("CEL expression not set.");
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::StatusOr<CompiledExpression>
CompiledExpression::Create(const BuilderInstanceSharedConstPtr& builder,
                           const google::api::expr::v1alpha1::Expr& expr) {
  std::string serialized;
  if (!expr.SerializeToString(&serialized)) {
    return absl::InvalidArgumentError(
        "Failed to serialize google::api::expr::v1alpha1 expression.");
  }
  cel::expr::Expr new_expr;
  if (!new_expr.ParseFromString(serialized)) {
    return absl::InvalidArgumentError("Failed to convert to cel::expr expression.");
  }
  return Create(builder, new_expr);
}

absl::optional<CelValue> CompiledExpression::evaluate(
    Protobuf::Arena& arena, const ::Envoy::LocalInfo::LocalInfo* local_info,
    const StreamInfo::StreamInfo& info, const ::Envoy::Http::RequestHeaderMap* request_headers,
    const ::Envoy::Http::ResponseHeaderMap* response_headers,
    const ::Envoy::Http::ResponseTrailerMap* response_trailers) const {
  auto activation =
      createActivation(local_info, info, request_headers, response_headers, response_trailers);
  auto eval_status = expr_->Evaluate(*activation, &arena);
  if (!eval_status.ok()) {
    return {};
  }

  return eval_status.value();
}

absl::StatusOr<CelValue> CompiledExpression::evaluate(const Activation& activation,
                                                      Protobuf::Arena* arena) const {
  return expr_->Evaluate(activation, arena);
}

bool CompiledExpression::matches(const StreamInfo::StreamInfo& info,
                                 const Http::RequestHeaderMap& headers) const {
  Protobuf::Arena arena;
  auto eval_status = evaluate(arena, nullptr, info, &headers, nullptr, nullptr);
  if (!eval_status.has_value()) {
    return false;
  }
  auto result = eval_status.value();
  return result.IsBool() ? result.BoolOrDie() : false;
}

std::string print(CelValue value) {
  switch (value.type()) {
  case CelValue::Type::kBool:
    return value.BoolOrDie() ? "true" : "false";
  case CelValue::Type::kInt64:
    return absl::StrCat(value.Int64OrDie());
  case CelValue::Type::kUint64:
    return absl::StrCat(value.Uint64OrDie());
  case CelValue::Type::kDouble:
    return absl::StrCat(value.DoubleOrDie());
  case CelValue::Type::kString:
    return std::string(value.StringOrDie().value());
  case CelValue::Type::kBytes:
    return std::string(value.BytesOrDie().value());
  case CelValue::Type::kMessage:
    return value.IsNull() ? "NULL" : value.MessageOrDie()->ShortDebugString();
  case CelValue::Type::kDuration:
    return absl::FormatDuration(value.DurationOrDie());
  case CelValue::Type::kTimestamp:
    return absl::FormatTime(value.TimestampOrDie(), absl::UTCTimeZone());
  case CelValue::Type::kNullType:
    return "NULL";
  default:
    return absl::StrCat(CelValue::TypeName(value.type()), " value");
  }
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
