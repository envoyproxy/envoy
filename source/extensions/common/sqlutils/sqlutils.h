#include "common/protobuf/utility.h"

#include "include/sqlparser/SQLParser.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SQLUtils {

class SQLutils {
public:
  static bool setMetadata(const std::string& query, ProtobufWkt::Struct& metadata);
};

} // namespace SQLUtils
} // namespace Common
} // namespace Extensions
} // namespace Envoy
