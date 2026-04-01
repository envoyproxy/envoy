#include <functional>
#include <iostream>

#if defined(WIN32)
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "validate/validate.h"

#include "tests/harness/cases/bool.pb.h"
#include "tests/harness/cases/bool.pb.validate.h"
#include "tests/harness/cases/bytes.pb.h"
#include "tests/harness/cases/bytes.pb.validate.h"
#include "tests/harness/cases/enums.pb.h"
#include "tests/harness/cases/enums.pb.validate.h"
#include "tests/harness/cases/filename-with-dash.pb.h"
#include "tests/harness/cases/filename-with-dash.pb.validate.h"
#include "tests/harness/cases/maps.pb.h"
#include "tests/harness/cases/maps.pb.validate.h"
#include "tests/harness/cases/messages.pb.h"
#include "tests/harness/cases/messages.pb.validate.h"
#include "tests/harness/cases/numbers.pb.h"
#include "tests/harness/cases/numbers.pb.validate.h"
#include "tests/harness/cases/oneofs.pb.h"
#include "tests/harness/cases/oneofs.pb.validate.h"
#include "tests/harness/cases/repeated.pb.h"
#include "tests/harness/cases/repeated.pb.validate.h"
#include "tests/harness/cases/strings.pb.h"
#include "tests/harness/cases/strings.pb.validate.h"
#include "tests/harness/cases/wkt_any.pb.h"
#include "tests/harness/cases/wkt_any.pb.validate.h"
#include "tests/harness/cases/wkt_duration.pb.h"
#include "tests/harness/cases/wkt_duration.pb.validate.h"
#include "tests/harness/cases/wkt_nested.pb.h"
#include "tests/harness/cases/wkt_nested.pb.validate.h"
#include "tests/harness/cases/wkt_timestamp.pb.h"
#include "tests/harness/cases/wkt_timestamp.pb.validate.h"
#include "tests/harness/cases/wkt_wrappers.pb.h"
#include "tests/harness/cases/wkt_wrappers.pb.validate.h"
#include "tests/harness/cases/kitchen_sink.pb.h"
#include "tests/harness/cases/kitchen_sink.pb.validate.h"

#include "tests/harness/harness.pb.h"

// These macros are defined in the various validation headers and call the
// X macro once for each message class in the header. Add macros here with new
// pb.validate.h headers.
#define X_TESTS_HARNESS_CASES(X) \
  X_TESTS_HARNESS_CASES_BOOL(X) \
  X_TESTS_HARNESS_CASES_BYTES(X) \
  X_TESTS_HARNESS_CASES_ENUMS(X) \
  X_TESTS_HARNESS_CASES_FILENAME_WITH_DASH(X) \
  X_TESTS_HARNESS_CASES_MAPS(X) \
  X_TESTS_HARNESS_CASES_MESSAGES(X) \
  X_TESTS_HARNESS_CASES_NUMBERS(X) \
  X_TESTS_HARNESS_CASES_ONEOFS(X) \
  X_TESTS_HARNESS_CASES_REPEATED(X) \
  X_TESTS_HARNESS_CASES_STRINGS(X) \
  X_TESTS_HARNESS_CASES_WKT_ANY(X) \
  X_TESTS_HARNESS_CASES_WKT_DURATION(X) \
  X_TESTS_HARNESS_CASES_WKT_NESTED(X) \
  X_TESTS_HARNESS_CASES_WKT_TIMESTAMP(X) \
  X_TESTS_HARNESS_CASES_WKT_WRAPPERS(X) \
  X_TESTS_HARNESS_CASES_KITCHEN_SINK(X) \

namespace {

using tests::harness::TestCase;
using tests::harness::TestResult;
using google::protobuf::Any;

std::ostream& operator<<(std::ostream& out, const TestResult& result) {
  if (result.reasons_size() > 0) {
    out << "valid: " << result.valid() << " reason: '" << result.reasons(0) << "'"
        << std::endl;
  } else {
    out << "valid: " << result.valid() << " reason: unknown"
        << std::endl;
  }
  return out;
}

void WriteTestResultAndExit(const TestResult& result) {
  if (!result.SerializeToOstream(&std::cout)) {
    std::cerr << "could not martial response: ";
    std::cerr << result << std::endl;
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

void ExitIfFailed(bool succeeded, const pgv::ValidationMsg& err_msg) {
  if (succeeded) {
    return;
  }

  TestResult result;
  result.set_error(true);
  result.add_reasons(pgv::String(err_msg));
  WriteTestResultAndExit(result);
}

std::function<TestResult()> GetValidationCheck(const Any& msg) {
  // This macro is intended to be called once for each message type with the
  // fully-qualified class name passed in as the only argument CLS. It checks
  // whether the msg argument above can be unpacked as a CLS. If so, it returns
  // a lambda that, when called, unpacks the message and validates it as a CLS.
  // This is here to work around the lack of duck-typing in C++, and because the
  // validation function can't be specified as a virtual method on the
  // google::protobuf::Message class.
#define TRY_RETURN_VALIDATE_CALLABLE(CLS) \
  if (msg.Is<CLS>()) { \
    return [msg] () {                                      \
      pgv::ValidationMsg err_msg;                          \
      TestResult result;                                   \
      CLS unpacked;                                        \
      msg.UnpackTo(&unpacked);                             \
      try {                                                \
        result.set_valid(Validate(unpacked, &err_msg));    \
        result.add_reasons(std::move(err_msg));            \
      } catch (pgv::UnimplementedException& e) {           \
        /* don't fail for unimplemented validations */     \
        result.set_valid(false);                           \
        result.set_allowfailure(true);                     \
        result.add_reasons(e.what());                      \
      }                                                    \
      return result;                                       \
    };                                                     \
  }

X_TESTS_HARNESS_CASES(TRY_RETURN_VALIDATE_CALLABLE)
#undef TRY_RETURN_VALIDATE_CALLABLE

  // Special handling for ignored messages, which don't have any code generated
  // for them.
  if (msg.Is<::tests::harness::cases::MessageIgnored>()) {
    return []() {
      TestResult result;
      result.set_valid(true);
      result.set_allowfailure(true);
      result.add_reasons("no validation possible for ignored messages");
      return result;
    };
  }

  // By default, return a null callable to signal that the message cannot be
  // handled.
  return nullptr;
}

}  // namespace

int main() {
  TestCase test_case;

#if defined(WIN32)
  // need to explicitly set the stdin file mode to binary on Windows
  ExitIfFailed(_setmode(_fileno(stdin), _O_BINARY) != -1, "failed to set stdin to binary mode");
#endif

  ExitIfFailed(test_case.ParseFromIstream(&std::cin), "failed to parse TestCase");

  auto validate_fn = GetValidationCheck(test_case.message());
  if (validate_fn == nullptr) {
    std::cerr << "No known validator for message type "
              << test_case.message().type_url()
              << "; did you add it to the harness?";
    return 1;
  }

  WriteTestResultAndExit(validate_fn());

  return 0;
}
