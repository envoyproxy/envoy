#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "extensions/filters/network/mongo_proxy/bson.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

/**
 * General implementation of https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/
 */

/**
 * Base class for all mongo messages.
 */
class Message {
public:
  enum class OpCode {
    Reply = 1,
    Msg = 1000,
    Update = 2001,
    Insert = 2002,
    Query = 2004,
    GetMore = 2005,
    Delete = 2006,
    KillCursors = 2007,
    Command = 2010,
    CommandReply = 2011
  };

  virtual ~Message() = default;

  virtual int32_t requestId() const PURE;
  virtual int32_t responseTo() const PURE;
  virtual std::string toString(bool full) const PURE;

  // Define some constants used in mongo messages encoding
  constexpr static uint32_t MessageHeaderSize = 16;
  constexpr static uint32_t Int32Length = 4;
  constexpr static uint32_t Int64Length = 8;
  constexpr static uint32_t StringPaddingLength = 1;
};

/**
 * Mongo OP_GET_MORE message.
 */
class GetMoreMessage : public virtual Message {
public:
  virtual bool operator==(const GetMoreMessage& rhs) const PURE;

  virtual const std::string& fullCollectionName() const PURE;
  virtual void fullCollectionName(const std::string& name) PURE;
  virtual int32_t numberToReturn() const PURE;
  virtual void numberToReturn(int32_t to_return) PURE;
  virtual int64_t cursorId() const PURE;
  virtual void cursorId(int64_t cursor_id) PURE;
};

using GetMoreMessagePtr = std::unique_ptr<GetMoreMessage>;

/**
 * Mongo OP_INSERT message.
 */
class InsertMessage : public virtual Message {
public:
  virtual bool operator==(const InsertMessage& rhs) const PURE;

  virtual int32_t flags() const PURE;
  virtual void flags(int32_t flags) PURE;
  virtual const std::string& fullCollectionName() const PURE;
  virtual void fullCollectionName(const std::string& name) PURE;
  virtual const std::list<Bson::DocumentSharedPtr>& documents() const PURE;
  virtual std::list<Bson::DocumentSharedPtr>& documents() PURE;
};

using InsertMessagePtr = std::unique_ptr<InsertMessage>;

/**
 * Mongo OP_KILL_CURSORS message.
 */
class KillCursorsMessage : public virtual Message {
public:
  virtual bool operator==(const KillCursorsMessage& rhs) const PURE;

  virtual int32_t numberOfCursorIds() const PURE;
  virtual void numberOfCursorIds(int32_t number_of_cursors_ids) PURE;
  virtual const std::vector<int64_t>& cursorIds() const PURE;
  virtual void cursorIds(std::vector<int64_t>&& cursors_ids) PURE;
};

using KillCursorsMessagePtr = std::unique_ptr<KillCursorsMessage>;

/**
 * Mongo OP_QUERY message.
 */
class QueryMessage : public virtual Message {
public:
  struct Flags {
    // clang-format off
    static const int32_t TailableCursor  = 0x1 << 1;
    static const int32_t NoCursorTimeout = 0x1 << 4;
    static const int32_t AwaitData       = 0x1 << 5;
    static const int32_t Exhaust         = 0x1 << 6;
    // clang-format on
  };

  virtual bool operator==(const QueryMessage& rhs) const PURE;

  virtual int32_t flags() const PURE;
  virtual void flags(int32_t flags) PURE;
  virtual const std::string& fullCollectionName() const PURE;
  virtual void fullCollectionName(const std::string& name) PURE;
  virtual int32_t numberToSkip() const PURE;
  virtual void numberToSkip(int32_t skip) PURE;
  virtual int32_t numberToReturn() const PURE;
  virtual void numberToReturn(int32_t to_return) PURE;
  virtual const Bson::Document* query() const PURE;
  virtual void query(Bson::DocumentSharedPtr&& query) PURE;
  virtual const Bson::Document* returnFieldsSelector() const PURE;
  virtual void returnFieldsSelector(Bson::DocumentSharedPtr&& fields) PURE;
};

