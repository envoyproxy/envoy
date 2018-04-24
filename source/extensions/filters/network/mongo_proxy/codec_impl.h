#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/common/logger.h"

#include "extensions/filters/network/mongo_proxy/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class MessageImpl : public virtual Message {
public:
  MessageImpl(int32_t request_id, uint32_t response_to)
      : request_id_(request_id), response_to_(response_to) {}

  virtual void fromBuffer(uint32_t message_length, Buffer::Instance& data) PURE;

  // Mongo::Message
  int32_t requestId() const override { return request_id_; }
  int32_t responseTo() const override { return response_to_; }

protected:
  std::string documentListToString(const std::list<Bson::DocumentSharedPtr>& documents) const;

  const int32_t request_id_;
  const int32_t response_to_;
};

class GetMoreMessageImpl : public MessageImpl,
                           public GetMoreMessage,
                           Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;

  // Mongo::Message
  std::string toString(bool full) const override;

  // Mongo::GetMoreMessage
  bool operator==(const GetMoreMessage& rhs) const override;
  const std::string& fullCollectionName() const override { return full_collection_name_; }
  void fullCollectionName(const std::string& name) override { full_collection_name_ = name; }
  int32_t numberToReturn() const override { return number_to_return_; }
  void numberToReturn(int32_t to_return) override { number_to_return_ = to_return; }
  int64_t cursorId() const override { return cursor_id_; }
  void cursorId(int64_t cursor_id) override { cursor_id_ = cursor_id; }

private:
  std::string full_collection_name_;
  int32_t number_to_return_{};
  int64_t cursor_id_{};
};

class InsertMessageImpl : public MessageImpl,
                          public InsertMessage,
                          Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;

  // Mongo::Message
  std::string toString(bool full) const override;

  // Mongo::InsertMessage
  bool operator==(const InsertMessage& rhs) const override;
  int32_t flags() const override { return flags_; }
  void flags(int32_t flags) override { flags_ = flags; }
  const std::string& fullCollectionName() const override { return full_collection_name_; }
  void fullCollectionName(const std::string& name) override { full_collection_name_ = name; }
  const std::list<Bson::DocumentSharedPtr>& documents() const override { return documents_; }
  std::list<Bson::DocumentSharedPtr>& documents() override { return documents_; }

private:
  int32_t flags_{};
  std::string full_collection_name_;
  std::list<Bson::DocumentSharedPtr> documents_;
};

class KillCursorsMessageImpl : public MessageImpl,
                               public KillCursorsMessage,
                               Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;

  // Mongo::Message
  std::string toString(bool full) const override;

  // Mongo::KillCursorsMessage
  bool operator==(const KillCursorsMessage& rhs) const override;
  int32_t numberOfCursorIds() const override { return number_of_cursor_ids_; }
  void numberOfCursorIds(int32_t number_of_cursor_ids) override {
    number_of_cursor_ids_ = number_of_cursor_ids;
  }
  const std::vector<int64_t>& cursorIds() const override { return cursor_ids_; }
  void cursorIds(std::vector<int64_t>&& cursor_ids) override {
    cursor_ids_ = std::move(cursor_ids);
  }

private:
  int32_t number_of_cursor_ids_{};
  std::vector<int64_t> cursor_ids_;
};

class QueryMessageImpl : public MessageImpl,
                         public QueryMessage,
                         Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;

  // Mongo::Message
  std::string toString(bool full) const override;

  // Mongo::QueryMessage
  bool operator==(const QueryMessage& rhs) const override;
  int32_t flags() const override { return flags_; }
  void flags(int32_t flags) override { flags_ = flags; }
  const std::string& fullCollectionName() const override { return full_collection_name_; }
  void fullCollectionName(const std::string& name) override { full_collection_name_ = name; }
  int32_t numberToSkip() const override { return number_to_skip_; }
  void numberToSkip(int32_t skip) override { number_to_skip_ = skip; }
  int32_t numberToReturn() const override { return number_to_return_; }
  void numberToReturn(int32_t to_return) override { number_to_return_ = to_return; }
  virtual const Bson::Document* query() const override { return query_.get(); }
  void query(Bson::DocumentSharedPtr&& query) override { query_ = std::move(query); }
  virtual const Bson::Document* returnFieldsSelector() const override {
    return return_fields_selector_.get();
  }
  void returnFieldsSelector(Bson::DocumentSharedPtr&& fields) override {
    return_fields_selector_ = std::move(fields);
  }

private:
  int32_t flags_{};
  std::string full_collection_name_;
  int32_t number_to_skip_{};
  int32_t number_to_return_{};
  Bson::DocumentSharedPtr query_;
  Bson::DocumentSharedPtr return_fields_selector_;
};

