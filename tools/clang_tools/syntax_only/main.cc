// This is a copy of the Hello World-style syntax check tool described in
// https://clang.llvm.org/docs/LibTooling.html. It's purpose is to provide an
// example of how to build and run libtooling based Envoy developer tools.
//
// NOLINT(namespace-envoy)

// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

using namespace clang::tooling;
using namespace llvm;

int main(int argc, const char** argv) {

  // Apply a custom category to all command-line options so that they are the
  // only ones displayed.
  llvm::cl::OptionCategory MyToolCategory("my-tool options");

  // CommonOptionsParser declares HelpMessage with a description of the common
  // command-line options related to the compilation database and input files.
  // It's nice to have this help message in all tools.
  cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

  // A help message for this specific tool can be added afterwards.
  cl::extrahelp MoreHelp("\nMore help text...\n");

  CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
}
