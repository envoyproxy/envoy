#include <cstdio>
#include <fstream>

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

#ifdef WIN32
class ScmTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ScmTest()
      : config_file_(TestEnvironment::temporaryFileSubstitute(
            "test/config/integration/google_com_proxy_port_0.yaml", TestEnvironment::ParamMap(),
            TestEnvironment::PortMap(), GetParam())),
        envoy_exe_(TestEnvironment::runfilesPath("source/exe/envoy-static.exe")) {
    scm_manager_handle_ = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    EXPECT_TRUE(scm_manager_handle_ != NULL)
        << fmt::format("Failed to OpenSCManagerA, error {}", ::GetLastError());
    service_handle_ =
        CreateServiceA(scm_manager_handle_, "envoyproxy", "envoyproxy", SERVICE_ALL_ACCESS,
                       SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                       envoy_exe_.c_str(), NULL, NULL, NULL, NULL, NULL);
    if (service_handle_ == NULL) {
      auto last_error = ::GetLastError();
      if (last_error == ERROR_SERVICE_EXISTS) {
        service_handle_ = OpenServiceA(scm_manager_handle_, "envoyproxy", SERVICE_ALL_ACCESS);
      }
    }
    EXPECT_TRUE(service_handle_ != NULL);
  }

  ~ScmTest() {
    stopService();
    CloseServiceHandle(service_handle_);
    CloseServiceHandle(scm_manager_handle_);
  }

  void changeServicePath(const std::string& new_binpath) {
    EXPECT_TRUE(ChangeServiceConfigA(service_handle_, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                                     SERVICE_NO_CHANGE, new_binpath.c_str(), NULL, NULL, NULL, NULL,
                                     NULL, NULL))
        << fmt::format("Failed to change service configuration, error {}", ::GetLastError());
  }

  DWORD getServiceStatus() {
    SERVICE_STATUS_PROCESS service_status;
    DWORD bytes_needed;
    EXPECT_TRUE(QueryServiceStatusEx(service_handle_, SC_STATUS_PROCESS_INFO,
                                     (LPBYTE)&service_status, sizeof(SERVICE_STATUS_PROCESS),
                                     &bytes_needed))
        << fmt::format("Could not query service status, error {}", ::GetLastError());
    return service_status.dwCurrentState;
  }

  void waitForServiceStatus(DWORD expected_status) {
    size_t total_sleep = 1;
    constexpr size_t resonable_sleep_time = 5;
    DWORD status;
    do {
      Sleep(total_sleep * 1000);
      ++total_sleep;
      status = getServiceStatus();
    } while (status != expected_status || total_sleep > resonable_sleep_time);
    EXPECT_EQ(expected_status, status);
  }

  void stopService() {
    DWORD status_code = getServiceStatus();
    if (status_code == SERVICE_STOPPED) {
      return;
    }
    SERVICE_STATUS status;
    EXPECT_TRUE(ControlService(service_handle_, SERVICE_CONTROL_STOP, &status))
        << fmt::format("Could not stop service, error {}", ::GetLastError());
    waitForServiceStatus(SERVICE_STOPPED);
  }

  void startService() {
    DWORD status = getServiceStatus();
    if (status == SERVICE_RUNNING) {
      stopService();
    }
    EXPECT_TRUE(StartServiceA(service_handle_, NULL, NULL))
        << fmt::format("Could not start service, error {}", ::GetLastError());
    waitForServiceStatus(SERVICE_RUNNING);
  }

  std::string config_file_;
  std::string envoy_exe_;
  SC_HANDLE scm_manager_handle_;
  SC_HANDLE service_handle_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ScmTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ScmTest, EnvoyStartsAndStopsUnderScm) {
  stopService();
  auto log_path = TestEnvironment::temporaryDirectory() + "/output.txt";
  auto commandLine =
      fmt::format("{} --config-path {} -l warn --log-path {}", envoy_exe_, config_file_, log_path);
  changeServicePath(commandLine);
  startService();
  stopService();
  remove(log_path.c_str());
}

TEST_P(ScmTest, EnvoySetsInvalidArgError) {
  stopService();
  auto commandLine = fmt::format("{} this is an invalid envoy command line", envoy_exe_);
  changeServicePath(commandLine);
  EXPECT_TRUE(StartServiceA(service_handle_, NULL, NULL))
      << fmt::format("Could not start service, error {}", ::GetLastError());
  waitForServiceStatus(SERVICE_STOPPED);
  SERVICE_STATUS_PROCESS service_status;
  DWORD bytes_needed;
  EXPECT_TRUE(QueryServiceStatusEx(service_handle_, SC_STATUS_PROCESS_INFO, (LPBYTE)&service_status,
                                   sizeof(SERVICE_STATUS_PROCESS), &bytes_needed))
      << fmt::format("Could not query service status, error {}", ::GetLastError());
  EXPECT_EQ(service_status.dwWin32ExitCode, ERROR_SERVICE_SPECIFIC_ERROR);
  EXPECT_EQ(service_status.dwServiceSpecificExitCode, E_INVALIDARG);
}

TEST_P(ScmTest, EnvoySetsEnvoyExceptionError) {
  stopService();
  auto log_path = "invalid/path/";
  auto commandLine =
      fmt::format("{} --config-path {} -l warn --log-path {}", envoy_exe_, config_file_, log_path);
  changeServicePath(commandLine);
  EXPECT_TRUE(StartServiceA(service_handle_, NULL, NULL))
      << fmt::format("Could not start service, error {}", ::GetLastError());
  waitForServiceStatus(SERVICE_STOPPED);
  SERVICE_STATUS_PROCESS service_status;
  DWORD bytes_needed;
  EXPECT_TRUE(QueryServiceStatusEx(service_handle_, SC_STATUS_PROCESS_INFO, (LPBYTE)&service_status,
                                   sizeof(SERVICE_STATUS_PROCESS), &bytes_needed))
      << fmt::format("Could not query service status, error {}", ::GetLastError());
  EXPECT_EQ(service_status.dwWin32ExitCode, ERROR_SERVICE_SPECIFIC_ERROR);
  EXPECT_EQ(service_status.dwServiceSpecificExitCode, E_FAIL);
}

TEST_P(ScmTest, EnvoyMergesArgumentsFromCreateAndStart) {
  stopService();
  auto log_path = TestEnvironment::temporaryDirectory() + "/output.txt";
  remove(log_path.c_str());
  auto commandLine = fmt::format("{} --config-path {} -l warn", envoy_exe_, config_file_);
  changeServicePath(commandLine);
  DWORD status = getServiceStatus();
  if (status == SERVICE_RUNNING) {
    stopService();
  }

  const char* extra_args[] = {"--log-path", log_path.c_str()};
  EXPECT_TRUE(StartServiceA(service_handle_, 2, extra_args))
      << fmt::format("Could not start service, error {}", ::GetLastError());
  waitForServiceStatus(SERVICE_RUNNING);
  std::ifstream generated_file(log_path.c_str());
  EXPECT_TRUE(generated_file.good());
  stopService();
  generated_file.close();
  remove(log_path.c_str());
}

#endif

} // namespace Envoy
