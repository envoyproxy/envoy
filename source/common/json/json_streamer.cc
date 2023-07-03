#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Json {

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer),
      closer_(closer),
      is_first_(true) {
  streamer_.addLiteralNoCopy(opener);
}

Streamer::Level::~Level() {
  streamer_.addLiteralNoCopy(closer_);
}

void Streamer::Level::newEntry() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addLiteralNoCopy(",");
  }
}

void Streamer::addLiteralCopy(absl::string_view str) {
  addLiteralNoCopy(str);
  flush();
}

void Streamer::flush() {
  response_.addFragments(fragments_);
  fragments_.clear();
}

void Streamer::addSanitized(absl::string_view token) {
  addLiteralCopy(Json::sanitize(buffer_, token));
}

} // namespace Json
} // namespace Envoy
