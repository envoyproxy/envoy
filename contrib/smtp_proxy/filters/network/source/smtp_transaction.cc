#include "contrib/smtp_proxy/filters/network/source/smtp_transaction.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpTransaction::SmtpTransaction(std::string& session_id, DecoderCallbacks* callbacks,
                                 TimeSource& time_source, Random::RandomGenerator& random_generator)
    : session_id_(session_id), callbacks_(callbacks), time_source_(time_source),
      random_generator_(random_generator),
      stream_info_(time_source_, callbacks_->connection().connectionInfoProviderSharedPtr()) {

  StreamInfo::StreamInfo& parent_stream_info = callbacks_->getStreamInfo();

  stream_info_.setUpstreamInfo(parent_stream_info.upstreamInfo());
  stream_info_.setDownstreamBytesMeter(std::make_shared<StreamInfo::BytesMeter>());

  stream_info_.setStreamIdProvider(
      std::make_shared<StreamInfo::StreamIdProviderImpl>(random_generator_.uuid()));

  transaction_id_ = stream_info_.getStreamIdProvider()->toStringView().value();
}

void SmtpTransaction::onComplete() { stream_info_.onRequestComplete(); }

void SmtpTransaction::emitLog() {
  // Emit per transaction log

  ProtobufWkt::Struct metadata(
      (*stream_info_.dynamicMetadata()
            .mutable_filter_metadata())[NetworkFilterNames::get().SmtpProxy]);

  auto& fields = *metadata.mutable_fields();
  fields["transaction_metadata"].mutable_struct_value()->Clear();

  ProtobufWkt::Struct transaction_metadata;
  encode(transaction_metadata);
  fields["transaction_metadata"].mutable_struct_value()->CopyFrom(transaction_metadata);

  ProtobufWkt::Value session_id;
  session_id.set_string_value(session_id_);
  fields["session_id"] = session_id;

  stream_info_.setDynamicMetadata(NetworkFilterNames::get().SmtpProxy, metadata);
  callbacks_->emitLogEntry(stream_info_);
}

void SmtpTransaction::encode(ProtobufWkt::Struct& metadata) {

  auto& fields = *(metadata.mutable_fields());

  ProtobufWkt::Value status;
  status.set_string_value(getStatus());
  fields["status"] = status;

  ProtobufWkt::Value sender;
  sender.set_string_value(sender_);
  fields["sender"] = sender;

  ProtobufWkt::Value payload;
  payload.set_number_value(payload_size_);
  fields["payload_size"] = payload;

  ProtobufWkt::ListValue recipients;
  for (auto& rcpt : recipients_) {
    recipients.add_values()->set_string_value(rcpt);
  }
  fields["recipients"].mutable_list_value()->CopyFrom(recipients);

  ProtobufWkt::ListValue commands;
  for (auto command : trxn_commands_) {
    ProtobufWkt::Struct data_struct;
    auto& fields = *(data_struct.mutable_fields());

    ProtobufWkt::Value name;
    name.set_string_value(command->getName());
    fields["command_name"] = name;

    ProtobufWkt::Value response_code;
    response_code.set_number_value(command->getResponseCode());
    fields["response_code"] = response_code;

    ProtobufWkt::Value duration;
    duration.set_number_value(command->getDuration());
    fields["duration"] = duration;

    commands.add_values()->mutable_struct_value()->CopyFrom(data_struct);
  }
  fields["commands"].mutable_list_value()->CopyFrom(commands);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
