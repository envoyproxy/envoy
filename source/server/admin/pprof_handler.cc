#include "source/server/admin/pprof_handler.h"

#include <string>

#include "source/common/common/assert.h"

#include "absl/debugging/symbolize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#endif
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"
#include "proto/profile.pb.h"
#include "zlib.h"

namespace Envoy {
namespace Server {

using Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl;

namespace {

#if defined(TCMALLOC)
constexpr bool kHasTcMalloc = true;
#else
constexpr bool kHasTcMalloc = false;
#endif

constexpr int kDefaultMemLevel = 5;
constexpr int kDefaultWindowBits = 12;

} // namespace

PprofHandler::PprofHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code PprofHandler::handlerHeapProfile(absl::string_view,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) {
  if (!kHasTcMalloc) {
    return Envoy::Http::Code::NotImplemented;
  }

  perftools::profiles::Profile profile;

  absl::flat_hash_set<void*> location_by_pointer;
  absl::flat_hash_map<std::string, uint64_t> index_by_string;
  absl::flat_hash_map<std::string, uint64_t> id_by_function;

  auto get_string_index = [&index_by_string, &profile](absl::string_view str) {
    auto it = index_by_string.find(str);
    if (it == index_by_string.end()) {
      it = index_by_string.emplace(str, profile.string_table_size()).first;
      profile.add_string_table(std::string(str));
    }
    return it->second;
  };

  auto get_function_id = [&id_by_function, &profile, &get_string_index](absl::string_view name) {
    auto it = id_by_function.find(name);
    if (it == id_by_function.end()) {
      auto id = id_by_function.size() + 1;
      it = id_by_function.emplace(name, id).first;
      auto* function = profile.add_function();
      function->set_id(id);
      function->set_name(get_string_index(name));
    }
    return it->second;
  };

  ABSL_ATTRIBUTE_UNUSED auto get_location_id = [&location_by_pointer, &profile,
                                                get_function_id](void* pointer) {
    uint64_t address = reinterpret_cast<uint64_t>(pointer);
    if (!location_by_pointer.contains(pointer)) {
      location_by_pointer.insert(pointer);
      auto* location = profile.add_location();
      location->set_id(address);
      location->set_address(address);

      char tmp_buffer[1024];
      if (absl::Symbolize(pointer, tmp_buffer, sizeof(tmp_buffer))) {
        auto* line = location->add_line();
        line->set_function_id(get_function_id(tmp_buffer));
      }
    }
    return address;
  };

  auto set_value_type = [&get_string_index](perftools::profiles::ValueType* value_type,
                                            absl::string_view type, absl::string_view unit) {
    value_type->set_type(get_string_index(type));
    value_type->set_unit(get_string_index(unit));
  };

  // The first string must be empty.
  get_string_index("");

  set_value_type(profile.mutable_period_type(), "space", "bytes");
  profile.set_default_sample_type(get_string_index(""));
  set_value_type(profile.add_sample_type(), "inuse_objects", "count");
  set_value_type(profile.add_sample_type(), "inuse_space", "bytes");

#if defined(TCMALLOC)
  auto snapshot = tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
  profile.set_period(snapshot.Period());
  snapshot.Iterate([&](const tcmalloc::Profile::Sample& sample) {
    auto* pb_sample = profile.add_sample();
    pb_sample->add_value(sample.count);
    pb_sample->add_value(sample.sum);
    for (int i = 0; i < sample.depth; ++i) {
      pb_sample->add_location_id(get_location_id(sample.stack[i]));
    }
  });
#endif

  ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard, ZlibCompressorImpl::CompressionStrategy::Standard, kDefaultMemLevel, kDefaultWindowBits);
  response.add(profile.SerializeAsString());
  compressor.compress(response, Compression::Compressor::State::Finish);
  response_headers.setContentType("application/octet-stream");
  return Envoy::Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
