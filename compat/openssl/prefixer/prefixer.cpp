#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/ADT/Statistic.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <regex>
#include <glob.h>


namespace opt {
  static std::set<std::string>    srcpaths;
  static std::set<std::string>    srcincl;
  static std::set<std::string>    srcskip;
  static std::set<std::string>    includes;
  static std::filesystem::path    output  = std::filesystem::current_path();
  static std::string              prefix  = "ossl";
  static bool                     verbose = false;

  static std::vector<std::regex>  extraIdentifiers  = {
      std::regex("^OPENSSL_.*", std::regex::basic | std::regex::optimize),
      std::regex("^AES_.*", std::regex::basic | std::regex::optimize),
      std::regex("^ASN1_.*", std::regex::basic | std::regex::optimize),
      std::regex("^sk_$", std::regex::basic | std::regex::optimize),
      std::regex("^d2i_", std::regex::basic | std::regex::optimize),
      std::regex("^i2d_", std::regex::basic | std::regex::optimize),
      std::regex("^DIRECTORYSTRING$", std::regex::basic | std::regex::optimize),
      std::regex("^DISPLAYTEXT$", std::regex::basic | std::regex::optimize),
      std::regex("^RSAPublicKey$", std::regex::basic | std::regex::optimize),
      std::regex("^RSAPrivateKey$", std::regex::basic | std::regex::optimize),
      std::regex("^DHparams$", std::regex::basic | std::regex::optimize),
      std::regex("^PKCS7_ATTR_", std::regex::basic | std::regex::optimize),
      std::regex("^PEM_read_", std::regex::basic | std::regex::optimize),
      std::regex("^PEM_write_", std::regex::basic | std::regex::optimize),
      std::regex("^PEM_", std::regex::basic | std::regex::optimize),
      std::regex("^ossl_check_$", std::regex::basic | std::regex::optimize),
      std::regex("^ossl_check_const_$", std::regex::basic | std::regex::optimize),
  };

  static std::map<std::string,std::vector<std::pair<std::string,std::pair<std::string,std::string>>>> extraInsertions = {
      {
          "openssl/asn1.h", {
              {
                  "attr type *name##_dup(const type *a);",
                  {
                      "attr type *",
                      "##_##name##_dup(const type *a);"
                  }
              },
          }
      }
  };

  static std::map<std::string,bool> headers; // Relative to srcpath e.g. "openssl/x509.h"

  static llvm::raw_ostream &vstr() { return verbose ? llvm::outs() : llvm::nulls(); }

  static std::filesystem::path incdir() { return opt::output / "include"; }
  static std::filesystem::path srcdir() { return opt::output / "source"; }
  static std::filesystem::path hfile() { return opt::incdir() / (opt::prefix + ".h"); }
  static std::filesystem::path cfile() { return opt::srcdir() / (opt::prefix + ".c"); }
};



static bool isAnonymousFunctionPointerType(const clang::QualType &qt) {
  if (qt->isFunctionPointerType()) {
    if (qt->getAs<clang::TypedefType>() == nullptr) {
      return true;
    }
  }
  return false;
}

clang::SourceLocation getFileLoc(const clang::SourceManager &srcmgr, clang::SourceLocation sloc) {
  if (sloc.isFileID()) {
    return sloc;
  }

  do {
    if (srcmgr.isMacroArgExpansion(sloc)) {
      sloc = srcmgr.getImmediateSpellingLoc(sloc);
    }
    else {
      sloc = srcmgr.getImmediateExpansionRange(sloc).getBegin();
    }
  } while (!sloc.isFileID());

  return sloc;
}


class Function {
  public:

    Function(clang::FunctionDecl *node) : m_node(node) {
    }

    bool hasBody() const {
      return m_node->doesThisDeclarationHaveABody();
    }

    std::string getHeader(const clang::SourceManager &srcmgr) const {
      clang::SourceLocation sloc = getFileLoc(srcmgr, m_node->getLocation());
      const clang::OptionalFileEntryRef declfile = srcmgr.getFileEntryRefForID(srcmgr.getFileID(sloc));
      return declfile->getName().str();
    }

    std::string getName(bool prefixed) const {
      return (prefixed ? (opt::prefix + "_") : "") + m_node->getName().str();
    }

    std::string getTypedefName() const {
      return getName(false) + "_t";
    }

    std::string getReturnTypeTypedefName() const {
      return getName(false) + "_ret_t";
    }

