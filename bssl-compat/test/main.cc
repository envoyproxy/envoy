#include <gtest/gtest.h>


class OpenSSLConf {
  public:

    OpenSSLConf(const char *config) {
      int fd = mkstemp(m_path.data());
      if (fd != -1) {
        FILE* file = fdopen(fd, "w");
        if (file) {
          if (fwrite(config, 1, strlen(config), file) == strlen(config)) {
            fclose(file);
            if (setenv("OPENSSL_CONF", m_path.c_str(), 1) == 0) {
              return;
            }
          }
        }
      }
      throw std::runtime_error("Failed to set up OPENSSL_CONF");
    }

    ~OpenSSLConf() {
      unlink(m_path.c_str());
    }

private:

    std::string m_path { "/tmp/openssl.conf.XXXXXX" };
};


static OpenSSLConf openssl_conf(R"(
  openssl_conf = openssl_init

  [openssl_init]
  providers = providers_sect
  ssl_conf = ssl_sect

  [providers_sect]
  default = default_sect
  legacy = legacy_sect

  [default_sect]
  activate = 1

  [legacy_sect]
  activate = 1

  [ssl_sect]
  system_default = system_default_sect

  [system_default_sect]
  CipherString = DEFAULT:@SECLEVEL=1
)");


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // Run all tests and return the result.
  return RUN_ALL_TESTS();
}