#include "bazel/cc_proto_descriptor_library/file_descriptor_generator.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "google/protobuf/compiler/retention.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"

// NOLINT(namespace-envoy)
namespace cc_proto_descriptor_library {

namespace {

// The longest string literal to emit. MSVC rejects longer literals ("error C1091: compiler limit:
// string exceeds 65535 bytes in length"), so larger descriptors are emitted as a char array
// instead. This mirrors how protoc's C++ generator embeds descriptors.
constexpr size_t kMaxStringLiteralBytes = 65535;
// The number of descriptor bytes to emit per line of generated code.
constexpr size_t kBytesPerLine = 40;

// Returns `bytes` escaped for use inside a C++ string literal. '?' is escaped too so that the
// literal can never contain a trigraph such as "??/".
std::string escapeForStringLiteral(absl::string_view bytes) {
  return absl::StrReplaceAll(absl::CEscape(bytes), {{"?", "\\?"}});
}

// Writes any definitions needed to embed `bytes` (which may contain any byte value, including
// NUL) to `contents`, and returns a braced initializer for an `absl::string_view` that refers to
// them.
std::string emitStringViewInitializer(absl::string_view bytes, std::ostream& contents) {
  if (bytes.size() <= kMaxStringLiteralBytes) {
    // Emit a string literal split across lines. absl::CEscape always emits three-digit octal
    // escapes, so an escape sequence never absorbs the digit that follows it and the literal can
    // be split after any byte.
    std::string literal;
    for (size_t i = 0; i < bytes.size(); i += kBytesPerLine) {
      absl::StrAppend(&literal, "\n    \"", escapeForStringLiteral(bytes.substr(i, kBytesPerLine)),
                      "\"");
    }
    if (literal.empty()) {
      literal = "\"\"";
    }
    return absl::StrCat("{", literal, ",\n    ", bytes.size(), "}");
  }
  contents << "static constexpr char kFileDescriptorBytes[] = {";
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i % kBytesPerLine == 0) {
      contents << "\n    ";
    }
    contents << "'" << absl::CEscape(bytes.substr(i, 1)) << "',";
  }
  contents << "\n};\n";
  return "{kFileDescriptorBytes, sizeof(kFileDescriptorBytes)}";
}

absl::string_view getFileBaseName(const google::protobuf::FileDescriptor* file) {
  absl::string_view stripped_name = file->name();
  if (absl::ConsumeSuffix(&stripped_name, ".proto")) {
    return stripped_name;
  } else if (absl::ConsumeSuffix(&stripped_name, ".protodevel")) {
    return stripped_name;
  }
  return stripped_name;
}

std::string getDescriptorHeaderName(const google::protobuf::FileDescriptor* file) {
  return absl::StrCat(getFileBaseName(file), "_descriptor.pb.h");
}

std::string getDescriptorSourceName(const google::protobuf::FileDescriptor* file) {
  return absl::StrCat(getFileBaseName(file), "_descriptor.pb.cc");
}

std::string getDescriptorNamespace(const google::protobuf::FileDescriptor* file) {
  return absl::AsciiStrToLower(
      absl::StrReplaceAll(getFileBaseName(file), {{"/", "_"}, {"-", "_"}}));
}

std::string getDependencyFileDescriptorInfoSymbol(const google::protobuf::FileDescriptor* file) {
  return absl::StrFormat("%s::kFileDescriptorInfo", getDescriptorNamespace(file));
}

std::string getDependencyFileDescriptorHeaderGuard(const google::protobuf::FileDescriptor* file) {
  std::string header_path = getDescriptorHeaderName(file);
  return absl::AsciiStrToUpper(
      absl::StrReplaceAll(header_path, {{"/", "_"}, {".", "_"}, {"-", "_"}}));
}

bool generateHeader(const google::protobuf::FileDescriptor* file,
                    google::protobuf::io::ZeroCopyOutputStream* output_stream) {
  auto header_guard = getDependencyFileDescriptorHeaderGuard(file);
  auto unique_namespace = getDescriptorNamespace(file);

  std::stringstream contents;
  contents << absl::StrFormat(R"text(
#ifndef %s
#define %s

#include "absl/base/attributes.h"

namespace cc_proto_descriptor_library {
namespace internal {

struct FileDescriptorInfo;

} // namespace internal
} // namespace cc_proto_descriptor_library

namespace protobuf {
namespace reflection {
namespace %s {

extern const ::cc_proto_descriptor_library::internal::FileDescriptorInfo kFileDescriptorInfo;
} // namespace %s
} // namespace reflection
} // namespace protobuf

#endif // %s

)text",
                              header_guard, header_guard, unique_namespace, unique_namespace,
                              header_guard);

  google::protobuf::io::CodedOutputStream output(output_stream);
  output.WriteString(contents.str());
  output.Trim();
  return !output.HadError();
}

bool generateSource(const google::protobuf::FileDescriptor* file,
                    google::protobuf::io::ZeroCopyOutputStream* output_stream) {
  auto unique_namespace = getDescriptorNamespace(file);
  std::stringstream contents;

  contents << absl::StrFormat("#include \"%s\"\n", getDescriptorHeaderName(file));
  for (int i = 0; i < file->dependency_count(); ++i) {
    contents << absl::StrFormat("#include \"%s\"\n", getDescriptorHeaderName(file->dependency(i)));
  }
  contents << R"text(#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
)text";

  google::protobuf::FileDescriptorProto file_descriptor_proto =
      google::protobuf::compiler::StripSourceRetentionOptions(*file);

  contents << absl::StrFormat(
      R"text(
namespace protobuf {
namespace reflection {
namespace %s {

)text",
      unique_namespace);

  contents << "static const"
              "::cc_proto_descriptor_library::internal::"
              "FileDescriptorInfo* kDeps[] = {\n";
  for (int i = 0; i < file->dependency_count(); ++i) {
    contents << absl::StrFormat("&%s,\n",
                                getDependencyFileDescriptorInfoSymbol(file->dependency(i)));
  }
  contents << "nullptr};\n";

  // Embed the serialized FileDescriptorProto as raw bytes. (Base64 would add a third to the size,
  // compress worse and need decoding at run time.)
  const std::string bytes_initializer =
      emitStringViewInitializer(file_descriptor_proto.SerializeAsString(), contents);
  contents << absl::StrFormat(
      R"text(

const ::cc_proto_descriptor_library::internal::FileDescriptorInfo kFileDescriptorInfo{
    "%s",
    %s,
    kDeps
  };

} // namespace %s
} // namespace reflection
} // namespace protobuf

)text",
      file->name(), bytes_initializer, unique_namespace);

  google::protobuf::io::CodedOutputStream output(output_stream);
  output.WriteString(contents.str());
  output.Trim();
  return !output.HadError();
}
} // namespace

bool ProtoDescriptorGenerator::Generate( // NOLINT(readability-identifier-naming)
    const google::protobuf::FileDescriptor* file, const std::string& parameter,
    google::protobuf::compiler::GeneratorContext* generator_context, std::string* error) const {
  std::string header_path = getDescriptorHeaderName(file);
  std::string source_path = getDescriptorSourceName(file);

  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> header_output(
      generator_context->Open(header_path));
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> source_output(
      generator_context->Open(source_path));

  if (!generateHeader(file, header_output.get())) {
    return false;
  }
  if (!generateSource(file, source_output.get())) {
    return false;
  }

  return true;
}

} // namespace cc_proto_descriptor_library
