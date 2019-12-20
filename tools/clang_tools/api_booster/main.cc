// Upgrade a single Envoy C++ file to the latest API version.
//
// Currently this tool is a WIP and only does inference of .pb[.validate].h
// #include locations. This already exercises some of the muscles we need, such
// as AST matching, rudimentary type inference and API type database lookup.
//
// NOLINT(namespace-envoy)

#include <fstream>
#include <iostream>
#include <regex>
#include <set>

// Declares clang::SyntaxOnlyAction.
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/ReplacementsYaml.h"

// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "proto_cxx_utils.h"

#include "tools/type_whisperer/api_type_db.h"

#include "absl/strings/str_cat.h"

// Enable to see debug log messages.
#ifdef ENABLE_DEBUG_LOG
#define DEBUG_LOG(s)                                                                               \
  do {                                                                                             \
    std::cerr << (s) << std::endl;                                                                 \
  } while (0)
#else
#define DEBUG_LOG(s)
#endif

using namespace Envoy::Tools::TypeWhisperer;

namespace ApiBooster {

class ApiBooster : public clang::ast_matchers::MatchFinder::MatchCallback,
                   public clang::tooling::SourceFileCallbacks {
public:
  ApiBooster(std::map<std::string, clang::tooling::Replacements>& replacements)
      : replacements_(replacements) {}

  // AST match callback.
  void run(const clang::ast_matchers::MatchFinder::MatchResult& match_result) override {
    clang::SourceManager& source_manager = match_result.Context->getSourceManager();
    // If we have a match on type, we should track the corresponding .pb.h and
    // attempt to upgrade.
    if (const clang::TypeLoc* type_loc = match_result.Nodes.getNodeAs<clang::TypeLoc>("type")) {
      const std::string type_name =
          type_loc->getType().getCanonicalType().getUnqualifiedType().getAsString();
      const auto result = getLatestTypeInformationFromCType(type_name);
      if (result) {
        source_api_proto_paths_.insert(adjustProtoSuffix(result->proto_path_, ".pb.h"));
        DEBUG_LOG(absl::StrCat("Matched type ", type_name, " ", type_loc->getTypeLocClass(), " ",
                               type_loc->getType()->getTypeClassName()));
        addTypeLocReplacements(*type_loc, result->type_name_, source_manager);
      }
      return;
    }

    // If we have a match on a call expression, check to see if it's something
    // like loadFromYamlAndValidate; if so, we might need to look at the
    // argument type to figure out any corresponding .pb.validate.h we require.
    if (const clang::CallExpr* call_expr =
            match_result.Nodes.getNodeAs<clang::CallExpr>("call_expr")) {
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
          const auto result = getLatestTypeInformationFromCType(type_name);
          if (result) {
            source_api_proto_paths_.insert(
                adjustProtoSuffix(result->proto_path_, ".pb.validate.h"));
          }
        }
      }
      return;
    }

    // The last place we need to look for .pb.validate.h reference is
    // instantiation of FactoryBase.
    if (const clang::ClassTemplateSpecializationDecl* tmpl =
            match_result.Nodes.getNodeAs<clang::ClassTemplateSpecializationDecl>("tmpl")) {
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
        const auto result = getLatestTypeInformationFromCType(type_name);
        if (result) {
          source_api_proto_paths_.insert(adjustProtoSuffix(result->proto_path_, ".pb.validate.h"));
        }
      }
    }
  }

  // Visitor callback for start of a compilation unit.
  bool handleBeginSource(clang::CompilerInstance& CI) override {
    source_api_proto_paths_.clear();
    return true;
  }

  // Visitor callback for end of a compilation unit.
  void handleEndSource() override {
    // Dump known API header paths to stdout for api_boost.py to rewrite with
    // (no rewriting support in this tool yet).
    for (const std::string& proto_path : source_api_proto_paths_) {
      std::cout << proto_path << std::endl;
    }
  }