class ReplyMessageImpl : public MessageImpl,
                         public ReplyMessage,
                         Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;

  // Mongo::Message
  std::string toString(bool full) const override;

  // Mongo::ReplyMessage
  bool operator==(const ReplyMessage& rhs) const override;
  int32_t flags() const override { return flags_; }
  void flags(int32_t flags) override { flags_ = flags; }
  int64_t cursorId() const override { return cursor_id_; }
  void cursorId(int64_t cursor_id) override { cursor_id_ = cursor_id; }
  int32_t startingFrom() const override { return starting_from_; }
  void startingFrom(int32_t starting_from) override { starting_from_ = starting_from; }
  int32_t numberReturned() const override { return number_returned_; }
  void numberReturned(int32_t number_returned) override { number_returned_ = number_returned; }
  const std::list<Bson::DocumentSharedPtr>& documents() const override { return documents_; }
  std::list<Bson::DocumentSharedPtr>& documents() override { return documents_; }

private:
  int32_t flags_{};
  int64_t cursor_id_{};
  int32_t starting_from_{};
  int32_t number_returned_{};
  std::list<Bson::DocumentSharedPtr> documents_;
};

// OP_COMMAND message.
class CommandMessageImpl : public MessageImpl,
                           public CommandMessage,
                           Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl.
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;
  std::string toString(bool full) const override;

  // CommandMessageImpl accessors.
  bool operator==(const CommandMessage& rhs) const override;
  std::string database() const override { return database_; }
  void database(std::string database) override { database_ = database; }
  std::string commandName() const override { return command_name_; }
  void commandName(std::string command_name) override { command_name_ = command_name; }
  const Bson::Document* metadata() const override { return metadata_.get(); }
  void metadata(Bson::DocumentSharedPtr&& metadata) override { metadata_ = std::move(metadata); }
  const Bson::Document* commandArgs() const override { return command_args_.get(); }
  void commandArgs(Bson::DocumentSharedPtr&& command_args) override {
    command_args_ = std::move(command_args);
  }
  const std::list<Bson::DocumentSharedPtr>& inputDocs() const override { return input_docs_; }
  std::list<Bson::DocumentSharedPtr>& inputDocs() override { return input_docs_; }

private:
  std::string database_;
  std::string command_name_;
  Bson::DocumentSharedPtr metadata_;
  Bson::DocumentSharedPtr command_args_;
  std::list<Bson::DocumentSharedPtr> input_docs_;
};

// OP_COMMANDREPLY message.
class CommandReplyMessageImpl : public MessageImpl,
                                public CommandReplyMessage,
                                Logger::Loggable<Logger::Id::mongo> {
public:
  using MessageImpl::MessageImpl;

  // MessageImpl.
  void fromBuffer(uint32_t message_length, Buffer::Instance& data) override;
  std::string toString(bool full) const override;

  // CommandMessageReplyImpl accessors.
  bool operator==(const CommandReplyMessage& rhs) const override;
  const Bson::Document* metadata() const override { return metadata_.get(); }
  void metadata(Bson::DocumentSharedPtr&& metadata) override { metadata_ = std::move(metadata); }
  const Bson::Document* commandReply() const override { return command_reply_.get(); }
  void commandReply(Bson::DocumentSharedPtr&& command_reply) override {
    command_reply_ = std::move(command_reply);
  }
  const std::list<Bson::DocumentSharedPtr>& outputDocs() const override { return output_docs_; }
  std::list<Bson::DocumentSharedPtr>& outputDocs() override { return output_docs_; }

private:
  Bson::DocumentSharedPtr metadata_;
  Bson::DocumentSharedPtr command_reply_;
  std::list<Bson::DocumentSharedPtr> output_docs_;
};

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::mongo> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // Mongo::Decoder
  void onData(Buffer::Instance& data) override;

private:
  bool decode(Buffer::Instance& data);

  DecoderCallbacks& callbacks_;
};

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::mongo> {
public:
  EncoderImpl(Buffer::Instance& output) : output_(output) {}

  // Mongo::Encoder
  void encodeGetMore(const GetMoreMessage& message) override;
  void encodeInsert(const InsertMessage& message) override;
  void encodeKillCursors(const KillCursorsMessage& message) override;
  void encodeQuery(const QueryMessage& message) override;
  void encodeReply(const ReplyMessage& message) override;
  void encodeCommand(const CommandMessage& message) override;
  void encodeCommandReply(const CommandReplyMessage& message) override;

private:
  void encodeCommonHeader(int32_t total_size, const Message& message, Message::OpCode op);

  Buffer::Instance& output_;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
