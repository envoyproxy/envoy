#include "options_impl.h"

#include "common/common/version.h"

#include "tclap/CmdLine.h"

OptionsImpl::OptionsImpl(int argc, char** argv, const std::string& hot_restart_version,
                         spdlog::level::level_enum default_log_level) {
  TCLAP::CmdLine cmd("envoy", ' ', VersionInfo::version());
  TCLAP::ValueArg<uint64_t> base_id(
      "", "base-id", "base ID so that multiple envoys can run on the same host if needed", false, 0,
      "uint64_t", cmd);
  TCLAP::ValueArg<uint32_t> concurrency("", "concurrency", "# of worker threads to run", false,
                                        std::thread::hardware_concurrency(), "uint32_t", cmd);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file", false,
                                           "", "string", cmd);
  TCLAP::ValueArg<uint64_t> log_level("l", "log-level", "Log level", false, default_log_level,
                                      "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> restart_epoch("", "restart-epoch", "hot restart epoch #", false, 0,
                                          "uint64_t", cmd);
  TCLAP::SwitchArg hot_restart_version_option("", "hot-restart-version",
                                              "hot restart compatability version", cmd);
  TCLAP::ValueArg<std::string> service_cluster("", "service-cluster", "Cluster name", false, "",
                                               "string", cmd);
  TCLAP::ValueArg<std::string> service_node("", "service-node", "Node name", false, "", "string",
                                            cmd);
  TCLAP::ValueArg<std::string> service_zone("", "service-zone", "Zone name", false, "", "string",
                                            cmd);
  TCLAP::ValueArg<uint64_t> flush_interval_msec(
      "", "flush-interval-msec", "Interval for log flushing in msec", false, 10000, "uint64_t", cmd);

  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    std::cerr << "error: " << e.error() << std::endl;
    exit(1);
  }

  if (hot_restart_version_option.getValue()) {
    std::cerr << hot_restart_version;
    exit(0);
  }

  // For base ID, scale what the user inputs by 10 so that we have spread for domain sockets.
  base_id_ = base_id.getValue() * 10;
  concurrency_ = concurrency.getValue();
  config_path_ = config_path.getValue();
  log_level_ = log_level.getValue();
  restart_epoch_ = restart_epoch.getValue();
  service_cluster_ = service_cluster.getValue();
  service_node_ = service_node.getValue();
  service_zone_ = service_zone.getValue();
  flush_interval_msec_ = std::chrono::milliseconds(flush_interval_msec.getValue());
}