using QueryMessagePtr = std::unique_ptr<QueryMessage>;

/**
 * Mongo OP_REPLY
 */
class ReplyMessage : public virtual Message {
public:
  struct Flags {
    // clang-format off
    static const int32_t CursorNotFound = 0x1 << 0;
    static const int32_t QueryFailure   = 0x1 << 1;
    // clang-format on
  };

  virtual bool operator==(const ReplyMessage& rhs) const PURE;

  virtual int32_t flags() const PURE;
  virtual void flags(int32_t flags) PURE;
  virtual int64_t cursorId() const PURE;
  virtual void cursorId(int64_t cursor_id) PURE;
  virtual int32_t startingFrom() const PURE;
  virtual void startingFrom(int32_t starting_from) PURE;
  virtual int32_t numberReturned() const PURE;
  virtual void numberReturned(int32_t number_returned) PURE;
  virtual const std::list<Bson::DocumentSharedPtr>& documents() const PURE;
  virtual std::list<Bson::DocumentSharedPtr>& documents() PURE;
};

using ReplyMessagePtr = std::unique_ptr<ReplyMessage>;

class CommandMessage : public virtual Message {
public:
  // CommandMessage accessors.
  virtual bool operator==(const CommandMessage& rhs) const PURE;
  virtual std::string database() const PURE;
  virtual void database(std::string database) PURE;
  virtual std::string commandName() const PURE;
  virtual void commandName(std::string command_name) PURE;
  virtual const Bson::Document* metadata() const PURE;
  virtual void metadata(Bson::DocumentSharedPtr&& metadata) PURE;
  virtual const Bson::Document* commandArgs() const PURE;
  virtual void commandArgs(Bson::DocumentSharedPtr&& command_args) PURE;
  virtual const std::list<Bson::DocumentSharedPtr>& inputDocs() const PURE;
  virtual std::list<Bson::DocumentSharedPtr>& inputDocs() PURE;
};

using CommandMessagePtr = std::unique_ptr<CommandMessage>;

class CommandReplyMessage : public virtual Message {
public:
  virtual bool operator==(const CommandReplyMessage& rhs) const PURE;
  virtual const Bson::Document* metadata() const PURE;
  virtual void metadata(Bson::DocumentSharedPtr&& metadata) PURE;
  virtual const Bson::Document* commandReply() const PURE;
  virtual void commandReply(Bson::DocumentSharedPtr&& command_reply) PURE;
  virtual const std::list<Bson::DocumentSharedPtr>& outputDocs() const PURE;
  virtual std::list<Bson::DocumentSharedPtr>& outputDocs() PURE;
};

using CommandReplyMessagePtr = std::unique_ptr<CommandReplyMessage>;

/**
 * General callbacks for dispatching decoded mongo messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void decodeGetMore(GetMoreMessagePtr&& message) PURE;
  virtual void decodeInsert(InsertMessagePtr&& message) PURE;
  virtual void decodeKillCursors(KillCursorsMessagePtr&& message) PURE;
  virtual void decodeQuery(QueryMessagePtr&& message) PURE;
  virtual void decodeReply(ReplyMessagePtr&& message) PURE;
  virtual void decodeCommand(CommandMessagePtr&& message) PURE;
  virtual void decodeCommandReply(CommandReplyMessagePtr&& message) PURE;
};

/**
 * Mongo message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

/**
 * Mongo message encoder.
 */
class Encoder {
public:
  virtual ~Encoder() = default;

  virtual void encodeGetMore(const GetMoreMessage& message) PURE;
  virtual void encodeInsert(const InsertMessage& message) PURE;
  virtual void encodeKillCursors(const KillCursorsMessage& message) PURE;
  virtual void encodeQuery(const QueryMessage& message) PURE;
  virtual void encodeReply(const ReplyMessage& message) PURE;
  virtual void encodeCommand(const CommandMessage& message) PURE;
  virtual void encodeCommandReply(const CommandReplyMessage& message) PURE;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