    std::string getReturnType() const {
      clang::QualType rtype = m_node->getDeclaredReturnType();

      if (isAnonymousFunctionPointerType(rtype)) {
        return getReturnTypeTypedefName();
      }
      else {
        return fixTypeName(rtype.getAsString());
      }
    }

    std::string getTypedef(clang::ASTContext &ctx) const {
      std::ostringstream str;
      clang::QualType rtype = m_node->getDeclaredReturnType();
      std::string rtypestr = rtype.getAsString();

      if (isAnonymousFunctionPointerType(rtype)) {
        std::string s;
        llvm::raw_string_ostream sstr(s);
        rtypestr = getReturnTypeTypedefName();
        ctx.buildImplicitTypedef(rtype, rtypestr)->print(sstr, ctx.getPrintingPolicy());
        sstr << ";\n";
        sstr.flush();
        str << s;
      }

      str << "typedef " << getReturnType() << " (*" << getTypedefName() << ")" << getParameters(true, true);

      return str.str();
    }

    std::string getStructMember() const {
      return getTypedefName() + " " + getName(false);
    }

    std::string getParameters(bool types, bool names) const {
      std::ostringstream str;

      str << "(";
      for (int i = 0, max = m_node->getNumParams(); i < max; i++) {
        std::string pstr;
        if (names) {
          clang::IdentifierInfo *ident = m_node->getParamDecl(i)->getIdentifier();
          if (ident == NULL) {
            std::string name = "_p" + std::to_string(i);
            ident = &m_node->getASTContext().Idents.getOwn(name);
            m_node->getParamDecl(i)->setDeclName(clang::DeclarationName(ident));
          }
        }
        if(types && names) {
          llvm::raw_string_ostream sstr(pstr);
          m_node->getParamDecl(i)->print(sstr);
        }
        else if (types) {
          pstr = m_node->getParamDecl(i)->getType().getAsString();
        }
        else if (names) {
          pstr = m_node->getParamDecl(i)->getNameAsString();
        }
        if (types) {
          pstr = fixTypeName(pstr);
        }
        str << (i ? ", " : "") << pstr;
      }
      if (m_node->isVariadic()) {
        str << ", ...";
      }
      str << ")";

      return str.str();
    }

    void writeImplementation(std::ostream &str) const {
      if (!m_node->isVariadic()) {
        str << getReturnType() << " " << getName(true) << getParameters(true, true) << " {" << std::endl;
        str << "  assert(" << opt::prefix << "." << getName(true) << ");" << std::endl;
        str << "  " << ((getReturnType() != "void") ? "return " : "");
        str << opt::prefix << "." << getName(true) << getParameters(false, true) << ";" << std::endl;
        if(m_node->isNoReturn()) {
          str << "  exit(1);" << std::endl;
        }
        str << "}" << std::endl;
      }
    }

  private:

    std::string fixTypeName(std::string type) const {
      static std::regex restackof("struct stack_st_([a-zA-Z0-9_]*)");
      static std::regex relhashof("struct lhash_st_([a-zA-Z0-9_]*)");

      std::smatch smatch;

      if (std::regex_search(type, smatch, restackof)) {
        type = smatch.prefix().str() + "STACK_OF(" + smatch[1].str() + ")" + smatch.suffix().str();
      }
      else if (std::regex_search(type, smatch, relhashof)) {
        type = smatch.prefix().str() + "LHASH_OF(" + smatch[1].str() + ")" + smatch.suffix().str();
      }

      return type;
    }

  private:

    clang::FunctionDecl *m_node;
};


class MyFrontendAction: public clang::ASTFrontendAction {

