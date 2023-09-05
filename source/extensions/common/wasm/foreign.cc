#include "source/common/common/logger.h"
#include "source/extensions/common/wasm/ext/declare_property.pb.h"
#include "source/extensions/common/wasm/ext/set_envoy_filter_state.pb.h"
#include "source/extensions/common/wasm/wasm.h"

#if defined(WASM_USE_CEL_PARSER)
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "parser/parser.h"
#endif
#include "zlib.h"

using proxy_wasm::RegisterForeignFunction;
using proxy_wasm::WasmForeignFunction;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using CelStateType = Filters::Common::Expr::CelStateType;

template <typename T> WasmForeignFunction createFromClass() {
  auto c = std::make_shared<T>();
  return c->create(c);
}

inline StreamInfo::FilterState::LifeSpan
toFilterStateLifeSpan(envoy::source::extensions::common::wasm::LifeSpan span) {
  switch (span) {
  case envoy::source::extensions::common::wasm::LifeSpan::FilterChain:
    return StreamInfo::FilterState::LifeSpan::FilterChain;
  case envoy::source::extensions::common::wasm::LifeSpan::DownstreamRequest:
    return StreamInfo::FilterState::LifeSpan::Request;
  case envoy::source::extensions::common::wasm::LifeSpan::DownstreamConnection:
    return StreamInfo::FilterState::LifeSpan::Connection;
  default:
    return StreamInfo::FilterState::LifeSpan::FilterChain;
  }
}

RegisterForeignFunction registerCompressForeignFunction(
    "compress",
    [](WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      unsigned long dest_len = compressBound(arguments.size());
      std::unique_ptr<unsigned char[]> b(new unsigned char[dest_len]);
      if (compress(b.get(), &dest_len, reinterpret_cast<const unsigned char*>(arguments.data()),
                   arguments.size()) != Z_OK) {
        return WasmResult::SerializationFailure;
      }
      auto result = alloc_result(dest_len);
      memcpy(result, b.get(), dest_len); // NOLINT(safe-memcpy)
      return WasmResult::Ok;
    });

RegisterForeignFunction registerUncompressForeignFunction(
    "uncompress",
    [](WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      unsigned long dest_len = arguments.size() * 2 + 2; // output estimate.
      while (true) {
        std::unique_ptr<unsigned char[]> b(new unsigned char[dest_len]);
        auto r =
            uncompress(b.get(), &dest_len, reinterpret_cast<const unsigned char*>(arguments.data()),
                       arguments.size());
        if (r == Z_OK) {
          auto result = alloc_result(dest_len);
          memcpy(result, b.get(), dest_len); // NOLINT(safe-memcpy)
          return WasmResult::Ok;
        }
        if (r != Z_BUF_ERROR) {
          return WasmResult::SerializationFailure;
        }
        dest_len = dest_len * 2;
      }
    });

RegisterForeignFunction registerSetEnvoyFilterStateForeignFunction(
    "set_envoy_filter_state",
    [](WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>&) -> WasmResult {
      envoy::source::extensions::common::wasm::SetEnvoyFilterStateArguments args;
      if (args.ParseFromArray(arguments.data(), arguments.size())) {
        auto context = static_cast<Context*>(proxy_wasm::current_context_);
        return context->setEnvoyFilterState(args.path(), args.value(),
                                            toFilterStateLifeSpan(args.span()));
      }
      return WasmResult::BadArgument;
    });

#if defined(WASM_USE_CEL_PARSER)
class ExpressionFactory : public Logger::Loggable<Logger::Id::wasm> {
protected:
  struct ExpressionData {
    google::api::expr::v1alpha1::ParsedExpr parsed_expr_;
    Filters::Common::Expr::ExpressionPtr compiled_expr_;
  };

  class ExpressionContext : public StorageObject {
  public:
    friend class ExpressionFactory;
    ExpressionContext(Filters::Common::Expr::BuilderPtr builder) : builder_(std::move(builder)) {}
    uint32_t createToken() {
      uint32_t token = next_expr_token_++;
      for (;;) {
        if (!expr_.count(token)) {
          break;
        }
        token = next_expr_token_++;
      }
      return token;
    }
    bool hasExpression(uint32_t token) { return expr_.contains(token); }
    ExpressionData& getExpression(uint32_t token) { return expr_[token]; }
    void deleteExpression(uint32_t token) { expr_.erase(token); }
    Filters::Common::Expr::Builder* builder() { return builder_.get(); }

  private:
    Filters::Common::Expr::BuilderPtr builder_{};
    uint32_t next_expr_token_ = 0;
    absl::flat_hash_map<uint32_t, ExpressionData> expr_;
  };

  static ExpressionContext& getOrCreateContext(ContextBase* context_base) {
    auto context = static_cast<Context*>(context_base);
    std::string data_name = "cel";
    auto expr_context = context->getForeignData<ExpressionContext>(data_name);
    if (!expr_context) {
      google::api::expr::runtime::InterpreterOptions options;
      auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
      auto status =
          google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry(), options);
      if (!status.ok()) {
        ENVOY_LOG(warn, "failed to register built-in functions: {}", status.message());
      }
      auto new_context = std::make_unique<ExpressionContext>(std::move(builder));
      expr_context = new_context.get();
      context->setForeignData(data_name, std::move(new_context));
    }
    return *expr_context;
  }
};

