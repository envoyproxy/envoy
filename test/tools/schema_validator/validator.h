#include <string>

#include "common/json/json_loader.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
class Options {
public:
  Options(int argc, char** argv);

  const std::string& schemaType() { return schema_type_; }
  const std::string& configPath() { return json_path_; }

private:
  std::string schema_type_;
  std::string json_path_;
};

class Validator {
public:
  static void validate(Options options);
};
}