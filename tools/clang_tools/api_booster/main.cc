// Upgrade a single Envoy C++ file to the latest API version.
//
// Currently this tool is a WiP and only does inference of .pb[.validate].h
// #include locations. This already exercises some of the muscles we need, such
// as AST matching, rudimentary type inference and API type database lookup.
//
// NOLINT(namespace-envoy)

#include <iostream>
#include <regex>
#include <set>

// Declares clang::SyntaxOnlyAction.
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "tools/type_whisperer/api_type_db.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

// Enable to see debug log messages.
#ifdef ENABLE_DEBUG_LOG
#define DEBUG_LOG(s)                                                                               \
  do {                                                                                             \
    std::cerr << (s) << std::endl;                                                                 \
  } while (0)
#else
#define DEBUG_LOG(s)
#endif

class ApiBooster : public clang::ast_matchers::MatchFinder::MatchCallback,
                   public clang::tooling::SourceFileCallbacks {
public:
  // AST match callback.
  void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override {
    // If we have a match on type, we should track the corresponding .pb.h.
    if (const clang::TypeLoc* type = result.Nodes.getNodeAs<clang::TypeLoc>("type")) {
      const std::string type_name =
          type->getType().getCanonicalType().getUnqualifiedType().getAsString();
      DEBUG_LOG(absl::StrCat("Matched type ", type_name));
      const auto result = getProtoPathFromCType(type_name);
      if (result) {
        source_api_proto_paths_.insert(*result + ".pb.h");
      }
      return;
    }

    // If we have a match on a call expression, check to see if it's something
    // like loadFromYamlAndValidate; if so, we might need to look at the
    // argument type to figure out any corresponding .pb.validate.h we require.
    if (const clang::CallExpr* call_expr = result.Nodes.getNodeAs<clang::CallExpr>("call_expr")) {
      auto* direct_callee = call_expr->getDirectCallee();
      if (direct_callee != nullptr) {
        const std::unordered_map<std::string, int> ValidateNameToArg = {
            {"loadFromYamlAndValidate", 1},
            {"loadFromFileAndValidate", 1},
            {"downcastAndValidate", 0},
            {"validate", 0},
        };
        const std::string& callee_name = direct_callee->getNameInfo().getName().getAsString();
        const auto arg = ValidateNameToArg.find(callee_name);
        // Sometimes we hit false positives because we aren't qualifying above.
        // TODO(htuch): fix this before merging.
        if (arg != ValidateNameToArg.end() && arg->second < call_expr->getNumArgs()) {
          const std::string type_name = call_expr->getArg(arg->second)
                                            ->getType()
                                            .getCanonicalType()
                                            .getUnqualifiedType()
                                            .getAsString();
          const auto result = getProtoPathFromCType(type_name);
          if (result) {
            source_api_proto_paths_.insert(*result + ".pb.validate.h");
          }
        }
      }
      return;
    }

    // The last place we need to look for .pb.validate.h reference is
    // instantiation of FactoryBase.
    if (const clang::ClassTemplateSpecializationDecl* tmpl =
            result.Nodes.getNodeAs<clang::ClassTemplateSpecializationDecl>("tmpl")) {
      const std::string tmpl_type_name = tmpl->getSpecializedTemplate()
                                             ->getInjectedClassNameSpecialization()
                                             .getCanonicalType()
                                             .getAsString();
      if (tmpl_type_name == "FactoryBase<type-parameter-0-0>") {
        const std::string type_name = tmpl->getTemplateArgs()
                                          .get(0)
                                          .getAsType()
                                          .getCanonicalType()
                                          .getUnqualifiedType()
                                          .getAsString();
        const auto result = getProtoPathFromCType(type_name);
        if (result) {
          source_api_proto_paths_.insert(*result + ".pb.validate.h");
        }
      }
    }
  }

  // Visitor callback for start of a compilation unit.
  bool handleBeginSource(clang::CompilerInstance& CI) override {
    source_api_proto_paths_.clear();
    return true;
  }

  // Visitor callback for end of a compiation unit.
  void handleEndSource() override {
    // Dump known API header paths to stdout for api_boost.py to rewrite with
    // (no rewriting support in this tool yet).
    for (const std::string& proto_path : source_api_proto_paths_) {
      std::cout << proto_path << std::endl;
    }
  }

private:
  // Convert from C++ type, e.g. envoy:config::v2::Cluster, to a proto path
  // (minus the .proto suffix), e.g. envoy/config/v2/cluster.
  absl::optional<std::string> getProtoPathFromCType(const std::string& c_type_name) {
    // Ignore compound or non-API types.
    // TODO(htuch): without compound types, this is only an under-approximation
    // of the types. Add proper logic to destructor compound types.
    const std::string type_name = std::regex_replace(c_type_name, std::regex("^(class|enum) "), "");
    if (!absl::StartsWith(type_name, "envoy::") || absl::StrContains(type_name, " ")) {
      return {};
    }

    // Convert from C++ to a qualified proto type. This is fairly hacky stuff,
    // we're essentially reversing the conventions that the protobuf C++
    // compiler is using, e.g. replacing _ and :: with . as needed, guessing
    // that a Case suffix implies some enum switching.
    const std::string dotted_path = std::regex_replace(type_name, std::regex("::"), ".");
    std::vector<std::string> frags = absl::StrSplit(dotted_path, '.');
    for (std::string& frag : frags) {
      if (!frag.empty() && isupper(frag[0])) {
        frag = std::regex_replace(frag, std::regex("_"), ".");
      }
    }
    if (absl::EndsWith(frags.back(), "Case")) {
      frags.pop_back();
    }
    const std::string proto_type_name = absl::StrJoin(frags, ".");

    // Use API type database to map from proto type to path.
    auto result = Envoy::Tools::TypeWhisperer::ApiTypeDb::getProtoPathForType(proto_type_name);
    if (result) {
      // Remove the .proto extension.
      return result->substr(0, result->size() - 6);
    } else if (!absl::StartsWith(proto_type_name, "envoy.HotRestart") &&
               !absl::StartsWith(proto_type_name, "envoy.RouterCheckToolSchema") &&
               !absl::StartsWith(proto_type_name, "envoy.test")) {
      // Die hard if we don't have a useful proto type for something that looks
      // like an API type(modulo a short whitelist).
      std::cerr << "Unknown API type: " << proto_type_name << std::endl;
      // TODO(htuch): mabye there is a nicer way to terminate AST traversal?
      ::exit(1);
    }

    return {};
  }

  // Set of inferred .pb[.validate].h, updated as the AST matcher callbacks above fire.
  std::set<std::string> source_api_proto_paths_;
};

int main(int argc, const char** argv) {
  // Apply a custom category to all command-line options so that they are the
  // only ones displayed.
  llvm::cl::OptionCategory api_booster_tool_category("api-booster options");

  clang::tooling::CommonOptionsParser OptionsParser(argc, argv, api_booster_tool_category);
  clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                 OptionsParser.getSourcePathList());

  ApiBooster api_booster;
  clang::ast_matchers::MatchFinder finder;

  // Match on all mentions of types in the AST.
  auto type_matcher =
      clang::ast_matchers::typeLoc(clang::ast_matchers::isExpansionInMainFile()).bind("type");
  finder.addMatcher(type_matcher, &api_booster);

  // Match on all call expressions. We are interested in particular in calls
  // where validation on protos is performed.
  auto call_matcher = clang::ast_matchers::callExpr().bind("call_expr");
  finder.addMatcher(call_matcher, &api_booster);

  // Match on all template instantiations.We are interested in particular in
  // instantiations of factories where validation on protos is performed.
  auto tmpl_matcher = clang::ast_matchers::classTemplateSpecializationDecl().bind("tmpl");
  finder.addMatcher(tmpl_matcher, &api_booster);

  return Tool.run(newFrontendActionFactory(&finder, &api_booster).get());
}