  public:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef InFile) override;

    bool BeginSourceFileAction(clang::CompilerInstance &compiler) override;
    void EndSourceFileAction() override;

    bool prefixable(const std::string &path) {
      std::filesystem::path p = std::filesystem::proximate(path, opt::incdir() / opt::prefix);
      return (opt::headers.find(p) != opt::headers.end());
    }

    bool prefixable(clang::SourceLocation sloc) {
      const clang::SourceManager &srcmgr = getCompilerInstance().getSourceManager();
      clang::FileID fileid = srcmgr.getFileID(getFileLoc(srcmgr, sloc));
      if(const clang::OptionalFileEntryRef declfile = srcmgr.getFileEntryRefForID(fileid)) {
        return prefixable (declfile->getName().str());
      }

      return false;
    }

    void MacroDefined(clang::Preprocessor &pp, const clang::Token &token, const clang::MacroDirective *directive) {
      if (prefixable(token.getLocation())) {
        std::string name = pp.getSpelling(token);
        m_identifiers.insert(name);
        if (name == "OPENSSL_VERSION_MAJOR") {
          const auto &token = directive->getMacroInfo()->getReplacementToken(0);
          m_version_major = std::string(token.getLiteralData(), token.getLength());
        }
        if (name == "OPENSSL_VERSION_MINOR") {
          const auto &token = directive->getMacroInfo()->getReplacementToken(0);
          m_version_minor = std::string(token.getLiteralData(), token.getLength());
        }
      }
    }

    void MacroUndefined(clang::Preprocessor &pp, const clang::Token &token, const clang::MacroDefinition &definition, const clang::MacroDirective *undef) {
      if (prefixable(token.getLocation())) {
        m_identifiers.insert(pp.getSpelling(token));
      }
    }

    void MacroExpands(clang::Preprocessor &pp, const clang::Token &token, const clang::MacroDefinition &definition, clang::SourceRange srange, const clang::MacroArgs *args) {
      if (prefixable(definition.getMacroInfo()->getDefinitionLoc())) {
        m_identifiers.insert(pp.getSpelling(token));
      }
    }

    bool VisitFunctionDecl(clang::FunctionDecl *node) {
      if ((node->getName().size() > 0) && prefixable(node->getLocation())) {
        m_identifiers.insert(node->getName().str());
        m_functions.push_back(node);
      }

      return true;
    }

    bool VisitRecordDecl(clang::RecordDecl *node) {
      if ((node->getName().size() > 0) && prefixable(node->getLocation())) {
        m_identifiers.insert(node->getName().str());
      }
      return true;
    }

    bool VisitTypedefDecl(clang::TypedefDecl *node) {
      if (prefixable(node->getLocation())) {
        m_identifiers.insert(node->getName().str());
      }
      return true;
    }

    bool VisitEnumDecl(clang::EnumDecl *node) {
      if (prefixable(node->getLocation())) {
        m_identifiers.insert(node->getName().str());
      }
      return true;
    }

    bool VisitEnumConstantDecl(clang::EnumConstantDecl *node) {
      if (prefixable(node->getLocation())) {
        m_identifiers.insert(node->getName().str());
      }
      return true;
    }

  private:

    std::set<std::string> m_identifiers; // To be prefixed
    std::vector<Function> m_functions;
    std::string m_version_major; // Parsed from OPENSSL_VERSION_MAJOR macro
    std::string m_version_minor; // Parsed from OPENSSL_VERSION_MINOR macro
};


class MyASTConsumer: public clang::ASTConsumer, public clang::RecursiveASTVisitor<MyASTConsumer> {
  public:
    explicit MyASTConsumer(MyFrontendAction &frontend) :
        m_frontend(frontend) {
    }

    void HandleTranslationUnit(clang::ASTContext &context) {
      TraverseDecl(context.getTranslationUnitDecl());
    }

    bool VisitFunctionDecl(clang::FunctionDecl *node) {
      return m_frontend.VisitFunctionDecl(node);
    }

    bool VisitRecordDecl(clang::RecordDecl *node) {
      return m_frontend.VisitRecordDecl(node);
    }

    bool VisitTypedefDecl(clang::TypedefDecl *node) {
      return m_frontend.VisitTypedefDecl(node);
    }

    bool VisitEnumDecl(clang::EnumDecl *node) {
      return m_frontend.VisitEnumDecl(node);
    }

    bool VisitEnumConstantDecl(clang::EnumConstantDecl *node) {
      return m_frontend.VisitEnumConstantDecl(node);
    }

  private:

    MyFrontendAction &m_frontend;
};


class MyPPCallbacks: public clang::PPCallbacks {
  public:

    explicit MyPPCallbacks(MyFrontendAction &frontend, clang::Preprocessor &preprocessor) :
        m_frontend(frontend),
        m_preprocessor(preprocessor) {
    }

    void MacroDefined(const clang::Token &token, const clang::MacroDirective *directive) override {
      m_frontend.MacroDefined(m_preprocessor, token, directive);
    }

    void MacroUndefined(const clang::Token &token, const clang::MacroDefinition &definition, const clang::MacroDirective *undef) override {
      m_frontend.MacroUndefined(m_preprocessor, token, definition, undef);
    }

