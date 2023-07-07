#include "bazel/cc_proto_descriptor_library/file_descriptor_generator.h"

#include <memory>
#include <sstream>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"

namespace cc_proto_descriptor_library {

namespace {

absl::string_view GetFileBaseName(const google::protobuf::FileDescriptor* file) {
  absl::string_view stripped_name = file->name();
  if (absl::ConsumeSuffix(&stripped_name, ".proto")) {
    return stripped_name;
  } else if (absl::ConsumeSuffix(&stripped_name, ".protodevel")) {
    return stripped_name;
  }
  return stripped_name;
}

std::string GetDescriptorHeaderName(const google::protobuf::FileDescriptor* file) {
  return absl::StrCat(GetFileBaseName(file), "_descriptor.pb.h");
}

std::string GetDescriptorSourceName(const google::protobuf::FileDescriptor* file) {
  return absl::StrCat(GetFileBaseName(file), "_descriptor.pb.cc");
}

std::string GetDescriptorNamespace(const google::protobuf::FileDescriptor* file) {
  return absl::AsciiStrToLower(
      absl::StrReplaceAll(GetFileBaseName(file), {{"/", "_"}, {"-", "_"}}));
}

std::string GetDependencyFileDescriptorInfoSymbol(const google::protobuf::FileDescriptor* file) {
  return absl::StrFormat("%s::kFileDescriptorInfo", GetDescriptorNamespace(file));
}

std::string GetDependencyFileDescriptorHeaderGuard(const google::protobuf::FileDescriptor* file) {
  std::string header_path = GetDescriptorHeaderName(file);
  return absl::AsciiStrToUpper(
      absl::StrReplaceAll(header_path, {{"/", "_"}, {".", "_"}, {"-", "_"}}));
}

bool GenerateHeader(const google::protobuf::FileDescriptor* file,
                    google::protobuf::io::ZeroCopyOutputStream* output_stream) {
  auto header_guard = GetDependencyFileDescriptorHeaderGuard(file);
  auto unique_namespace = GetDescriptorNamespace(file);

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

extern const ::cc_proto_descriptor_library::internal::FileDescriptorInfo kFileDescriptorInfo ABSL_CONST_INIT;
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

bool GenerateSource(const google::protobuf::FileDescriptor* file,
                    google::protobuf::io::ZeroCopyOutputStream* output_stream) {
  auto unique_namespace = GetDescriptorNamespace(file);
  std::stringstream contents;

  contents << absl::StrFormat("#include \"%s\"\n", GetDescriptorHeaderName(file));
  for (int i = 0; i < file->dependency_count(); ++i) {
    contents << absl::StrFormat("#include \"%s\"\n", GetDescriptorHeaderName(file->dependency(i)));
  }
  contents << R"text(#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
)text";

  google::protobuf::FileDescriptorProto file_descriptor_proto;
  file->CopyTo(&file_descriptor_proto);
  // XXX
  /*
    google::protobuf::FileDescriptorProto file_descriptor_proto =
      google::protobuf::compiler::StripSourceRetentionOptions(*file);
  */

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
                                GetDependencyFileDescriptorInfoSymbol(file->dependency(i)));
  }
  contents << "nullptr};\n";

  contents << absl::StrFormat(
      R"text(

const ::cc_proto_descriptor_library::internal::FileDescriptorInfo kFileDescriptorInfo{
    "%s",
    "%s",
    kDeps
  };

} // namespace %s
} // namespace reflection
} // namespace protobuf

)text",
      file->name(), absl::Base64Escape(file_descriptor_proto.SerializeAsString()),
      unique_namespace);

  google::protobuf::io::CodedOutputStream output(output_stream);
  output.WriteString(contents.str());
  output.Trim();
  return !output.HadError();
}
} // namespace

bool ProtoDescriptorGenerator::Generate(
    const google::protobuf::FileDescriptor* file, const std::string& parameter,
    google::protobuf::compiler::GeneratorContext* generator_context, std::string* error) const {
  std::string header_path = GetDescriptorHeaderName(file);
  std::string source_path = GetDescriptorSourceName(file);

  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> header_output(
      generator_context->Open(header_path));
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> source_output(
      generator_context->Open(source_path));

  if (!GenerateHeader(file, header_output.get())) {
    return false;
  }
  if (!GenerateSource(file, source_output.get())) {
    return false;
  }

  return true;
}

} // namespace cc_proto_descriptor_library
