#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/mongo/bson.h"

namespace Envoy {
/**
 * General implementation of https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/
 */
namespace Mongo {

/**
 * Base class for all mongo messages.
 */
class Message {
public:
  enum class OpCode {
    OP_REPLY = 1,
    OP_MSG = 1000,
    OP_UPDATE = 2001,
    OP_INSERT = 2002,
    OP_QUERY = 2004,
    OP_GET_MORE = 2005,
    OP_DELETE = 2006,
    OP_KILL_CURSORS = 2007
  };

  virtual ~Message(){};

  virtual int32_t requestId() const PURE;
  virtual int32_t responseTo() const PURE;
  virtual std::string toString(bool full) const PURE;
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

typedef std::unique_ptr<GetMoreMessage> GetMoreMessagePtr;

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

typedef std::unique_ptr<InsertMessage> InsertMessagePtr;

/**
 * Mongo OP_KILL_CURSORS message.
 */
class KillCursorsMessage : public virtual Message {
public:
  virtual bool operator==(const KillCursorsMessage& rhs) const PURE;

  virtual int32_t numberOfCursorIds() const PURE;
  virtual void numberOfCursorIds(int32_t number_of_cursors_ids_) PURE;
  virtual const std::vector<int64_t>& cursorIds() const PURE;
  virtual void cursorIds(std::vector<int64_t>&& cursors_ids) PURE;
};

typedef std::unique_ptr<KillCursorsMessage> KillCursorsMessagePtr;

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

typedef std::unique_ptr<QueryMessage> QueryMessagePtr;

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

typedef std::unique_ptr<ReplyMessage> ReplyMessagePtr;

/**
 * General callbacks for dispatching decoded mongo messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  virtual void decodeGetMore(GetMoreMessagePtr&& message) PURE;
  virtual void decodeInsert(InsertMessagePtr&& message) PURE;
  virtual void decodeKillCursors(KillCursorsMessagePtr&& message) PURE;
  virtual void decodeQuery(QueryMessagePtr&& message) PURE;
  virtual void decodeReply(ReplyMessagePtr&& message) PURE;
};

/**
 * Mongo message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() {}

  virtual void onData(Buffer::Instance& data) PURE;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

/**
 * Mongo message encoder.
 */
class Encoder {
public:
  virtual ~Encoder() {}

  virtual void encodeGetMore(const GetMoreMessage& message) PURE;
  virtual void encodeInsert(const InsertMessage& message) PURE;
  virtual void encodeKillCursors(const KillCursorsMessage& message) PURE;
  virtual void encodeQuery(const QueryMessage& message) PURE;
  virtual void encodeReply(const ReplyMessage& message) PURE;
};

} // namespace Mongo
} // namespace Envoy
