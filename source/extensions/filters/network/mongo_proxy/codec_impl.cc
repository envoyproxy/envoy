#include "extensions/filters/network/mongo_proxy/codec_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/mongo_proxy/bson_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

std::string
MessageImpl::documentListToString(const std::list<Bson::DocumentSharedPtr>& documents) const {
  std::stringstream out;
  out << "[";

  bool first = true;
  for (const Bson::DocumentSharedPtr& document : documents) {
    if (!first) {
      out << ", ";
    }

    out << document->toString();
    first = false;
  }

  out << "]";
  return out.str();
}

void GetMoreMessageImpl::fromBuffer(uint32_t, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding get more message");
  Bson::BufferHelper::removeInt32(data); // "zero" (unused)
  full_collection_name_ = Bson::BufferHelper::removeCString(data);
  number_to_return_ = Bson::BufferHelper::removeInt32(data);
  cursor_id_ = Bson::BufferHelper::removeInt64(data);
  ENVOY_LOG(trace, "{}", toString(true));
}

bool GetMoreMessageImpl::operator==(const GetMoreMessage& rhs) const {
  return requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
         fullCollectionName() == rhs.fullCollectionName() &&
         numberToReturn() == rhs.numberToReturn() && cursorId() == rhs.cursorId();
}

std::string GetMoreMessageImpl::toString(bool) const {
  return fmt::format(
      R"EOF({{"opcode": "OP_GET_MORE", "id": {}, "response_to": {}, "collection": "{}", "return": {}, )EOF"
      R"EOF("cursor": {}}})EOF",
      request_id_, response_to_, full_collection_name_, number_to_return_, cursor_id_);
}

