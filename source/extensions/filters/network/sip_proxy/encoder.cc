#include "extensions/filters/network/sip_proxy/encoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

void EncoderImpl::encode(const MessageMetadataSharedPtr& metadata, Buffer::Instance& out) {
  std::string& msg = metadata->rawMsg();

  if (metadata->methodType() == MethodType::Ok200) {
    if (metadata->insertEPLocation().has_value() && metadata->insertTagLocation().has_value() &&
        metadata->EP().has_value()) {
      auto ep_insert_value = ";ep=" + std::string(metadata->EP().value());
      if (metadata->insertEPLocation().value() < metadata->insertTagLocation().value()) {
        msg.insert(metadata->insertEPLocation().value(), ep_insert_value);
        msg.insert(metadata->insertTagLocation().value() + ep_insert_value.length(), ";tag");
      } else {
        msg.insert(metadata->insertTagLocation().value(), ";tag");
        msg.insert(metadata->insertEPLocation().value(), ep_insert_value);
      }
    } else {
      if (metadata->insertEPLocation().has_value() && metadata->EP().has_value()) {
        msg.insert(metadata->insertEPLocation().value(),
                   ";ep=" + std::string(metadata->EP().value()));
      } else if (metadata->insertTagLocation().has_value()) {
        msg.insert(metadata->insertTagLocation().value(), ";tag");
      }
    }
  } else if (metadata->methodType() == MethodType::Invite &&
             metadata->insertEPLocation().has_value() && metadata->EP().has_value()) {
    msg.insert(metadata->insertEPLocation().value(),
               ";ep=" + std::string(metadata->EP().value()));
  } else if ((metadata->methodType() == MethodType::Ack ||
              metadata->methodType() == MethodType::Bye ||
              metadata->methodType() == MethodType::Cancel) &&
             metadata->insertTagLocation().has_value()) {
    msg.insert(metadata->insertTagLocation().value(), ";tag");
  }

  out.add(msg);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
