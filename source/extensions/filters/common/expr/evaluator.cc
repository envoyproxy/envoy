#include "source/extensions/filters/common/expr/evaluator.h"

#include "envoy/common/exception.h"
#include "envoy/singleton/manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/utility.h"

#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

namespace {

#define ACTIVATION_TOKENS(_f)                                                                      \
  _f(Request) _f(Response) _f(Connection) _f(Context) _f(Upstream) _f(Source) _f(Destination)      \
      _f(Metadata) _f(FilterState) _f(XDS) _f(UpstreamFilterState)

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
    return CelValue::CreateMap(Protobuf::Arena::Create<RequestWrapper>(
        arena, *arena, activation_request_headers_, info, random_value_));
  case ActivationToken::Response:
    return CelValue::CreateMap(Protobuf::Arena::Create<ResponseWrapper>(
        arena, *arena, activation_response_headers_, activation_response_trailers_, info));
  case ActivationToken::Connection:
    return CelValue::CreateMap(Protobuf::Arena::Create<ConnectionWrapper>(arena, *arena, info));
  case ActivationToken::Context:
    return CelValue::CreateMap(
        Protobuf::Arena::Create<ContextWrapper>(arena, *arena, random_value_));
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
                               const Http::ResponseTrailerMap* response_trailers,
                               uint64_t random_value) {
  return std::make_unique<StreamActivation>(local_info, info, request_headers, response_headers,
                                            response_trailers, random_value);
}

BuilderPtr createBuilder(Protobuf::Arena* arena) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  google::api::expr::runtime::InterpreterOptions options;

  // Security-oriented defaults
  options.enable_comprehension = false;
  options.enable_regex = true;
  options.regex_max_program_size = 100;
  options.enable_string_conversion = false;
  options.enable_string_concat = false;
  options.enable_list_concat = false;
  options.constant_folding =
      false; // disable constant folding to prevent random() function from being inlined
  options.constant_arena = arena;

  auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
  auto register_status =
      google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry(), options);
  if (!register_status.ok()) {
    throw CelException(
        absl::StrCat("failed to register built-in functions: ", register_status.message()));
  }

  // Register custom functions
  auto sample_register_status =
      CelFunctionAdapter<absl::StatusOr<bool>, const CelMap*, double, int64_t>::CreateAndRegister(
          Sample,
          /*receiver_style=*/true, &SampleExtensionFunction, builder->GetRegistry());
  if (!sample_register_status.ok()) {
    throw CelException(
        absl::StrCat("failed to register sample function: ", sample_register_status.message()));
  }
  return builder;
}

SINGLETON_MANAGER_REGISTRATION(expression_builder);

BuilderInstanceSharedPtr getBuilder(Server::Configuration::CommonFactoryContext& context) {
  return context.singletonManager().getTyped<BuilderInstance>(
      SINGLETON_MANAGER_REGISTERED_NAME(expression_builder),
      [] { return std::make_shared<BuilderInstance>(createBuilder(nullptr)); });
}

ExpressionPtr createExpression(Builder& builder, const google::api::expr::v1alpha1::Expr& expr) {
  google::api::expr::v1alpha1::SourceInfo source_info;
  auto cel_expression_status = builder.CreateExpression(&expr, &source_info);
  if (!cel_expression_status.ok()) {
    throw CelException(
        absl::StrCat("failed to create an expression: ", cel_expression_status.status().message()));
  }
  return std::move(cel_expression_status.value());
}

absl::optional<CelValue>
evaluate(const Expression& expr, Protobuf::Arena& arena, const LocalInfo::LocalInfo* local_info,
         const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap* request_headers,
         const Http::ResponseHeaderMap* response_headers,
         const Http::ResponseTrailerMap* response_trailers, uint64_t random_value) {
  auto activation = createActivation(local_info, info, request_headers, response_headers,
                                     response_trailers, random_value);
  auto eval_status = expr.Evaluate(*activation, &arena);
  if (!eval_status.ok()) {
    return {};
  }

  return eval_status.value();
}

bool matches(const Expression& expr, const StreamInfo::StreamInfo& info,
             const Http::RequestHeaderMap& headers, uint64_t random_value) {
  Protobuf::Arena arena;
  auto eval_status =
      Expr::evaluate(expr, arena, nullptr, info, &headers, nullptr, nullptr, random_value);
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

absl::StatusOr<bool> SampleExtensionFunction(Protobuf::Arena*, const CelMap* map,
                                             double probability, int64_t idx) {
  // search for random value
  absl::optional<CelValue> entry = (*map)[CelValue::CreateStringView(RandomValue)];
  if (!entry.has_value() || !entry.value().IsUint64()) {
    return absl::InvalidArgumentError("map missing uint random value key");
  }

  // bounds check probability
  if (probability <= 0) {
    return false;
  } else if (probability >= 1) {
    return true;
  }

  // Generate deterministic random value by generating a sha of a random value
  // concatenated with the index. Use consistent endianness to ensure consistency
  // between evaluations across multiple envoy hops.
  uint64_t seed = entry.value().Uint64OrDie();
  Buffer::OwnedImpl buffer;
  buffer.writeLEInt<uint64_t>(seed);
  buffer.writeLEInt<int64_t>(idx);
  std::vector<uint8_t> digest =
      Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer);
  RELEASE_ASSERT(digest.size() >= sizeof(uint64_t), "Missing enough bytes to construct uint64_t");
  uint64_t* toCopy = reinterpret_cast<uint64_t*>(digest.data());
  uint64_t randomUint = fromEndianness<ByteOrder::LittleEndian, uint64_t>(*toCopy);

  // copied from Random::RandomGenerator. Negligibly biases high values due to floating point
  // precision.
  return randomUint < static_cast<uint64_t>(
                          probability * static_cast<double>(std::numeric_limits<uint64_t>::max()));
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