void InsertMessageImpl::fromBuffer(uint32_t message_length, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding insert message");
  uint64_t original_buffer_length = data.length();
  ASSERT(message_length <= original_buffer_length);

  flags_ = Bson::BufferHelper::removeInt32(data);
  full_collection_name_ = Bson::BufferHelper::removeCString(data);
  while (data.length() - (original_buffer_length - message_length) > 0) {
    documents_.emplace_back(Bson::DocumentImpl::create(data));
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

bool InsertMessageImpl::operator==(const InsertMessage& rhs) const {
  if (!(requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
        flags() == rhs.flags() && fullCollectionName() == rhs.fullCollectionName() &&
        documents().size() == rhs.documents().size())) {
    return false;
  }

  for (auto i = documents().begin(), j = rhs.documents().begin(); i != documents().end();
       i++, j++) {
    if (!(**i == **j)) {
      return false;
    }
  }

  return true;
}

std::string InsertMessageImpl::toString(bool full) const {
  return fmt::format(
      R"EOF({{"opcode": "OP_INSERT", "id": {}, "response_to": {}, "flags": "{:#x}", "collection": "{}", )EOF"
      R"EOF("documents": {}}})EOF",
      request_id_, response_to_, flags_, full_collection_name_,
      full ? documentListToString(documents_) : std::to_string(documents_.size()));
}

void KillCursorsMessageImpl::fromBuffer(uint32_t, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding kill cursors message");
  Bson::BufferHelper::removeInt32(data); // zero
  number_of_cursor_ids_ = Bson::BufferHelper::removeInt32(data);
  for (int32_t i = 0; i < number_of_cursor_ids_; i++) {
    cursor_ids_.push_back(Bson::BufferHelper::removeInt64(data));
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

bool KillCursorsMessageImpl::operator==(const KillCursorsMessage& rhs) const {
  return requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
         numberOfCursorIds() == rhs.numberOfCursorIds() && cursorIds() == rhs.cursorIds();
}

std::string KillCursorsMessageImpl::toString(bool) const {
  std::stringstream cursors;
  cursors << "[";
  for (size_t i = 0; i < cursor_ids_.size(); i++) {
    if (i > 0) {
      cursors << ", ";
    }

    cursors << cursor_ids_[i];
  }
  cursors << "]";

  return fmt::format(
      R"EOF({{"opcode": "KILL_CURSORS", "id": {}, "response_to": "{:#x}", "num_cursors": "{}", )EOF"
      R"EOF("cursors": {}}})EOF",
      request_id_, response_to_, number_of_cursor_ids_, cursors.str());
}

void QueryMessageImpl::fromBuffer(uint32_t message_length, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding query message");
  uint64_t original_buffer_length = data.length();
  ASSERT(message_length <= original_buffer_length);

  flags_ = Bson::BufferHelper::removeInt32(data);
  full_collection_name_ = Bson::BufferHelper::removeCString(data);
  number_to_skip_ = Bson::BufferHelper::removeInt32(data);
  number_to_return_ = Bson::BufferHelper::removeInt32(data);
  query_ = Bson::DocumentImpl::create(data);

  if (data.length() - (original_buffer_length - message_length) > 0) {
    return_fields_selector_ = Bson::DocumentImpl::create(data);
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

bool QueryMessageImpl::operator==(const QueryMessage& rhs) const {
  if (!(requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
        flags() == rhs.flags() && fullCollectionName() == rhs.fullCollectionName() &&
        numberToSkip() == rhs.numberToSkip() && numberToReturn() == rhs.numberToReturn() &&
        !query() == !rhs.query() && !returnFieldsSelector() == !rhs.returnFieldsSelector())) {
    return false;
  }

  if (query()) {
    if (!(*query() == *rhs.query())) {
      return false;
    }
  }

  if (returnFieldsSelector()) {
    if (!(*returnFieldsSelector() == *rhs.returnFieldsSelector())) {
      return false;
    }
  }

  return true;
}

std::string QueryMessageImpl::toString(bool full) const {
  return fmt::format(
      R"EOF({{"opcode": "OP_QUERY", "id": {}, "response_to": {}, "flags": "{:#x}", "collection": "{}", )EOF"
      R"EOF("skip": {}, "return": {}, "query": {}, "fields": {}}})EOF",
      request_id_, response_to_, flags_, full_collection_name_, number_to_skip_, number_to_return_,
      full ? query_->toString() : "\"{...}\"",
      return_fields_selector_ ? return_fields_selector_->toString() : "{}");
}

void ReplyMessageImpl::fromBuffer(uint32_t, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding reply message");
  flags_ = Bson::BufferHelper::removeInt32(data);
  cursor_id_ = Bson::BufferHelper::removeInt64(data);
  starting_from_ = Bson::BufferHelper::removeInt32(data);
  number_returned_ = Bson::BufferHelper::removeInt32(data);
  for (int32_t i = 0; i < number_returned_; i++) {
    documents_.emplace_back(Bson::DocumentImpl::create(data));
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

bool ReplyMessageImpl::operator==(const ReplyMessage& rhs) const {
  if (!(requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
        flags() == rhs.flags() && cursorId() == rhs.cursorId() &&
        startingFrom() == rhs.startingFrom() && numberReturned() == rhs.numberReturned())) {

    return false;
  }

  for (auto i = documents().begin(), j = rhs.documents().begin(); i != documents().end();
       i++, j++) {
    if (!(**i == **j)) {
      return false;
    }
  }

  return true;
}

std::string ReplyMessageImpl::toString(bool full) const {
  return fmt::format(
      R"EOF({{"opcode": "OP_REPLY", "id": {}, "response_to": {}, "flags": "{:#x}", "cursor": "{}", )EOF"
      R"EOF("from": {}, "returned": {}, "documents": {}}})EOF",
      request_id_, response_to_, flags_, cursor_id_, starting_from_, number_returned_,
      full ? documentListToString(documents_) : std::to_string(documents_.size()));
}

/*
 * OP_COMMAND mongo message implementation.
 */
void CommandMessageImpl::fromBuffer(uint32_t message_length, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding COMMAND message");
  const uint64_t original_data_length = data.length();
  ASSERT(data.length() >= message_length); // See comment below about relationship.

  database_ = Bson::BufferHelper::removeCString(data);
  command_name_ = Bson::BufferHelper::removeCString(data);
  metadata_ = Bson::DocumentImpl::create(data);
  command_args_ = Bson::DocumentImpl::create(data);

  // There may be additional docs.
  // message_length is mongo message length. original_data_length contains
  // mongo message and possibly first few bytes of next message.
  while (data.length() - (original_data_length - message_length) > 0) {
    input_docs_.emplace_back(Bson::DocumentImpl::create(data));
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

std::string CommandMessageImpl::toString(bool full) const {
  return fmt::format(
      R"EOF({{"opcode": "OP_COMMAND", "id": {}, "response_to": {}, "database": "{}", )EOF"
      R"EOF("commandName": "{}", "metadata": {}, )EOF"
      R"EOF("commandArgs": {}, "inputDocs": {}}})EOF",
      request_id_, response_to_, database_.c_str(), command_name_.c_str(), metadata_->toString(),
      command_args_->toString(),
      full ? documentListToString(input_docs_) : std::to_string(input_docs_.size()));
}

bool CommandMessageImpl::operator==(const CommandMessage& rhs) const {
  if (!(requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
        database() == rhs.database() && commandName() == rhs.commandName() &&
        !metadata() == !rhs.metadata() && !commandArgs() == !rhs.commandArgs() &&
        inputDocs().size() == rhs.inputDocs().size())) {
    return false;
  }

  // Compare documents now.
  if (metadata()) {
    if (!(*metadata() == *rhs.metadata())) {
      return false;
    }
  }

  if (commandArgs()) {
    if (!(*commandArgs() == *rhs.commandArgs())) {
      return false;
    }
  }

  for (auto i = inputDocs().begin(), j = rhs.inputDocs().begin(); i != inputDocs().end();
       i++, j++) {
    if (!(**i == **j)) {
      return false;
    }
  }

  return true;
}

// OP_COMMANDREPLY implementation.
void CommandReplyMessageImpl::fromBuffer(uint32_t message_length, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding COMMAND REPLY message");
  const uint64_t original_data_length = data.length();
  ASSERT(data.length() >= message_length); // See comment below about relationship.

  metadata_ = Bson::DocumentImpl::create(data);
  command_reply_ = Bson::DocumentImpl::create(data);

  // There may be additional docs.
  // message_length is mongo message length. original_data_length contains
  // mongo message and possibly first few bytes of next message.
  while (data.length() - (original_data_length - message_length) > 0) {
    output_docs_.emplace_back(Bson::DocumentImpl::create(data));
  }

  ENVOY_LOG(trace, "{}", toString(true));
}

std::string CommandReplyMessageImpl::toString(bool full) const {
  return fmt::format(R"EOF({{"opcode": "OP_COMMANDREPLY", "id": {}, "response_to": {}, )EOF"
                     R"EOF("metadata": {}, "commandReply": {}, "outputDocs":{}}} )EOF",
                     request_id_, response_to_, metadata_->toString(), command_reply_->toString(),
                     full ? documentListToString(output_docs_)
                          : std::to_string(output_docs_.size()));
}

bool CommandReplyMessageImpl::operator==(const CommandReplyMessage& rhs) const {
  if (!(requestId() == rhs.requestId() && responseTo() == rhs.responseTo() &&
        !metadata() == !rhs.metadata() && !commandReply() == !rhs.commandReply() &&
        outputDocs().size() == rhs.outputDocs().size())) {
    return false;
  }

  // Compare documents now.
  if (metadata()) {
    if (!(*metadata() == *rhs.metadata())) {
      return false;
    }
  }

  if (commandReply()) {
    if (!(*commandReply() == *rhs.commandReply())) {
      return false;
    }
  }

  for (auto i = outputDocs().begin(), j = rhs.outputDocs().begin(); i != outputDocs().end();
       i++, j++) {
    if (!(**i == **j)) {
      return false;
    }
  }

  return true;
}
bool DecoderImpl::decode(Buffer::Instance& data) {
  // See if we have enough data for the message length.
  ENVOY_LOG(trace, "decoding {} bytes", data.length());
  if (data.length() < sizeof(int32_t)) {
    return false;
  }

  uint32_t message_length = Bson::BufferHelper::peekInt32(data);
  ENVOY_LOG(trace, "message is {} bytes", message_length);
  if (data.length() < message_length) {
    return false;
  }

  data.drain(sizeof(int32_t));
  int32_t request_id = Bson::BufferHelper::removeInt32(data);
  int32_t response_to = Bson::BufferHelper::removeInt32(data);
  Message::OpCode op_code = static_cast<Message::OpCode>(Bson::BufferHelper::removeInt32(data));
  ENVOY_LOG(trace, "message op: {}", static_cast<int32_t>(op_code));

  // Some messages need to know how long they are to parse. Subtract the header that we have already
  // parsed off before passing the final value.
  message_length -= Message::MessageHeaderSize;

  switch (op_code) {
  case Message::OpCode::OP_REPLY: {
    std::unique_ptr<ReplyMessageImpl> message(new ReplyMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeReply(std::move(message));
    break;
  }

  case Message::OpCode::OP_QUERY: {
    std::unique_ptr<QueryMessageImpl> message(new QueryMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeQuery(std::move(message));
    break;
  }

  case Message::OpCode::OP_GET_MORE: {
    std::unique_ptr<GetMoreMessageImpl> message(new GetMoreMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeGetMore(std::move(message));
    break;
  }

  case Message::OpCode::OP_INSERT: {
    std::unique_ptr<InsertMessageImpl> message(new InsertMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeInsert(std::move(message));
    break;
  }

  case Message::OpCode::OP_KILL_CURSORS: {
    std::unique_ptr<KillCursorsMessageImpl> message(
        new KillCursorsMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeKillCursors(std::move(message));
    break;
  }

  case Message::OpCode::OP_COMMAND: {
    std::unique_ptr<CommandMessageImpl> message(new CommandMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeCommand(std::move(message));
    break;
  }

  case Message::OpCode::OP_COMMANDREPLY: {
    std::unique_ptr<CommandReplyMessageImpl> message(
        new CommandReplyMessageImpl(request_id, response_to));
    message->fromBuffer(message_length, data);
    callbacks_.decodeCommandReply(std::move(message));
    break;
  }

  default:
    throw EnvoyException(fmt::format("invalid mongo op {}", static_cast<int32_t>(op_code)));
  }

  ENVOY_LOG(trace, "{} bytes remaining after decoding", data.length());
  return true;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  while (data.length() > 0 && decode(data)) {
  }
}

void EncoderImpl::encodeCommonHeader(int32_t total_size, const Message& message,
                                     Message::OpCode op) {
  Bson::BufferHelper::writeInt32(output_, total_size);
  Bson::BufferHelper::writeInt32(output_, message.requestId());
  Bson::BufferHelper::writeInt32(output_, message.responseTo());
  Bson::BufferHelper::writeInt32(output_, static_cast<int32_t>(op));
}

void EncoderImpl::encodeGetMore(const GetMoreMessage& message) {
  if (message.fullCollectionName().empty() || message.cursorId() == 0) {
    throw EnvoyException("invalid get more message");
  }

  // https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#op-get-more
  int32_t total_size = Message::MessageHeaderSize + Message::Int32Length +
                       message.fullCollectionName().size() + Message::StringPaddingLength +
                       Message::Int32Length + Message::Int64Length;

  encodeCommonHeader(total_size, message, Message::OpCode::OP_GET_MORE);
  Bson::BufferHelper::writeInt32(output_, 0);
  Bson::BufferHelper::writeCString(output_, message.fullCollectionName());
  Bson::BufferHelper::writeInt32(output_, message.numberToReturn());
  Bson::BufferHelper::writeInt64(output_, message.cursorId());
}

void EncoderImpl::encodeInsert(const InsertMessage& message) {
  if (message.fullCollectionName().empty() || message.documents().empty()) {
    throw EnvoyException("invalid insert message");
  }

  // https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#op-insert
  int32_t total_size = Message::MessageHeaderSize + Message::Int32Length +
                       message.fullCollectionName().size() + Message::StringPaddingLength;
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    total_size += document->byteSize();
  }

  encodeCommonHeader(total_size, message, Message::OpCode::OP_INSERT);
  Bson::BufferHelper::writeInt32(output_, message.flags());
  Bson::BufferHelper::writeCString(output_, message.fullCollectionName());
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    document->encode(output_);
  }
}

void EncoderImpl::encodeKillCursors(const KillCursorsMessage& message) {
  if (message.numberOfCursorIds() == 0 ||
      message.numberOfCursorIds() != static_cast<int32_t>(message.cursorIds().size())) {
    throw EnvoyException("invalid kill cursors message");
  }

  // https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#op-kill-cursors
  int32_t total_size =
      Message::MessageHeaderSize + 2 * Message::Int32Length + (message.numberOfCursorIds() * 8);

  encodeCommonHeader(total_size, message, Message::OpCode::OP_KILL_CURSORS);
  Bson::BufferHelper::writeInt32(output_, 0);
  Bson::BufferHelper::writeInt32(output_, message.numberOfCursorIds());
  for (int64_t cursor : message.cursorIds()) {
    Bson::BufferHelper::writeInt64(output_, cursor);
  }
}

void EncoderImpl::encodeQuery(const QueryMessage& message) {
  if (message.fullCollectionName().empty() || !message.query()) {
    throw EnvoyException("invalid query message");
  }

  // https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#op-query
  int32_t total_size = Message::MessageHeaderSize + 3 * Message::Int32Length +
                       message.fullCollectionName().size() + Message::StringPaddingLength +
                       message.query()->byteSize();
  if (message.returnFieldsSelector()) {
    total_size += message.returnFieldsSelector()->byteSize();
  }

  encodeCommonHeader(total_size, message, Message::OpCode::OP_QUERY);
  Bson::BufferHelper::writeInt32(output_, message.flags());
  Bson::BufferHelper::writeCString(output_, message.fullCollectionName());
  Bson::BufferHelper::writeInt32(output_, message.numberToSkip());
  Bson::BufferHelper::writeInt32(output_, message.numberToReturn());

  message.query()->encode(output_);
  if (message.returnFieldsSelector()) {
    message.returnFieldsSelector()->encode(output_);
  }
}

void EncoderImpl::encodeReply(const ReplyMessage& message) {
  // https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#op-reply
  int32_t total_size = Message::MessageHeaderSize + 3 * Message::Int32Length + Message::Int64Length;
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    total_size += document->byteSize();
  }

  encodeCommonHeader(total_size, message, Message::OpCode::OP_REPLY);
  Bson::BufferHelper::writeInt32(output_, message.flags());
  Bson::BufferHelper::writeInt64(output_, message.cursorId());
  Bson::BufferHelper::writeInt32(output_, message.startingFrom());
  Bson::BufferHelper::writeInt32(output_, message.numberReturned());
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    document->encode(output_);
  }
}

void EncoderImpl::encodeCommand(const CommandMessage& message) {
  int32_t total_size = Message::MessageHeaderSize;
  total_size += message.database().size() + Message::StringPaddingLength;
  total_size += message.commandName().size() + Message::StringPaddingLength;
  total_size += message.metadata()->byteSize();
  total_size += message.commandArgs()->byteSize();
  for (const Bson::DocumentSharedPtr& document : message.inputDocs()) {
    total_size += document->byteSize();
  }

  // Now encode.
  encodeCommonHeader(total_size, message, Message::OpCode::OP_COMMAND);
  Bson::BufferHelper::writeCString(output_, message.database());
  Bson::BufferHelper::writeCString(output_, message.commandName());
  message.metadata()->encode(output_);
  message.commandArgs()->encode(output_);
  for (const Bson::DocumentSharedPtr& document : message.inputDocs()) {
    document->encode(output_);
  }
}

void EncoderImpl::encodeCommandReply(const CommandReplyMessage& message) {
  int32_t total_size = Message::MessageHeaderSize;
  total_size += message.metadata()->byteSize();
  total_size += message.commandReply()->byteSize();
  for (const Bson::DocumentSharedPtr& document : message.outputDocs()) {
    total_size += document->byteSize();
  }

  // Now encode.
  encodeCommonHeader(total_size, message, Message::OpCode::OP_COMMANDREPLY);
  message.metadata()->encode(output_);
  message.commandReply()->encode(output_);
  for (const Bson::DocumentSharedPtr& document : message.outputDocs()) {
    document->encode(output_);
  }
}
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
