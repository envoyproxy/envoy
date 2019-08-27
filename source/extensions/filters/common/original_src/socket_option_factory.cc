#include "extensions/filters/common/original_src/socket_option_factory.h"

#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"

#include "extensions/filters/common/original_src/original_src_socket_option.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace OriginalSrc {

Network::Socket::OptionsSharedPtr
buildOriginalSrcOptions(Network::Address::InstanceConstSharedPtr source, uint32_t mark) {
  const auto address_without_port = Network::Utility::getAddressWithPort(*source, 0);

  // Note: we don't expect this to change the behaviour of the socket. We expect it to be copied
  // into the upstream connection later.
  auto options_to_add = std::make_shared<Network::Socket::Options>();
  options_to_add->emplace_back(
      std::make_shared<Filters::Common::OriginalSrc::OriginalSrcSocketOption>(
          std::move(address_without_port)));

  if (mark != 0) {
    const auto mark_options = Network::SocketOptionFactory::buildSocketMarkOptions(mark);
    options_to_add->insert(options_to_add->end(), mark_options->begin(), mark_options->end());
  }

  const auto transparent_options = Network::SocketOptionFactory::buildIpTransparentOptions();
  options_to_add->insert(options_to_add->end(), transparent_options->begin(),
                         transparent_options->end());
  return options_to_add;
}

} // namespace OriginalSrc
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