class CreateExpressionFactory : public ExpressionFactory {
public:
  WasmForeignFunction create(std::shared_ptr<CreateExpressionFactory> self) const {
    WasmForeignFunction f =
        [self](WasmBase&, std::string_view expr,
               const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      auto parse_status = google::api::expr::parser::Parse(std::string(expr));
      if (!parse_status.ok()) {
        ENVOY_LOG(info, "expr_create parse error: {}", parse_status.status().message());
        return WasmResult::BadArgument;
      }

      auto& expr_context = getOrCreateContext(proxy_wasm::current_context_->root_context());
      auto token = expr_context.createToken();
      auto& handler = expr_context.getExpression(token);

      handler.parsed_expr_ = parse_status.value();
      auto cel_expression_status = expr_context.builder()->CreateExpression(
          &handler.parsed_expr_.expr(), &handler.parsed_expr_.source_info());
      if (!cel_expression_status.ok()) {
        ENVOY_LOG(info, "expr_create compile error: {}", cel_expression_status.status().message());
        expr_context.deleteExpression(token);
        return WasmResult::BadArgument;
      }

      handler.compiled_expr_ = std::move(cel_expression_status.value());
      auto result = reinterpret_cast<uint32_t*>(alloc_result(sizeof(uint32_t)));
      *result = token;
      return WasmResult::Ok;
    };
    return f;
  }
};
RegisterForeignFunction
    registerCreateExpressionForeignFunction("expr_create",
                                            createFromClass<CreateExpressionFactory>());

class EvaluateExpressionFactory : public ExpressionFactory {
public:
  WasmForeignFunction create(std::shared_ptr<EvaluateExpressionFactory> self) const {
    WasmForeignFunction f =
        [self](WasmBase&, std::string_view argument,
               const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      auto& expr_context = getOrCreateContext(proxy_wasm::current_context_->root_context());
      if (argument.size() != sizeof(uint32_t)) {
        return WasmResult::BadArgument;
      }
      uint32_t token = *reinterpret_cast<const uint32_t*>(argument.data());
      if (!expr_context.hasExpression(token)) {
        return WasmResult::NotFound;
      }
      Protobuf::Arena arena;
      auto& handler = expr_context.getExpression(token);
      auto context = static_cast<Context*>(proxy_wasm::current_context_);
      auto eval_status = handler.compiled_expr_->Evaluate(*context, &arena);
      if (!eval_status.ok()) {
        ENVOY_LOG(debug, "expr_evaluate error: {}", eval_status.status().message());
        return WasmResult::InternalFailure;
      }
      auto value = eval_status.value();
      if (value.IsError()) {
        ENVOY_LOG(debug, "expr_evaluate value error: {}", value.ErrorOrDie()->message());
        return WasmResult::InternalFailure;
      }
      std::string result;
      auto serialize_status = serializeValue(value, &result);
      if (serialize_status != WasmResult::Ok) {
        return serialize_status;
      }
      auto output = alloc_result(result.size());
      memcpy(output, result.data(), result.size()); // NOLINT(safe-memcpy)
      return WasmResult::Ok;
    };
    return f;
  }
};
RegisterForeignFunction
    registerEvaluateExpressionForeignFunction("expr_evaluate",
                                              createFromClass<EvaluateExpressionFactory>());

class DeleteExpressionFactory : public ExpressionFactory {
public:
  WasmForeignFunction create(std::shared_ptr<DeleteExpressionFactory> self) const {
    WasmForeignFunction f = [self](WasmBase&, std::string_view argument,
                                   const std::function<void*(size_t size)>&) -> WasmResult {
      auto& expr_context = getOrCreateContext(proxy_wasm::current_context_->root_context());
      if (argument.size() != sizeof(uint32_t)) {
        return WasmResult::BadArgument;
      }
      uint32_t token = *reinterpret_cast<const uint32_t*>(argument.data());
      expr_context.deleteExpression(token);
      return WasmResult::Ok;
    };
    return f;
  }
};
RegisterForeignFunction
    registerDeleteExpressionForeignFunction("expr_delete",
                                            createFromClass<DeleteExpressionFactory>());
#endif

// TODO(kyessenov) The factories should be separated into individual compilation units.
// TODO(kyessenov) Leverage the host argument marshaller instead of the protobuf argument list.
class DeclarePropertyFactory {
public:
  WasmForeignFunction create(std::shared_ptr<DeclarePropertyFactory> self) const {
    WasmForeignFunction f = [self](WasmBase&, std::string_view arguments,
                                   const std::function<void*(size_t size)>&) -> WasmResult {
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      if (args.ParseFromArray(arguments.data(), arguments.size())) {
        CelStateType type = CelStateType::Bytes;
        switch (args.type()) {
        case envoy::source::extensions::common::wasm::WasmType::Bytes:
          type = CelStateType::Bytes;
          break;
        case envoy::source::extensions::common::wasm::WasmType::Protobuf:
          type = CelStateType::Protobuf;
          break;
        case envoy::source::extensions::common::wasm::WasmType::String:
          type = CelStateType::String;
          break;
        case envoy::source::extensions::common::wasm::WasmType::FlatBuffers:
          type = CelStateType::FlatBuffers;
          break;
        default:
          // do nothing
          break;
        }
        StreamInfo::FilterState::LifeSpan span = toFilterStateLifeSpan(args.span());
        auto context = static_cast<Context*>(proxy_wasm::current_context_);
        return context->declareProperty(
            args.name(), std::make_unique<const Filters::Common::Expr::CelStatePrototype>(
                             args.readonly(), type, args.schema(), span));
      }
      return WasmResult::BadArgument;
    };
    return f;
  }
};
RegisterForeignFunction
    registerDeclarePropertyForeignFunction("declare_property",
                                           createFromClass<DeclarePropertyFactory>());

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
