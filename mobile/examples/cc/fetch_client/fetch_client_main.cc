#include "examples/cc/fetch_client/fetch_client.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];

const char build_scm_revision[] = "0";
const char build_scm_status[] = "test";

// Fetches each URL specified on the command line in series,
// and prints the contents to standard out.
int main(int argc, char** argv) {
  Envoy::Fetch client;
  std::vector<absl::string_view> urls;
  // Start at 1 to skip the command name.
  for (int i = 1; i < argc; ++i) {
    urls.push_back(argv[i]);
  }
  std::vector<Envoy::Http::Protocol> protocols;
  client.fetch(urls, /* quic_hints=*/{}, /* protocols= */ protocols);

  exit(0);
}
