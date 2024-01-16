#include "source/server/admin/runtime_handler.h"

#include <string>
#include <vector>

#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/utils.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Server {

RuntimeHandler::RuntimeHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code RuntimeHandler::handlerRuntime(Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream& admin_stream) {
  const Http::Utility::QueryParamsMulti params = admin_stream.queryParams();
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  // TODO(jsedgwick): Use proto to structure this output instead of arbitrary JSON.
  const auto& layers = server_.runtime().snapshot().getLayers();

  std::vector<ProtobufWkt::Value> layer_names;
  layer_names.reserve(layers.size());
  std::map<std::string, std::vector<std::string>> entries;
  for (const auto& layer : layers) {
    layer_names.push_back(ValueUtil::stringValue(layer->name()));
    for (const auto& value : layer->values()) {
      const auto found = entries.find(value.first);
      if (found == entries.end()) {
        entries.emplace(value.first, std::vector<std::string>{});
      }
    }
  }

  for (const auto& layer : layers) {
    for (auto& entry : entries) {
      const auto found = layer->values().find(entry.first);
      const auto& entry_value =
          found == layer->values().end() ? EMPTY_STRING : found->second.raw_string_value_;
      entry.second.push_back(entry_value);
    }
  }

  ProtobufWkt::Struct layer_entries;
  auto* layer_entry_fields = layer_entries.mutable_fields();
  for (const auto& entry : entries) {
    std::vector<ProtobufWkt::Value> layer_entry_values;
    layer_entry_values.reserve(entry.second.size());
    std::string final_value;
    for (const auto& value : entry.second) {
      if (!value.empty()) {
        final_value = value;
      }
      layer_entry_values.push_back(ValueUtil::stringValue(value));
    }

    ProtobufWkt::Struct layer_entry_value;
    auto* layer_entry_value_fields = layer_entry_value.mutable_fields();

    (*layer_entry_value_fields)["final_value"] = ValueUtil::stringValue(final_value);
    (*layer_entry_value_fields)["layer_values"] = ValueUtil::listValue(layer_entry_values);
    (*layer_entry_fields)[entry.first] = ValueUtil::structValue(layer_entry_value);
  }

  ProtobufWkt::Struct runtime;
  auto* fields = runtime.mutable_fields();

  (*fields)["layers"] = ValueUtil::listValue(layer_names);
  (*fields)["entries"] = ValueUtil::structValue(layer_entries);

  response.add(MessageUtil::getJsonStringFromMessageOrError(runtime, true, true));
  return Http::Code::OK;
}

Http::Code RuntimeHandler::handlerRuntimeModify(Http::ResponseHeaderMap&,
                                                Buffer::Instance& response,
                                                AdminStream& admin_stream) {
  Http::Utility::QueryParamsMulti params = admin_stream.queryParams();
  if (params.data().empty()) {
    response.add("usage: /runtime_modify?key1=value1&key2=value2&keyN=valueN\n");
    response.add("       or send the parameters as form values\n");
    response.add("use an empty value to remove a previously added override");
    return Http::Code::BadRequest;
  }
  absl::node_hash_map<std::string, std::string> overrides;
  for (const auto& it : params.data()) {
    overrides.insert({it.first, it.second[0]});
  }
  TRY_ASSERT_MAIN_THREAD { server_.runtime().mergeValues(overrides); }
  END_TRY
  catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(error, "{}", e.what());
    response.add(e.what());
    return Http::Code::ServiceUnavailable;
  }
  response.add("OK\n");
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