    void MacroExpands(const clang::Token &token, const clang::MacroDefinition &definition, clang::SourceRange srange, const clang::MacroArgs *args) override {
      m_frontend.MacroExpands(m_preprocessor, token, definition, srange, args);
    }

  private:

    MyFrontendAction &m_frontend;
    clang::Preprocessor &m_preprocessor;
};


class CompilationDatabase : public clang::tooling::CompilationDatabase
{
  public:

    std::vector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef file) const override {
      std::vector<std::string> cmdline = {
          "dummy",
          std::string("-I") + opt::incdir().string(),
      };
      for (const auto &inc : opt::includes) {
        cmdline.push_back(std::string("-I") + inc);
      }
      cmdline.push_back(file.str());
      return { clang::tooling::CompileCommand(".", file, cmdline, "") };
    }
};



std::unique_ptr<clang::ASTConsumer> MyFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef InFile) {
  return std::make_unique<MyASTConsumer>(*this);
}

bool MyFrontendAction::BeginSourceFileAction(clang::CompilerInstance &compiler) {
  auto &preprocessor = compiler.getPreprocessor();
  std::unique_ptr<MyPPCallbacks> ppcallbacks(new MyPPCallbacks(*this, preprocessor));
  preprocessor.addPPCallbacks(std::move(ppcallbacks));
  return true;
}

void MyFrontendAction::EndSourceFileAction() {
  // Write a typedef and extern variable for each function pointer
  {
    std::filesystem::create_directories(opt::hfile().parent_path());
    std::ofstream hstr (opt::hfile());  // writes header file
    const auto &srcmgr = getCompilerInstance().getSourceManager();

    hstr << "//" << std::endl << "// THIS FILE IS GENERATED BY THE PREFIXER TOOL DO NOT EDIT" << std::endl << "//" << std::endl;
    hstr << "#ifndef _" << opt::prefix << "_H_\n";
    hstr << "#define _" << opt::prefix << "_H_\n";
    std::map<std::string,std::vector<Function>> funcmap;

    for(const auto &f : m_functions) {
      std::filesystem::path header = f.getHeader(srcmgr);
      header = header.lexically_relative(opt::incdir());
      if(funcmap.find(header) == funcmap.end()) {
        hstr << "#include \"" << header.string() <<"\"" << std::endl;
      }
      funcmap[header].push_back(f);
    }
    hstr << std::endl;

    for(const auto &f : m_functions) {
      if (!f.hasBody()) {
        hstr << f.getTypedef(getCompilerInstance().getASTContext()) << ";" << std::endl;
      }
      m_identifiers.insert(f.getTypedefName()); // Ensure it gets prefixed
    }
    hstr << std::endl;

    hstr << "struct " << opt::prefix + "_functions {" << std::endl;
    for (const auto &function : m_functions) {
      if (!function.hasBody()) {
        hstr << "  " << function.getStructMember() << ";" << std::endl;
      }
    }
    hstr << "};" << std::endl
         << std::endl
         << "extern struct " << opt::prefix << "_functions " << opt::prefix << ";" << std::endl
         << std::endl
         << "#endif" << std::endl;
  }

  {
    std::filesystem::create_directories(opt::cfile().parent_path());
    std::ofstream cstr (opt::cfile());

    cstr << "//" << std::endl << "// THIS FILE IS GENERATED BY THE PREFIXER TOOL DO NOT EDIT" << std::endl << "//" << std::endl
         << "#define _GNU_SOURCE" << std::endl
         << "#include <link.h>" << std::endl
         << "#include <dlfcn.h>" << std::endl
         << "#include <errno.h>" << std::endl
         << "#include <assert.h>" << std::endl
         << "#include \"ossl_dlutil.h\"" << std::endl
         << "#include \"" << opt::prefix << ".h\"" << std::endl
         << std::endl
         << "struct " << opt::prefix << "_functions " << opt::prefix << ";" << std::endl
         << std::endl
         << "static void " << opt::prefix << "_init(void)  __attribute__ ((constructor));" << std::endl
         << "static void " << opt::prefix << "_fini(void)  __attribute__ ((destructor));" << std::endl
         << std::endl
         << "static void " << opt::prefix << "_init(void) {" << std::endl
         << std::endl
         << "  ossl_dlopen(" << m_version_major << ", " << m_version_minor << ");" << std::endl;

    for(const auto &function : m_functions) {
      if (!function.hasBody()) {
        cstr << "  " << opt::prefix << "." << function.getName(true) << " = (" << function.getTypedefName() << ")ossl_dlsym(\"" << function.getName(false) << "\");" << std::endl;
      }
    }

    cstr << std::endl
         << "  ossl.ossl_ERR_load_crypto_strings();" << std::endl
         << "  ossl.ossl_SSL_load_error_strings();" << std::endl
         << "}" << std::endl
         << std::endl
         << "static void " << opt::prefix << "_fini(void) {" << std::endl
         << "  ossl_dlclose();" << std::endl
         << "}" << std::endl
         << std::endl;

    for(const auto &function : m_functions) {
      if (!function.hasBody()) {
        function.writeImplementation(cstr);
      }
    }
  }

  auto files(opt::headers);
  files[opt::hfile()] = false;
  files[opt::cfile()] = false;
  std::regex regex("[a-zA-Z_][a-zA-Z0-9_]*", std::regex::basic | std::regex::optimize);
  opt::vstr() << "Processing " << files.size() << " files...\n";
  for (auto [header, incl] : files) {
    auto path = opt::incdir() / opt::prefix / header;
    std::string buffer;

    opt::vstr() << " - " << path << "\n";

    // Read the source header
    {
      std::ifstream ifstr(path);
      std::stringstream isstr;
      isstr << ifstr.rdbuf();
      buffer = isstr.str();
    }

    // Add the prefix to all identifiers in m_identifiers
    {
      std::ostringstream osstr;
      std::smatch match;
      std::string::const_iterator searchStart = buffer.cbegin();
      std::string suffix;

      while (std::regex_search(searchStart, buffer.cend(), match, regex)) {
        bool matched = false;
        const std::string &matchstr = match[0];

        if ((matched = (m_identifiers.find(matchstr) != m_identifiers.end())) == false) {
          for (std::regex pattern : opt::extraIdentifiers) {
            if ((matched = (std::regex_search(matchstr, pattern)))) {
              break;
            }
          }
        }
        osstr << match.prefix() << (matched ? (opt::prefix + "_") : "") << matchstr;

        searchStart = match.suffix().first;
        suffix = match.suffix();
      }
      osstr << suffix;
      buffer = osstr.str();
    }

    // Do extra prefix insertions, listed in opt::extraInsertions
    if (opt::extraInsertions.find(header) != opt::extraInsertions.end()) {
      for (auto entry : opt::extraInsertions[header]) {
        std::string search = entry.first;
        std::string replace = entry.second.first + opt::prefix + entry.second.second;

        for(auto next = buffer.find(search); next != std::string::npos; next = buffer.find(search, next)) {
          buffer.replace(next, search.length(), replace);
          next += replace.length();
        }
      }
    }

    // Write the file back
    {
      std::ofstream ofstr(path);
      ofstr << buffer;
    }
  }
}




