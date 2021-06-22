#include "source/server/admin/pprof_handler.h"

#include <string>

#include "source/common/common/assert.h"
#include "source/server/admin/profile.pb.h"

#include "absl/debugging/symbolize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#endif
#include "zlib.h"

namespace Envoy {
namespace Server {

namespace {

#if defined(TCMALLOC)
constexpr bool kHasTcMalloc = true;
#else
constexpr bool kHasTcMalloc = false;
#endif

constexpr size_t kChunkSize = 16 * 1024;
constexpr int kGzipEncoding = 16;
constexpr int kDefaultMemLevel = 8;

class ZStream {
public:
  explicit ZStream(std::function<void(z_stream*)> destructor) : destructor_(std::move(destructor)) {
    ResetOutput();
  }
  ~ZStream() { destructor_(&stream_); }

  z_stream* Get() { return &stream_; }
  z_stream* operator->() { return Get(); }

  absl::StatusOr<std::string> process(const std::string& data,
                                      std::function<int(z_stream*)> process,
                                      absl::string_view process_name) {
    stream_.avail_in = data.size();
    stream_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.c_str()));

    for (;;) {
      int ret = process(&stream_);
      // Z_STREAM_ERROR indicates that z_stream is corrupted. Should never happen.
      RELEASE_ASSERT(ret != Z_STREAM_ERROR, "zstream is corrupted");

      UpdateOutput();
      if (ret == Z_STREAM_END) {
        // Double check that all input was indeed consumed.
        RELEASE_ASSERT(stream_.avail_in == 0, "some input was not consumed");
        break;
      }

      // Z_OK means that some progress was made, otherwise it is an error as all available input has
      // been already provided to zlib.
      if (ret != Z_OK) {
        return invalidArgumentError(absl::StrFormat("%s() failed", process_name));
      }
    }

    return std::move(output_);
  }

  absl::Status invalidArgumentError(absl::string_view prefix) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s: %s", prefix, stream_.msg ? stream_.msg : "unknown"));
  }

private:
  void UpdateOutput() {
    const size_t out_size = sizeof(buffer_) - stream_.avail_out;
    output_.append(reinterpret_cast<char*>(&buffer_), reinterpret_cast<char*>(buffer_ + out_size));
    ResetOutput();
  }

  void ResetOutput() {
    stream_.next_out = buffer_;
    stream_.avail_out = sizeof(buffer_);
  }

  Bytef buffer_[kChunkSize];
  z_stream stream_{};
  std::function<void(z_stream*)> destructor_;
  std::string output_;
};

absl::StatusOr<std::string> compress(const std::string& data) {
  ZStream stream([](z_stream* stream) {
    int ret = deflateEnd(stream);
    // deflateEnd fails iff stream is invalid, which is impossible.
    RELEASE_ASSERT(ret == Z_OK, "deflateEnd() failed");
  });
  int ret = deflateInit2(stream.Get(), Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS | kGzipEncoding,
                         kDefaultMemLevel, Z_DEFAULT_STRATEGY);
  // deflateInit2 fails iff input arguments are invalid or process is out of memory.
  // In either case we can not fix it (arguments are constants and handling out of memory is too
  // hard problem to solve).
  RELEASE_ASSERT(ret == Z_OK, "deflateInit2() failed");

  return stream.process(
      data, [](z_stream* stream) { return deflate(stream, Z_FINISH); }, "deflate");
}

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

  auto compressed = compress(profile.SerializeAsString());
  if (!compressed.ok()) {
    response.add(compressed.status().ToString());
    return Envoy::Http::Code::InternalServerError;
  }

  response_headers.setContentType("application/octet-stream");
  response.add(*compressed);
  return Envoy::Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
