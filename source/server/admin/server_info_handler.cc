#include "source/server/admin/server_info_handler.h"

#include "envoy/admin/v3/memory.pb.h"

#include "source/common/memory/stats.h"
#include "source/common/version/version.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

ServerInfoHandler::ServerInfoHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ServerInfoHandler::handlerCerts(Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&) {
  // This set is used to track distinct certificates. We may have multiple listeners, upstreams, etc
  // using the same cert.
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  envoy::admin::v3::Certificates certificates;
  server_.sslContextManager().iterateContexts([&](const Ssl::Context& context) -> void {
    envoy::admin::v3::Certificate& certificate = *certificates.add_certificates();
    if (context.getCaCertInformation() != nullptr) {
      envoy::admin::v3::CertificateDetails* ca_certificate = certificate.add_ca_cert();
      *ca_certificate = *context.getCaCertInformation();
    }
    for (const auto& cert_details : context.getCertChainInformation()) {
      envoy::admin::v3::CertificateDetails* cert_chain = certificate.add_cert_chain();
      *cert_chain = *cert_details;
    }
  });
  response.add(MessageUtil::getJsonStringFromMessageOrError(certificates, true, true));
  return Http::Code::OK;
}

Http::Code ServerInfoHandler::handlerHotRestartVersion(Http::ResponseHeaderMap&,
                                                       Buffer::Instance& response, AdminStream&) {
  response.add(server_.hotRestart().version());
  return Http::Code::OK;
}

// TODO(ambuc): Add more tcmalloc stats, export proto details based on allocator.
Http::Code ServerInfoHandler::handlerMemory(Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  envoy::admin::v3::Memory memory;
  memory.set_allocated(Memory::Stats::totalCurrentlyAllocated());
  memory.set_heap_size(Memory::Stats::totalCurrentlyReserved());
  memory.set_total_thread_cache(Memory::Stats::totalThreadCacheBytes());
  memory.set_pageheap_unmapped(Memory::Stats::totalPageHeapUnmapped());
  memory.set_pageheap_free(Memory::Stats::totalPageHeapFree());
  memory.set_total_physical_bytes(Memory::Stats::totalPhysicalBytes());
  response.add(MessageUtil::getJsonStringFromMessageOrError(memory, true, true)); // pretty-print
  return Http::Code::OK;
}

Http::Code ServerInfoHandler::handlerReady(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                           AdminStream&) {
  const envoy::admin::v3::ServerInfo::State state =
      Utility::serverState(server_.initManager().state(), server_.healthCheckFailed());

  response.add(envoy::admin::v3::ServerInfo::State_Name(state) + "\n");
  Http::Code code =
      state == envoy::admin::v3::ServerInfo::LIVE ? Http::Code::OK : Http::Code::ServiceUnavailable;
  return code;
}

Http::Code ServerInfoHandler::handlerServerInfo(Http::ResponseHeaderMap& headers,
                                                Buffer::Instance& response, AdminStream&) {
  const std::time_t current_time =
      std::chrono::system_clock::to_time_t(server_.timeSource().systemTime());
  const std::time_t uptime_current_epoch = current_time - server_.startTimeCurrentEpoch();
  const std::time_t uptime_all_epochs = current_time - server_.startTimeFirstEpoch();

  ASSERT(uptime_current_epoch >= 0);
  ASSERT(uptime_all_epochs >= 0);

  envoy::admin::v3::ServerInfo server_info;
  server_info.set_version(VersionInfo::version());
  server_info.set_hot_restart_version(server_.hotRestart().version());
  server_info.set_state(
      Utility::serverState(server_.initManager().state(), server_.healthCheckFailed()));

  server_info.mutable_uptime_current_epoch()->set_seconds(uptime_current_epoch);
  server_info.mutable_uptime_all_epochs()->set_seconds(uptime_all_epochs);
  envoy::admin::v3::CommandLineOptions* command_line_options =
      server_info.mutable_command_line_options();
  *command_line_options = *server_.options().toCommandLineOptions();
  server_info.mutable_node()->MergeFrom(server_.localInfo().node());
  response.add(MessageUtil::getJsonStringFromMessageOrError(server_info, true, true));
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