static bool usage(int exitcode) {
  std::cerr << std::endl
            << "USAGE: prefixer [options]" << std::endl
            << std::endl
            << "OPTIONS:" << std::endl
            << std::endl
            << "  --src-path <path>       Directory containing the openssl headers e.g. /usr/include" << std::endl
            << "  --src-incl <pattern>    Header files to be prefixed e.g. openssl/*.h" << std::endl
            << "  --src-skip <pattern>    Header files to be skipped e.g. openssl/asn1_mac.h" << std::endl
            << "  --include <path>        Directory to search for #includes" << std::endl
            << "  --prefix <string>       The prefix to be applied to functions, types & macros" << std::endl
            << "  --output <path>         Output directory for generated files" << std::endl
            << "  --verbose               Print more info about what's being done" << std::endl
            << std::endl
            << "All files will be generated under the output directory as follows:" << std::endl
            << std::endl
            << "  <output>/" << std::endl
            << "  ├── source/" << std::endl
            << "  |   └── <prefix>.c" << std::endl
            << "  └── include/" << std::endl
            << "      └── <prefix>.h" << std::endl
            << "      └── <prefix>/" << std::endl
            << "          └── openssl/" << std::endl
            << "              ├── aes.h" << std::endl
            << "              ├── asn1.h" << std::endl
            << "              ├── ...." << std::endl
            << "              ├── x509v3.h" << std::endl
            << "              └── x509_vfy.h" << std::endl
            << std::endl;

  if (exitcode) {
    exit(exitcode);
  }

  return true;
}


