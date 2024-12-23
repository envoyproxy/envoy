#include <string>

#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/linux_container_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {


LinuxContainerStatsReader::LinuxContainerStatsReader(const std::string& linux_cgroup_cpu_allocated_file, const std::string& linux_cgroup_cpu_times_file ) 
: linux_cgroup_cpu_allocated_file_(linux_cgroup_cpu_allocated_file), linux_cgroup_cpu_times_file_(linux_cgroup_cpu_times_file)  {}


EnvoyContainerStats LinuxContainerStatsReader::getEnvoyContainerStats() {
    std::ifstream cpu_allocated_file, cpu_times_file;
    int64_t cpu_allocated_value, cpu_times_value;
    bool stats_valid = true;
    cpu_allocated_file.open(linux_cgroup_cpu_allocated_file_);
    if (!cpu_allocated_file.is_open()) {
        ENVOY_LOG_MISC(error, "Can't open linux cpu allocated file {}", linux_cgroup_cpu_allocated_file_);
        stats_valid = false;
        cpu_allocated_value = 0;
    }else{
        cpu_allocated_file >> cpu_allocated_value;
        if (!cpu_allocated_file) {
            ENVOY_LOG_MISC(error, "Unexpected format in linux cpu allocated file {}", linux_cgroup_cpu_allocated_file_);
            stats_valid = false;
            cpu_allocated_value = 0;
        }
    }

    cpu_times_file.open(linux_cgroup_cpu_times_file_);
    if (!cpu_times_file.is_open()) {
        ENVOY_LOG_MISC(error, "Can't open linux cpu usage seconds file {}", linux_cgroup_cpu_times_file_);
        stats_valid = false;
        cpu_times_value = 0;
    }else{
        cpu_times_file >> cpu_times_value;
        if(!cpu_times_file) {
            ENVOY_LOG_MISC(error, "Unexpected format in linux cpu usage seconds file {}", linux_cgroup_cpu_times_file_);
            stats_valid = false;
            cpu_times_value = 0;
        }
    }

    return {stats_valid,cpu_allocated_value, cpu_times_value,std::chrono::system_clock::now()};
}

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy