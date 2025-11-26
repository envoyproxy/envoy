#include "source/common/common/logger.h"
#include "source/extensions/common/wasm/ext/declare_property.pb.h"
#include "source/extensions/common/wasm/ext/set_envoy_filter_state.pb.h"
#include "source/extensions/common/wasm/ext/sign.pb.h"
#include "source/extensions/common/wasm/ext/verify_signature.pb.h"
#include "source/extensions/common/wasm/wasm.h"

#if defined(WASM_USE_CEL_PARSER)
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "parser/parser.h"
#endif
#include "zlib.h"
#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"

#include "absl/types/span.h"

using proxy_wasm::RegisterForeignFunction;
using proxy_wasm::WasmForeignFunction;

namespace Envoy {

namespace {
// Helper function to import public key from either PEM or DER format
Envoy::Common::Crypto::PKeyObjectPtr
importPublicKey(Envoy::Common::Crypto::Utility& crypto_util,
                const envoy::source::extensions::common::wasm::VerifySignatureArguments& args) {
  bool has_pem = !args.public_key_pem().empty();
  bool has_der = !args.public_key().empty();

  if (has_pem && has_der) {
    return nullptr; // Both PEM and DER keys provided
  }

  if (has_pem) {
    return crypto_util.importPublicKeyPEM(args.public_key_pem());
  } else if (has_der) {
    auto key_str = args.public_key();
    return crypto_util.importPublicKeyDER(
        absl::MakeSpan(reinterpret_cast<const uint8_t*>(key_str.data()), key_str.size()));
  } else {
    return nullptr; // No key provided
  }
}

// Helper function to import private key from either PEM or DER format
Envoy::Common::Crypto::PKeyObjectPtr
importPrivateKey(Envoy::Common::Crypto::Utility& crypto_util,
                 const envoy::source::extensions::common::wasm::SignArguments& args) {
  bool has_pem = !args.private_key_pem().empty();
  bool has_der = !args.private_key().empty();

  if (has_pem && has_der) {
    return nullptr; // Both PEM and DER keys provided
  }

  if (has_pem) {
    return crypto_util.importPrivateKeyPEM(args.private_key_pem());
  } else if (has_der) {
    auto key_str = args.private_key();
    return crypto_util.importPrivateKeyDER(
        absl::MakeSpan(reinterpret_cast<const uint8_t*>(key_str.data()), key_str.size()));
  } else {
    return nullptr; // No key provided
  }
}
} // namespace
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

RegisterForeignFunction registerVerifySignatureForeignFunction(
    "verify_signature",
    [](WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      envoy::source::extensions::common::wasm::VerifySignatureArguments args;
      if (args.ParseFromString(arguments)) {
        const auto& hash = args.hash_function();
        auto signature_str = args.signature();
        auto text_str = args.text();

        auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
        auto crypto_ptr = importPublicKey(crypto_util, args);
        if (!crypto_ptr) {
          return WasmResult::BadArgument;
        }

        auto output = crypto_util.verifySignature(
            hash, *crypto_ptr,
            absl::MakeSpan(reinterpret_cast<const uint8_t*>(signature_str.data()),
                           signature_str.size()),
            absl::MakeSpan(reinterpret_cast<const uint8_t*>(text_str.data()), text_str.size()));

        envoy::source::extensions::common::wasm::VerifySignatureResult verification_result;
        if (output.ok()) {
          verification_result.set_result(true);
          verification_result.set_error("");
        } else {
          verification_result.set_result(false);
          verification_result.set_error(output.message());
        }

        auto size = verification_result.ByteSizeLong();
        auto result = alloc_result(size);
        verification_result.SerializeToArray(result, static_cast<int>(size));
        return WasmResult::Ok;
      }
      return WasmResult::BadArgument;
    });

RegisterForeignFunction registerSignForeignFunction(
    "sign",
    [](WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      envoy::source::extensions::common::wasm::SignArguments args;
      if (args.ParseFromArray(arguments.data(), arguments.size())) {
        const auto& hash = args.hash_function();
        auto text_str = args.text();

        auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
        auto crypto_ptr = importPrivateKey(crypto_util, args);
        if (!crypto_ptr) {
          return WasmResult::BadArgument;
        }

        auto output = crypto_util.sign(
            hash, *crypto_ptr,
            absl::MakeSpan(reinterpret_cast<const uint8_t*>(text_str.data()), text_str.size()));

        envoy::source::extensions::common::wasm::SignResult signing_result;
        if (output.ok()) {
          signing_result.set_result(true);
          signing_result.set_signature(output->data(), output->size());
          signing_result.set_error("");
        } else {
          signing_result.set_result(false);
          signing_result.set_error(output.status().message());
        }

        auto size = signing_result.ByteSizeLong();
        auto result = alloc_result(size);
        signing_result.SerializeToArray(result, static_cast<int>(size));
        return WasmResult::Ok;
      }
      return WasmResult::BadArgument;
    });

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
      if (args.ParseFromString(arguments)) {
        auto context = static_cast<Context*>(proxy_wasm::current_context_);
        return context->setEnvoyFilterState(args.path(), args.value(),
                                            toFilterStateLifeSpan(args.span()));
      }
      return WasmResult::BadArgument;
    });

RegisterForeignFunction registerClearRouteCacheForeignFunction(
    "clear_route_cache",
    [](WasmBase&, std::string_view, const std::function<void*(size_t size)>&) -> WasmResult {
      auto context = static_cast<Context*>(proxy_wasm::current_context_);
      context->clearRouteCache();
      return WasmResult::Ok;
    });

#if defined(WASM_USE_CEL_PARSER)
class ExpressionFactory : public Logger::Loggable<Logger::Id::wasm> {
protected:
  struct ExpressionData {
    cel::expr::ParsedExpr parsed_expr_;
    Filters::Common::Expr::ExpressionPtr compiled_expr_;
  };

  class ExpressionContext : public StorageObject {
  public:
    friend class ExpressionFactory;
    ExpressionContext(Filters::Common::Expr::BuilderConstPtr builder)
        : builder_(std::move(builder)) {}
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
    const Filters::Common::Expr::Builder* builder() const { return builder_.get(); }

  private:
    const Filters::Common::Expr::BuilderConstPtr builder_{};
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

      const auto& parsed_expr = parse_status.value();
      handler.parsed_expr_ = parsed_expr;

      std::vector<absl::Status> warnings;
      auto cel_expression_status = expr_context.builder()->CreateExpression(
          &handler.parsed_expr_.expr(), &handler.parsed_expr_.source_info(), &warnings);

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
      if (args.ParseFromString(arguments)) {
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