int main(int argc, const char **argv) {

  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if ((arg == "--src-path") && ((++i < argc) || usage(-1))) {
      opt::srcpaths.insert (std::filesystem::canonical(argv[i]));
    }
    else if ((arg == "--src-incl") && ((++i < argc) || usage(-1))) {
      opt::srcincl.insert(argv[i]);
    }
    else if ((arg == "--src-skip") && ((++i < argc) || usage(-1))) {
      opt::srcskip.insert(argv[i]);
    }
    else if ((arg == "--include") && ((++i < argc) || usage(-1))) {
      opt::includes.insert(argv[i]);
    }
    else if ((arg == "--prefix") && ((++i < argc) || usage(-1))) {
      opt::prefix = argv[i];
    }
    else if ((arg == "--output") && ((++i < argc) || usage(-1))) {
      opt::output = std::filesystem::absolute(argv[i]);
    }
    else if (arg == "--verbose") {
      opt::verbose = true;
    }
    else if (arg == "--help") {
      usage(0);
      exit(0);
    }
    else {
      llvm::errs() << "Unrecognised option : " << arg << "\n";
      exit(-1);
    }
  }

  // Build the list of header files to be processed
  for (std::filesystem::path srcpath : opt::srcpaths) {
    if (!std::filesystem::is_directory(srcpath)) {
      llvm::errs() << "Source directory " << srcpath << " does not exist\n";
      return -1;
    }
    else {
      opt::vstr() << "Finding source headers in " << srcpath << "\n";
      {
        glob_t globbuf;
        int globflags = GLOB_MARK;
        for (auto i : opt::srcincl) {
          auto pattern = srcpath / i;
          glob(pattern.c_str(), globflags, 0, &globbuf);
          globflags |= GLOB_APPEND;
        }
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
          auto p = std::filesystem::path(globbuf.gl_pathv[i]).lexically_relative(srcpath);
          opt::headers[p] = true;
        }
        globfree (&globbuf);
      }
      {
        glob_t globbuf = { .gl_pathc = 0 };
        int globflags = GLOB_MARK;
        for (auto i : opt::srcskip) {
          auto pattern = srcpath / i;
          glob(pattern.c_str(), globflags, 0, &globbuf);
          globflags |= GLOB_APPEND;
        }
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
          auto p = std::filesystem::path(globbuf.gl_pathv[i]).lexically_relative(srcpath);
          opt::headers[p] = false;
        }
        globfree (&globbuf);
      }
    }
  }
  if (opt::verbose) {
    for(auto [path, incl] : opt::headers) {
      opt::vstr() << "  " << incl << " " << path << "\n";
    }
  }

  opt::vstr() << "Creating output directory " << opt::incdir() / opt::prefix << "\n";
  std::filesystem::create_directories (opt::incdir() / opt::prefix);
  std::filesystem::path tmpfile = (opt::incdir() / opt::prefix).string() + ".c";
  {
    std::ostringstream subts;
    std::ostringstream files;
    std::ofstream str (tmpfile);
    for (auto [hdr, incl] : opt::headers) {
      auto dsthdr = opt::incdir() / opt::prefix / hdr;

      std::filesystem::create_directories(dsthdr.parent_path());
      for (std::filesystem::path srcpath : opt::srcpaths) {
        std::filesystem::path srchdr = srcpath / hdr;
        if (std::filesystem::is_regular_file(srchdr)) {
          if (std::filesystem::exists(dsthdr)) {
            std::filesystem::remove(dsthdr);
          }
          std::filesystem::copy_file(srcpath / hdr, dsthdr);
          std::filesystem::permissions(dsthdr, std::filesystem::perms::owner_write |
                                               std::filesystem::perms::owner_read);
        }
      }

      subts << " -e 's!<" << hdr << ">!\"" << opt::prefix << "/" << hdr << "\"!g'";
      files << " " << opt::incdir() / opt::prefix << "/" << hdr;

      if (incl) {
        str << "#include \"" << opt::prefix << "/" << hdr << "\"" << std::endl;
      }
    }
    std::system((std::string("sed -i ") + subts.str() + files.str()).c_str());
  }

  clang::tooling::ClangTool tool(CompilationDatabase(), { tmpfile });
  int ret = tool.run(clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());

  std::filesystem::remove(tmpfile);

  return ret;
}