private:
  // Attempt to add type replacements as applicable for Envoy API types.
  void addTypeLocReplacements(const clang::TypeLoc& type_loc,
                              const std::string& latest_proto_type_name,
                              const clang::SourceManager& source_manager) {
    // We only support upgrading ElaboratedTypes so far. TODO(htuch): extend
    // this to other AST type matches.
    const clang::UnqualTypeLoc unqual_type_loc = type_loc.getUnqualifiedLoc();
    if (unqual_type_loc.getTypeLocClass() == clang::TypeLoc::Elaborated) {
      clang::LangOptions lopt;
      const clang::SourceLocation start = unqual_type_loc.getSourceRange().getBegin();
      const clang::SourceLocation end = clang::Lexer::getLocForEndOfToken(
          unqual_type_loc.getSourceRange().getEnd(), 0, source_manager, lopt);
      const size_t length = source_manager.getFileOffset(end) - source_manager.getFileOffset(start);
      clang::tooling::Replacement type_replacement(
          source_manager, start, length, ProtoCxxUtils::protoToCxxType(latest_proto_type_name));
      llvm::Error error = replacements_[type_replacement.getFilePath()].add(type_replacement);
      if (error) {
        std::cerr << "  Replacement insertion error: " << llvm::toString(std::move(error))
                  << std::endl;
      } else {
        std::cerr << "  Replacement added: " << type_replacement.toString() << std::endl;
      }
    }
  }

  // Remove .proto from a path, apply specified suffix instead.
  std::string adjustProtoSuffix(absl::string_view proto_path, absl::string_view suffix) {
    return absl::StrCat(proto_path.substr(0, proto_path.size() - 6), suffix);
  }

  // Obtain the latest type information for a given from C++ type, e.g. envoy:config::v2::Cluster,
  // from the API type database.
  absl::optional<TypeInformation>
  getLatestTypeInformationFromCType(const std::string& c_type_name) {
    // Ignore compound or non-API types.
    // TODO(htuch): this is all super hacky and not really right, we should be
    // removing qualifiers etc. to get to the underlying type name.
    const std::string type_name = std::regex_replace(c_type_name, std::regex("^(class|enum) "), "");
    if (!absl::StartsWith(type_name, "envoy::") || absl::StrContains(type_name, " ")) {
      return {};
    }
    const std::string proto_type_name = ProtoCxxUtils::cxxToProtoType(type_name);

    // Use API type database to map from proto type to path.
    auto result = ApiTypeDb::getLatestTypeInformation(proto_type_name);
    if (result) {
      // Remove the .proto extension.
      return result;
    } else if (!absl::StartsWith(proto_type_name, "envoy.HotRestart") &&
               !absl::StartsWith(proto_type_name, "envoy.RouterCheckToolSchema") &&
               !absl::StartsWith(proto_type_name, "envoy.test") &&
               !absl::StartsWith(proto_type_name, "envoy.tracers.xray.daemon")) {
      // Die hard if we don't have a useful proto type for something that looks
      // like an API type(modulo a short whitelist).
      std::cerr << "Unknown API type: " << proto_type_name << std::endl;
      // TODO(htuch): maybe there is a nicer way to terminate AST traversal?
      ::exit(1);
    }

    return {};
  }

  // Set of inferred .pb[.validate].h, updated as the AST matcher callbacks above fire.
  std::set<std::string> source_api_proto_paths_;
  // Map from source file to replacements.
  std::map<std::string, clang::tooling::Replacements>& replacements_;
};

} // namespace ApiBooster

int main(int argc, const char** argv) {
  // Apply a custom category to all command-line options so that they are the
  // only ones displayed.
  llvm::cl::OptionCategory api_booster_tool_category("api-booster options");

  clang::tooling::CommonOptionsParser options_parser(argc, argv, api_booster_tool_category);
  clang::tooling::RefactoringTool tool(options_parser.getCompilations(),
                                       options_parser.getSourcePathList());

  ApiBooster::ApiBooster api_booster(tool.getReplacements());
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

  // Apply ApiBooster to AST matches. This will generate a set of replacements in
  // tool.getReplacements().
  const int run_result = tool.run(newFrontendActionFactory(&finder, &api_booster).get());
  if (run_result != 0) {
    std::cerr << "Exiting with non-zero result " << run_result << std::endl;
    return run_result;
  }

  // Serialize replacements to <main source file path>.clang-replacements.yaml.
  // These are suitable for consuming by clang-apply-replacements.
  for (const auto& file_replacement : tool.getReplacements()) {
    // Populate TranslationUnitReplacements from file replacements (this is what
    // there exists llvm::yaml serialization support for).
    clang::tooling::TranslationUnitReplacements tu_replacements;
    tu_replacements.MainSourceFile = file_replacement.first;
    for (const auto& r : file_replacement.second) {
      tu_replacements.Replacements.push_back(r);
      DEBUG_LOG(r.toString());
    }
    // Serialize TranslationUnitReplacements to YAML.
    std::string yaml_content;
    llvm::raw_string_ostream yaml_content_stream(yaml_content);
    llvm::yaml::Output yaml(yaml_content_stream);
    yaml << tu_replacements;
    // Write to <main source file path>.clang-replacements.yaml.
    std::ofstream serialized_replacement_file(tu_replacements.MainSourceFile +
                                              ".clang-replacements.yaml");
    serialized_replacement_file << yaml_content_stream.str();
  }

  return 0;
}
