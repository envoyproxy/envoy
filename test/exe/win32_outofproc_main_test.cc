#include <cstdio>

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

#ifdef WIN32
class MainCommonTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  MainCommonTest()
      : config_file_(TestEnvironment::temporaryFileSubstitute(
            "test/config/integration/google_com_proxy_port_0.yaml", TestEnvironment::ParamMap(),
            TestEnvironment::PortMap(), GetParam())),
        envoy_exe_(TestEnvironment::runfilesPath("source/exe/envoy-static.exe")) {}

  void cleanup() {
    UINT uExitCode = 0;
    TerminateProcess(piProcInfo_.hProcess, uExitCode);
    remove(log_path_.c_str());
  }

  void sendSingal(DWORD pid, DWORD signal) {
    EXPECT_TRUE(FreeConsole()) << fmt::format("Failed to FreeConsole, error {}", GetLastError());
    EXPECT_TRUE(AttachConsole(pid))
        << fmt::format("Failed to AttachConsole, error {}", GetLastError());
    EXPECT_TRUE(SetConsoleCtrlHandler(NULL, 1))
        << fmt::format("Failed to SetConsoleCtrlHandler, error {}", GetLastError());
    EXPECT_TRUE(GenerateConsoleCtrlEvent(signal, pid))
        << fmt::format("Failed to GenerateConsoleCtrlEvent, error {}", GetLastError());
  }

  void createEnvoyProcess() {
    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    log_path_ = TestEnvironment::temporaryDirectory() + "/output.txt";
    ZeroMemory(&piProcInfo_, sizeof(PROCESS_INFORMATION));
    auto commandLine = fmt::format("{} --config-path {} -l warn --log-path {}", envoy_exe_,
                                   config_file_, log_path_);
    ENVOY_LOG_MISC(warn, commandLine);
    BOOL bSuccess = CreateProcessA(NULL,
                                   const_cast<char*>(commandLine.c_str()), // command line
                                   NULL,                     // process security attributes
                                   NULL,                     // primary thread security attributes
                                   1,                        // handles are inherited
                                   CREATE_NEW_PROCESS_GROUP, // creation flags
                                   NULL,                     // use parent's environment
                                   NULL,                     // use parent's current directory
                                   &siStartInfo,             // `STARTUPINFO` pointer
                                   &piProcInfo_);            // receives `PROCESS_INFORMATION`
    EXPECT_TRUE(bSuccess) << fmt::format("Failed to create Envoy process, error {}",
                                         GetLastError());
  }

  std::string config_file_;
  std::string envoy_exe_;
  PROCESS_INFORMATION piProcInfo_;
  std::string log_path_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MainCommonTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MainCommonTest, EnvoyHandlesCtrlBreakEvent) {
  createEnvoyProcess();
  ENVOY_LOG_MISC(warn, "Envoy process with pid {}", piProcInfo_.dwProcessId);
  Sleep(2 * 1000);
  sendSingal(piProcInfo_.dwProcessId, CTRL_BREAK_EVENT);
  size_t total_sleep = 1;
  constexpr size_t resonable_sleep_time = 5;
  DWORD exitCode;
  do {
    Sleep(total_sleep * 1000);
    ++total_sleep;
    GetExitCodeProcess(piProcInfo_.hProcess, &exitCode);
  } while (exitCode == STILL_ACTIVE && total_sleep <= resonable_sleep_time);
  EXPECT_TRUE(total_sleep <= resonable_sleep_time);
  auto output = TestEnvironment::readFileToStringForTest(log_path_);
  size_t count = 0;
  for (size_t pos = 0; (pos = output.find("ENVOY_SIGTERM", pos)) != std::string::npos;
       ++pos, ++count) {
  }
  EXPECT_EQ(1, count);
  ENVOY_LOG_MISC(warn, "Envoy output {}", output);
  cleanup();
}
#endif

} // namespace Envoy
