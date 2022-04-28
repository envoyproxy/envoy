#include "source/extensions/filters/listener/original_dst/original_dst.h"

#include "envoy/network/listen_socket.h"

#include "source/common/common/assert.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

Network::Address::InstanceConstSharedPtr OriginalDstFilter::getOriginalDst(Network::Socket& sock) {
  return Network::Utility::getOriginalDst(sock);
}

Network::FilterStatus OriginalDstFilter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "original_dst: new connection accepted");
  Network::ConnectionSocket& socket = cb.socket();

  if (socket.addressType() == Network::Address::Type::Ip) {
    Network::Address::InstanceConstSharedPtr original_local_address = getOriginalDst(socket);
    // A listener that has the use_original_dst flag set to true can still receive
    // connections that are NOT redirected using iptables. If a connection was not redirected,
    // the address returned by getOriginalDst() matches the local address of the new socket.
    // In this case the listener handles the connection directly and does not hand it off.
    if (original_local_address) {
#ifdef WIN32
      // See how to perform bind or connect redirection here:
      // https://docs.microsoft.com/en-us/windows-hardware/drivers/network/using-bind-or-connect-redirection
      if constexpr (Platform::win32SupportsOriginalDestination()) {
        if (traffic_direction_ == envoy::config::core::v3::OUTBOUND) {
          ENVOY_LOG(debug, "[Windows] Querying for redirect record for outbound listener");
          unsigned long redirectRecordsSize = 0;
          auto redirect_records = std::make_shared<Network::Win32RedirectRecords>();
          auto status = socket.ioctl(SIO_QUERY_WFP_CONNECTION_REDIRECT_RECORDS, NULL, 0,
                                     redirect_records->buf_, sizeof(redirect_records->buf_),
                                     &redirect_records->buf_size_);
          if (status.return_value_ != 0) {
            ENVOY_LOG(debug,
                      "closing connection: cannot broker connection to original destination "
                      "[Query redirect record failed] with error {}",
                      status.errno_);
            return Network::FilterStatus::StopIteration;
          }

          StreamInfo::FilterState& filter_state = cb.filterState();
          auto has_options = filter_state.hasData<Network::UpstreamSocketOptionsFilterState>(
              Network::UpstreamSocketOptionsFilterState::key());
          if (!has_options) {
            filter_state.setData(Network::UpstreamSocketOptionsFilterState::key(),
                                 std::make_unique<Network::UpstreamSocketOptionsFilterState>(),
                                 StreamInfo::FilterState::StateType::Mutable,
                                 StreamInfo::FilterState::LifeSpan::Connection);
          }
          filter_state
              .getDataMutable<Network::UpstreamSocketOptionsFilterState>(
                  Network::UpstreamSocketOptionsFilterState::key())
              ->addOption(
                  Network::SocketOptionFactory::buildWFPRedirectRecordsOptions(*redirect_records));
        }
      }
#endif
      ENVOY_LOG(trace, "original_dst: set destination to {}", original_local_address->asString());

      // Restore the local address to the original one.
      socket.connectionInfoProvider().restoreLocalAddress(original_local_address);
    }
  }

  return Network::FilterStatus::Continue;
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
