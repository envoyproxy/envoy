#include "extensions/filters/network/thrift_proxy/twitter_protocol_impl.h"

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/thrift_object_impl.h"
#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

struct StructNameValues {
  const std::string connectionOptionsStruct = "ConnectionOptions";
  const std::string requestHeaderStruct = "RequestHeader";
  const std::string clientIdStruct = "ClientId";
  const std::string delegationStruct = "Delegation";
  const std::string requestContextStruct = "RequestContext";
  const std::string responseHeaderStruct = "ResponseHeader";
  const std::string spanStruct = "Span";
  const std::string annotationStruct = "Annotation";
  const std::string binaryAnnotationStruct = "BinaryAnnotation";
  const std::string endpointStruct = "Endpoint";
  const std::string upgradeReplyStruct = "UpgradeReply";
};
using StructNames = ConstSingleton<StructNameValues>;

struct RequestHeaderFieldNameValues {
  const std::string traceIdField = "trace_id";
  const std::string spanIdField = "span_id";
  const std::string parentSpanIdField = "parent_span_id";
  const std::string sampledField = "sampled";
  const std::string clientIdField = "client_id";
  const std::string flagsField = "flags";
  const std::string contextsField = "contexts";
  const std::string destField = "dest";
  const std::string delegationsField = "delegations";
  const std::string traceIdHighField = "trace_id_high";
};
using RequestHeaderFieldNames = ConstSingleton<RequestHeaderFieldNameValues>;

struct ClientIdFieldNameValues {
  const std::string nameField = "name";
};
using ClientIdFieldNames = ConstSingleton<ClientIdFieldNameValues>;

struct DelegationFieldNameValues {
  const std::string srcField = "src";
  const std::string dstField = "dst";
};
using DelegationFieldNames = ConstSingleton<DelegationFieldNameValues>;

struct RequestContextFieldNameValues {
  const std::string keyField = "key";
  const std::string valueField = "value";
};
using RequestContextFieldNames = ConstSingleton<RequestContextFieldNameValues>;

struct ResponseHeaderFieldNameValues {
  const std::string spansField = "spans";
  const std::string contextsField = "contexts";
};
using ResponseHeaderFieldNames = ConstSingleton<ResponseHeaderFieldNameValues>;

struct SpanFieldNameValues {
  const std::string traceIdField = "trace_id";
  const std::string nameField = "name";
  const std::string idField = "id";
  const std::string parentIdField = "parent_id";
  const std::string annotationsField = "annotations";
  const std::string binaryAnnotationsField = "binary_annotations";
  const std::string debugField = "debug";
};
using SpanFieldNames = ConstSingleton<SpanFieldNameValues>;

struct AnnotationFieldNameValues {
  const std::string timestampField = "timestamp";
  const std::string valueField = "value";
  const std::string hostField = "host";
};
using AnnotationFieldNames = ConstSingleton<AnnotationFieldNameValues>;

struct BinaryAnnotationFieldNameValues {
  const std::string keyField = "key";
  const std::string valueField = "value";
  const std::string annotationTypeField = "annotation_type";
  const std::string hostField = "host";
};
using BinaryAnnotationFieldNames = ConstSingleton<BinaryAnnotationFieldNameValues>;

struct EndpointFieldNameValues {
  const std::string ipv4Field = "ipv4";
  const std::string portField = "port";
  const std::string serviceNameField = "service_name";
};
using EndpointFieldNames = ConstSingleton<EndpointFieldNameValues>;

const std::string& emptyString() { CONSTRUCT_ON_FIRST_USE(std::string, ""); }

/**
 * HeaderObjectProtocol implements BinaryProtocolImpl for the specific purpose of decoding the
 * Twitter protocol RequestHeader and ResponseHeader thrift structs. These appear after any
 * transport data (e.g. frame size) and before the start of a Thrift message. Decoding them
 * via a Protocol implementation allows us to reuse the Decoder and its state machine.
 */
class HeaderObjectProtocol : public BinaryProtocolImpl {
public:
  bool readMessageBegin(Buffer::Instance&, MessageMetadata&) override { return true; }
  bool readMessageEnd(Buffer::Instance&) override { return true; }
};

// Not const because the interfaces do not allow it, but these objects do not maintain internal
// state and are therefore not modifiable.
Transport& headerObjectTransport() {
  static UnframedTransportImpl* transport = new UnframedTransportImpl();
  return *transport;
}

Protocol& headerObjectProtocol() {
  static HeaderObjectProtocol* protocol = new HeaderObjectProtocol();
  return *protocol;
}

/**
 * ClientId is a Twitter protocol client identifier.
 *
 * See https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class ClientId {
public:
  ClientId(const std::string& name) : name_(name) {}
  ClientId(const ThriftStructValue& value) {
    for (const auto& field : value.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      if (field->fieldId() == NameFieldId) {
        name_ = field->getValue().getValueTyped<std::string>();
      }
    }
  }

  void write(Buffer::Instance& buffer) {
    Protocol& protocol = headerObjectProtocol();
    protocol.writeStructBegin(buffer, StructNames::get().clientIdStruct);

    // name
    protocol.writeFieldBegin(buffer, ClientIdFieldNames::get().nameField, FieldType::String,
                             NameFieldId);
    protocol.writeString(buffer, name_);
    protocol.writeFieldEnd(buffer);

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  static constexpr int16_t NameFieldId = 1;

  std::string name_;
};

/**
 * UpgradeReply represents Twitter protocol upgrade responses.
 */
class UpgradeReply : public DirectResponse, public ThriftObject {
public:
  UpgradeReply() = default;
  UpgradeReply(Transport& transport)
      : thrift_obj_(std::make_unique<ThriftObjectImpl>(transport, protocol_)) {}

  // DirectResponse
  DirectResponse::ResponseType encode(MessageMetadata& metadata, Protocol&,
                                      Buffer::Instance& buffer) const override {
    if (!metadata.hasSequenceId()) {
      metadata.setSequenceId(0);
    };

    metadata.setMethodName(TwitterProtocolImpl::upgradeMethodName());
    metadata.setMessageType(MessageType::Reply);

    // The upgrade response cannot have Twitter protocol headers, so ignore the caller's Protocol.
    BinaryProtocolImpl protocol;
    protocol.writeMessageBegin(buffer, metadata);

    // Per the Thrift standard, this is an invalid reply. We should start a reply struct with a
    // single field of id 0 (0x0B 0x00 0x00) to indicate success, followed by an empty UpgradeReply
    // struct (0x00), followed by a stop field for the reply struct (0x00). The finagle-twitter
    // implementation, however, just emits a single stop field.
    protocol.writeStructBegin(buffer, StructNames::get().upgradeReplyStruct);
    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);

    protocol.writeMessageEnd(buffer);

    return DirectResponse::ResponseType::SuccessReply;
  }

  // ThriftObject
  const ThriftFieldPtrList& fields() const override { return thrift_obj_->fields(); }
  bool onData(Buffer::Instance& buffer) override { return thrift_obj_->onData(buffer); }

private:
  BinaryProtocolImpl protocol_;
  ThriftObjectPtr thrift_obj_;
};

/**
 * ConnectionOptions is the Twitter protocol upgrade request. It is an empty struct.
 */
class ConnectionOptions : public ThriftStructValueImpl {
public:
  ConnectionOptions() : ThriftStructValueImpl(nullptr) {}
};

/**
 * RequestContext is a Twitter protocol request context (key/value pair).
 *
 * See https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class RequestContext {
public:
  RequestContext(const std::string& key, const std::string& value) : key_(key), value_(value) {}
  RequestContext(const ThriftStructValue& value) {
    for (const auto& field : value.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case 1:
        key_ = field->getValue().getValueTyped<std::string>();
        break;
      case 2:
        value_ = field->getValue().getValueTyped<std::string>();
        break;
      }
    }
  }

  void write(Buffer::Instance& buffer) const {
    Protocol& protocol = headerObjectProtocol();
    protocol.writeStructBegin(buffer, StructNames::get().requestContextStruct);

    // key
    protocol.writeFieldBegin(buffer, RequestContextFieldNames::get().keyField, FieldType::String,
                             KeyFieldId);
    protocol.writeString(buffer, key_);
    protocol.writeFieldEnd(buffer);

    // value
    protocol.writeFieldBegin(buffer, RequestContextFieldNames::get().valueField, FieldType::String,
                             ValueFieldId);
    protocol.writeString(buffer, value_);
    protocol.writeFieldEnd(buffer);

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  static constexpr int16_t KeyFieldId = 1;
  static constexpr int16_t ValueFieldId = 2;

  std::string key_;
  std::string value_;
};
using RequestContextList = std::list<RequestContext>;

/**
 * Delegation is Twitter protocol delegation table entry.
 *
 * See https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class Delegation {
public:
  Delegation(const std::string& src, const std::string& dst) : src_(src), dst_(dst) {}
  Delegation(const ThriftStructValue& value) {
    for (const auto& field : value.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case SrcFieldId:
        src_ = field->getValue().getValueTyped<std::string>();
        break;
      case DstFieldId:
        dst_ = field->getValue().getValueTyped<std::string>();
        break;
      }
    }
  }

  void write(Buffer::Instance& buffer) const {
    Protocol& protocol = headerObjectProtocol();
    protocol.writeStructBegin(buffer, StructNames::get().delegationStruct);

    // src
    protocol.writeFieldBegin(buffer, DelegationFieldNames::get().srcField, FieldType::String,
                             SrcFieldId);
    protocol.writeString(buffer, src_);
    protocol.writeFieldEnd(buffer);

    // dst
    protocol.writeFieldBegin(buffer, DelegationFieldNames::get().dstField, FieldType::String,
                             DstFieldId);
    protocol.writeString(buffer, dst_);
    protocol.writeFieldEnd(buffer);

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  static constexpr int16_t SrcFieldId = 1;
  static constexpr int16_t DstFieldId = 2;

  std::string src_;
  std::string dst_;
};
using DelegationList = std::list<Delegation>;

/**
 * RequestHeader is a Twitter protocol request header, inserted between the transport start and
 * message begin.
 *
 * See https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class RequestHeader {
public:
  RequestHeader(const ThriftObject& header) {
    for (const auto& field : header.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case TraceIdFieldId:
        trace_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      case SpanIdFieldId:
        span_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      case ParentSpanIdFieldId:
        parent_span_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      // unused: field 4
      case SampledFieldId:
        sampled_ = field->getValue().getValueTyped<bool>();
        break;
      case ClientIdFieldId:
        client_id_ = ClientId(field->getValue().getValueTyped<ThriftStructValue>());
        break;
      case FlagsFieldId:
        flags_ = field->getValue().getValueTyped<int64_t>();
        break;
      case ContextsFieldId:
        readContexts(field->getValue().getValueTyped<ThriftListValue>());
        break;
      case DestFieldId:
        dest_ = field->getValue().getValueTyped<std::string>();
        break;
      case DelegationsFieldId:
        readDelegations(field->getValue().getValueTyped<ThriftListValue>());
        break;
      case TraceIdHighFieldId:
        trace_id_high_ = field->getValue().getValueTyped<int64_t>();
        break;
      }
    }
  }

  RequestHeader(const MessageMetadata& metadata) {
    if (metadata.traceId()) {
      trace_id_ = *metadata.traceId();
    }
    if (metadata.traceIdHigh()) {
      trace_id_high_ = *metadata.traceIdHigh();
    }

    if (metadata.spanId()) {
      span_id_ = *metadata.spanId();
    }
    if (metadata.parentSpanId()) {
      parent_span_id_ = *metadata.parentSpanId();
    }

    if (metadata.flags()) {
      flags_ = *metadata.flags();
    }

    if (metadata.sampled().has_value()) {
      sampled_ = metadata.sampled().value();
    }

    metadata.headers().iterate([this](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      absl::string_view key = header.key().getStringView();
      if (key.empty()) {
        return Http::HeaderMap::Iterate::Continue;
      }

      if (key == Headers::get().ClientId.get()) {
        client_id_ = ClientId(std::string(header.value().getStringView()));
      } else if (key == Headers::get().Dest.get()) {
        dest_ = std::string(header.value().getStringView());
      } else if (key.find(":d:") == 0 && key.size() > 3) {
        delegations_.emplace_back(std::string(key.substr(3)),
                                  std::string(header.value().getStringView()));
      } else if (key[0] != ':') {
        contexts_.emplace_back(std::string(key), std::string(header.value().getStringView()));
      }
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  void write(Buffer::Instance& buffer) {
    Protocol& protocol = headerObjectProtocol();
    protocol.writeStructBegin(buffer, StructNames::get().requestHeaderStruct);

    // trace_id
    protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().traceIdField, FieldType::I64,
                             TraceIdFieldId);
    protocol.writeInt64(buffer, trace_id_);
    protocol.writeFieldEnd(buffer);

    // span_id
    protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().spanIdField, FieldType::I64,
                             SpanIdFieldId);
    protocol.writeInt64(buffer, span_id_);
    protocol.writeFieldEnd(buffer);

    // parent_span_id
    if (parent_span_id_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().parentSpanIdField,
                               FieldType::I64, ParentSpanIdFieldId);
      protocol.writeInt64(buffer, *parent_span_id_);
      protocol.writeFieldEnd(buffer);
    }

    // sampled
    if (sampled_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().sampledField, FieldType::Bool,
                               SampledFieldId);
      protocol.writeBool(buffer, *sampled_);
      protocol.writeFieldEnd(buffer);
    }

    // client_id
    if (client_id_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().clientIdField,
                               FieldType::Struct, ClientIdFieldId);
      client_id_->write(buffer);
      protocol.writeFieldEnd(buffer);
    }

    // flags
    if (flags_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().flagsField, FieldType::I64,
                               FlagsFieldId);
      protocol.writeInt64(buffer, *flags_);
      protocol.writeFieldEnd(buffer);
    }

    // contexts
    if (!contexts_.empty()) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().contextsField,
                               FieldType::List, ContextsFieldId);
      protocol.writeListBegin(buffer, FieldType::Struct, contexts_.size());
      for (const auto& context : contexts_) {
        context.write(buffer);
      }
      protocol.writeListEnd(buffer);
      protocol.writeFieldEnd(buffer);
    }

    // dest
    if (dest_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().destField, FieldType::String,
                               DestFieldId);
      protocol.writeString(buffer, *dest_);
      protocol.writeFieldEnd(buffer);
    }

    // delegations
    if (!delegations_.empty()) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().delegationsField,
                               FieldType::List, DelegationsFieldId);
      protocol.writeListBegin(buffer, FieldType::Struct, delegations_.size());
      for (const auto& delegation : delegations_) {
        delegation.write(buffer);
      }
      protocol.writeListEnd(buffer);
      protocol.writeFieldEnd(buffer);
    }

    // trace_id_high
    if (trace_id_high_) {
      protocol.writeFieldBegin(buffer, RequestHeaderFieldNames::get().traceIdHighField,
                               FieldType::I64, TraceIdHighFieldId);
      protocol.writeInt64(buffer, *trace_id_high_);
      protocol.writeFieldEnd(buffer);
    }

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  int64_t traceId() const { return trace_id_; }
  int64_t spanId() const { return span_id_; }
  absl::optional<int64_t> parentSpanId() const { return parent_span_id_; }
  absl::optional<bool> sampled() const { return sampled_; }
  absl::optional<ClientId> clientId() const { return client_id_; }
  absl::optional<int64_t> flags() const { return flags_; }
  const RequestContextList& contexts() const { return contexts_; }
  RequestContextList* contexts() { return &contexts_; }
  absl::optional<std::string> dest() { return dest_; }
  const DelegationList& delegations() const { return delegations_; }
  DelegationList* delegations() { return &delegations_; }
  absl::optional<int64_t> traceIdHigh() const { return trace_id_high_; }

private:
  static constexpr int16_t TraceIdFieldId = 1;
  static constexpr int16_t SpanIdFieldId = 2;
  static constexpr int16_t ParentSpanIdFieldId = 3;
  static constexpr int16_t SampledFieldId = 5;
  static constexpr int16_t ClientIdFieldId = 6;
  static constexpr int16_t FlagsFieldId = 7;
  static constexpr int16_t ContextsFieldId = 8;
  static constexpr int16_t DestFieldId = 9;
  static constexpr int16_t DelegationsFieldId = 10;
  static constexpr int16_t TraceIdHighFieldId = 11;

  void readContexts(const ThriftListValue& ctxts_list) {
    contexts_.clear();
    for (const auto& elem : ctxts_list.elements()) {
      const ThriftStructValue& ctxt_struct = elem->getValueTyped<ThriftStructValue>();
      contexts_.emplace_back(ctxt_struct);
    }
  }

  void readDelegations(const ThriftListValue& delegations_list) {
    delegations_.clear();
    for (const auto& elem : delegations_list.elements()) {
      const ThriftStructValue& ctxt_struct = elem->getValueTyped<ThriftStructValue>();
      delegations_.emplace_back(ctxt_struct);
    }
  }

  int64_t trace_id_{0};
  int64_t span_id_{0};
  absl::optional<int64_t> parent_span_id_;
  absl::optional<bool> sampled_;
  absl::optional<ClientId> client_id_;
  absl::optional<int64_t> flags_;
  std::list<RequestContext> contexts_;
  absl::optional<std::string> dest_;
  DelegationList delegations_;
  absl::optional<int64_t> trace_id_high_;
};

/**
 * ResponseHeader is a Twitter protocol response header, inserted between the transport start and
 * message begin.
 *
 * See https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class ResponseHeader {
public:
  ResponseHeader(const ThriftObject& header) {
    for (const auto& field : header.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case SpansFieldId:
        readSpans(field->getValue().getValueTyped<ThriftListValue>());
        break;
      case ContextsFieldId:
        readContexts(field->getValue().getValueTyped<ThriftListValue>());
        break;
      }
    }
  }
  ResponseHeader(const MessageMetadata& metadata) : spans_(metadata.spans()) {
    metadata.headers().iterate([this](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      absl::string_view key = header.key().getStringView();
      if (!key.empty() && key[0] != ':') {
        contexts_.emplace_back(std::string(key), std::string(header.value().getStringView()));
      }
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  void write(Buffer::Instance& buffer) {
    Protocol& protocol = headerObjectProtocol();
    protocol.writeStructBegin(buffer, StructNames::get().responseHeaderStruct);

    // spans
    if (!spans_.empty()) {
      protocol.writeFieldBegin(buffer, ResponseHeaderFieldNames::get().spansField, FieldType::List,
                               SpansFieldId);
      protocol.writeListBegin(buffer, FieldType::Struct, spans_.size());
      for (const auto& span : spans_) {
        writeSpan(buffer, span);
      }
      protocol.writeListEnd(buffer);
      protocol.writeFieldEnd(buffer);
    }

    // contexts
    if (!contexts_.empty()) {
      protocol.writeFieldBegin(buffer, ResponseHeaderFieldNames::get().contextsField,
                               FieldType::List, ContextsFieldId);
      protocol.writeListBegin(buffer, FieldType::Struct, contexts_.size());
      for (const auto& context : contexts_) {
        context.write(buffer);
      }
      protocol.writeListEnd(buffer);
      protocol.writeFieldEnd(buffer);
    }

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  SpanList& spans() { return spans_; }
  RequestContextList& contexts() { return contexts_; }

private:
  static constexpr int16_t SpansFieldId = 1;
  static constexpr int16_t ContextsFieldId = 2;

  static constexpr int16_t SpanTraceIdFieldId = 1;
  static constexpr int16_t SpanNameFieldId = 3;
  static constexpr int16_t SpanIdFieldId = 4;
  static constexpr int16_t SpanParentIdFieldId = 5;
  static constexpr int16_t SpanAnnotationsFieldId = 6;
  static constexpr int16_t SpanBinaryAnnotationsFieldId = 8;
  static constexpr int16_t SpanDebugFieldId = 9;

  static constexpr int16_t AnnotationTimestampFieldId = 1;
  static constexpr int16_t AnnotationValueFieldId = 2;
  static constexpr int16_t AnnotationHostFieldId = 3;

  static constexpr int16_t BinaryAnnotationKeyFieldId = 1;
  static constexpr int16_t BinaryAnnotationValueFieldId = 2;
  static constexpr int16_t BinaryAnnotationAnnotationTypeFieldId = 3;
  static constexpr int16_t BinaryAnnotationHostFieldId = 4;

  static constexpr int16_t EndpointIpv4FieldId = 1;
  static constexpr int16_t EndpointPortFieldId = 2;
  static constexpr int16_t EndpointServiceNameFieldId = 3;

  void readSpans(const ThriftListValue& spans_list) {
    spans_.clear();
    for (const auto& elem : spans_list.elements()) {
      spans_.emplace_back();
      readSpan(spans_.back(), elem->getValueTyped<ThriftStructValue>());
    }
  }

  void readSpan(Span& span, const ThriftStructValue& thrift_struct) {
    for (const auto& field : thrift_struct.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case SpanTraceIdFieldId:
        span.trace_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      // field 2: unused
      case SpanNameFieldId:
        span.name_ = field->getValue().getValueTyped<std::string>();
        break;
      case SpanIdFieldId:
        span.span_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      case SpanParentIdFieldId:
        span.parent_span_id_ = field->getValue().getValueTyped<int64_t>();
        break;
      case SpanAnnotationsFieldId:
        readAnnotations(span.annotations_, field->getValue().getValueTyped<ThriftListValue>());
        break;
      // field 7: unused
      case SpanBinaryAnnotationsFieldId:
        readBinaryAnnotations(span.binary_annotations_,
                              field->getValue().getValueTyped<ThriftListValue>());
        break;
      case SpanDebugFieldId:
        span.debug_ = field->getValue().getValueTyped<bool>();
        break;
      }
    }
  }

  void writeSpan(Buffer::Instance& buffer, const Span& span) {
    Protocol& protocol = headerObjectProtocol();

    protocol.writeStructBegin(buffer, StructNames::get().spanStruct);
    // trace_id
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().traceIdField, FieldType::I64,
                             SpanTraceIdFieldId);
    protocol.writeInt64(buffer, span.trace_id_);
    protocol.writeFieldEnd(buffer);

    // name
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().nameField, FieldType::String,
                             SpanNameFieldId);
    protocol.writeString(buffer, span.name_);
    protocol.writeFieldEnd(buffer);

    // id
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().idField, FieldType::I64, SpanIdFieldId);
    protocol.writeInt64(buffer, span.span_id_);
    protocol.writeFieldEnd(buffer);

    // parent_id
    if (span.parent_span_id_) {
      protocol.writeFieldBegin(buffer, SpanFieldNames::get().parentIdField, FieldType::I64,
                               SpanParentIdFieldId);
      protocol.writeInt64(buffer, *span.parent_span_id_);
      protocol.writeFieldEnd(buffer);
    }

    // annotations
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().annotationsField, FieldType::List,
                             SpanAnnotationsFieldId);
    protocol.writeListBegin(buffer, FieldType::Struct, span.annotations_.size());
    for (const auto& annotation : span.annotations_) {
      writeAnnotation(buffer, annotation);
    }
    protocol.writeListEnd(buffer);
    protocol.writeFieldEnd(buffer);

    // binary_annotations
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().binaryAnnotationsField, FieldType::List,
                             SpanBinaryAnnotationsFieldId);
    protocol.writeListBegin(buffer, FieldType::Struct, span.binary_annotations_.size());
    for (const auto& annotation : span.binary_annotations_) {
      writeBinaryAnnotation(buffer, annotation);
    }
    protocol.writeListEnd(buffer);
    protocol.writeFieldEnd(buffer);

    // debug
    protocol.writeFieldBegin(buffer, SpanFieldNames::get().debugField, FieldType::Bool,
                             SpanDebugFieldId);
    protocol.writeBool(buffer, span.debug_);
    protocol.writeFieldEnd(buffer);

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  void readAnnotations(AnnotationList& annotations, const ThriftListValue& thrift_list) {
    annotations.clear();
    for (const auto& elem : thrift_list.elements()) {
      annotations.emplace_back();
      readAnnotation(annotations.back(), elem->getValueTyped<ThriftStructValue>());
    }
  }

  void readAnnotation(Annotation& annotation, const ThriftStructValue& thrift_struct) {
    for (const auto& field : thrift_struct.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case AnnotationTimestampFieldId:
        annotation.timestamp_ = field->getValue().getValueTyped<int64_t>();
        break;
      case AnnotationValueFieldId:
        annotation.value_ = field->getValue().getValueTyped<std::string>();
        break;
      case AnnotationHostFieldId:
        annotation.host_.emplace();
        readEndpoint(annotation.host_.value(),
                     field->getValue().getValueTyped<ThriftStructValue>());
        break;
      }
    }
  }

  void writeAnnotation(Buffer::Instance& buffer, const Annotation& annotation) {
    Protocol& protocol = headerObjectProtocol();

    protocol.writeStructBegin(buffer, StructNames::get().annotationStruct);

    // timestamp
    protocol.writeFieldBegin(buffer, AnnotationFieldNames::get().timestampField, FieldType::I64,
                             AnnotationTimestampFieldId);
    protocol.writeInt64(buffer, annotation.timestamp_);
    protocol.writeFieldEnd(buffer);

    // value
    protocol.writeFieldBegin(buffer, AnnotationFieldNames::get().valueField, FieldType::String,
                             AnnotationValueFieldId);
    protocol.writeString(buffer, annotation.value_);
    protocol.writeFieldEnd(buffer);

    // endpoint
    if (annotation.host_) {
      protocol.writeFieldBegin(buffer, AnnotationFieldNames::get().hostField, FieldType::Struct,
                               AnnotationHostFieldId);
      writeEndpoint(buffer, *annotation.host_);
      protocol.writeFieldEnd(buffer);
    }

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  void readBinaryAnnotations(BinaryAnnotationList& annotations,
                             const ThriftListValue& thrift_list) {
    annotations.clear();
    for (const auto& elem : thrift_list.elements()) {
      annotations.emplace_back();
      readBinaryAnnotation(annotations.back(), elem->getValueTyped<ThriftStructValue>());
    }
  }

  void readBinaryAnnotation(BinaryAnnotation& annotation, const ThriftStructValue& thrift_struct) {
    for (const auto& field : thrift_struct.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case BinaryAnnotationKeyFieldId:
        annotation.key_ = field->getValue().getValueTyped<std::string>();
        break;
      case BinaryAnnotationValueFieldId:
        annotation.value_ = field->getValue().getValueTyped<std::string>();
        break;
      case BinaryAnnotationAnnotationTypeFieldId:
        annotation.annotation_type_ =
            static_cast<AnnotationType>(field->getValue().getValueTyped<int32_t>());
        break;
      case BinaryAnnotationHostFieldId:
        annotation.host_.emplace();
        readEndpoint(annotation.host_.value(),
                     field->getValue().getValueTyped<ThriftStructValue>());
        break;
      }
    }
  }

  void writeBinaryAnnotation(Buffer::Instance& buffer, const BinaryAnnotation& annotation) {
    Protocol& protocol = headerObjectProtocol();

    protocol.writeStructBegin(buffer, StructNames::get().binaryAnnotationStruct);

    // key
    protocol.writeFieldBegin(buffer, BinaryAnnotationFieldNames::get().keyField, FieldType::String,
                             BinaryAnnotationKeyFieldId);
    protocol.writeString(buffer, annotation.key_);
    protocol.writeFieldEnd(buffer);

    // value
    protocol.writeFieldBegin(buffer, BinaryAnnotationFieldNames::get().valueField,
                             FieldType::String, BinaryAnnotationValueFieldId);
    protocol.writeString(buffer, annotation.value_);
    protocol.writeFieldEnd(buffer);

    // annotation_type
    protocol.writeFieldBegin(buffer, BinaryAnnotationFieldNames::get().annotationTypeField,
                             FieldType::I32, BinaryAnnotationAnnotationTypeFieldId);
    protocol.writeInt32(buffer, static_cast<int32_t>(annotation.annotation_type_));
    protocol.writeFieldEnd(buffer);

    // endpoint
    if (annotation.host_) {
      protocol.writeFieldBegin(buffer, BinaryAnnotationFieldNames::get().hostField,
                               FieldType::Struct, BinaryAnnotationHostFieldId);
      writeEndpoint(buffer, *annotation.host_);
      protocol.writeFieldEnd(buffer);
    }

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  void readEndpoint(Endpoint& endpoint, const ThriftStructValue& thrift_struct) {
    for (const auto& field : thrift_struct.fields()) {
      // Unknown field id are ignored, to allow for future additional fields.
      switch (field->fieldId()) {
      case 1:
        endpoint.ipv4_ = field->getValue().getValueTyped<int32_t>();
        break;
      case 2:
        endpoint.port_ = field->getValue().getValueTyped<int16_t>();
        break;
      case 3:
        endpoint.service_name_ = field->getValue().getValueTyped<std::string>();
        break;
      }
    }
  }

  void writeEndpoint(Buffer::Instance& buffer, const Endpoint& endpoint) {
    Protocol& protocol = headerObjectProtocol();

    protocol.writeStructBegin(buffer, StructNames::get().endpointStruct);

    // ipv4
    protocol.writeFieldBegin(buffer, EndpointFieldNames::get().ipv4Field, FieldType::I32,
                             EndpointIpv4FieldId);
    protocol.writeInt32(buffer, endpoint.ipv4_);
    protocol.writeFieldEnd(buffer);

    // port
    protocol.writeFieldBegin(buffer, EndpointFieldNames::get().portField, FieldType::I16,
                             EndpointPortFieldId);
    protocol.writeInt16(buffer, endpoint.port_);
    protocol.writeFieldEnd(buffer);

    // service_name
    protocol.writeFieldBegin(buffer, EndpointFieldNames::get().serviceNameField, FieldType::String,
                             EndpointServiceNameFieldId);
    protocol.writeString(buffer, endpoint.service_name_);
    protocol.writeFieldEnd(buffer);

    protocol.writeFieldBegin(buffer, emptyString(), FieldType::Stop, 0);
    protocol.writeStructEnd(buffer);
  }

  void readContexts(const ThriftListValue& ctxts_list) {
    contexts_.clear();
    for (const auto& elem : ctxts_list.elements()) {
      const ThriftStructValue& ctxt_struct = elem->getValueTyped<ThriftStructValue>();
      contexts_.emplace_back(ctxt_struct);
    }
  }

  std::list<Span> spans_;
  std::list<RequestContext> contexts_;
};

} // namespace

bool TwitterProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  // If we see a normal binary protocol message with the improbable name on the first request
  // or response, we're upgrading to the TTwitter protocol.
  if (!upgraded_.has_value()) {
    if (!BinaryProtocolImpl::readMessageBegin(buffer, metadata)) {
      // Need more data.
      return false;
    }

    ASSERT(metadata.hasMethodName());
    if (metadata.methodName() == upgradeMethodName()) {
      metadata.setProtocolUpgradeMessage(true);
      return true;
    }

    upgraded_ = false;
    return true;
  }

  if (!upgraded_.value()) {
    // Fall back to regular binary protocol with no header object.
    return BinaryProtocolImpl::readMessageBegin(buffer, metadata);
  }

  // Upgraded protocol: consume RequestHeader or ResponseHeader.
  if (!header_complete_) {
    if (!header_) {
      header_ = std::make_unique<ThriftObjectImpl>(headerObjectTransport(), headerObjectProtocol());
    }
    header_complete_ = header_->onData(buffer);
    if (!header_complete_) {
      // Need more data.
      return false;
    }
  }

  if (!BinaryProtocolImpl::readMessageBegin(buffer, metadata)) {
    // Need more data.
    return false;
  }

  // Now that we know whether this is a request or a response, handle the header.
  ASSERT(metadata.hasMessageType());
  switch (metadata.messageType()) {
  case MessageType::Call:
  case MessageType::Oneway:
    updateMetadataWithRequestHeader(*header_, metadata);
    break;
  case MessageType::Reply:
  case MessageType::Exception:
    updateMetadataWithResponseHeader(*header_, metadata);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  header_complete_ = false;
  header_.reset();
  return true;
}

void TwitterProtocolImpl::writeMessageBegin(Buffer::Instance& buffer,
                                            const MessageMetadata& metadata) {
  if (upgraded_.value_or(false)) {
    switch (metadata.messageType()) {
    case MessageType::Call:
    case MessageType::Oneway:
      writeRequestHeader(buffer, metadata);
      break;
    case MessageType::Reply:
    case MessageType::Exception:
      writeResponseHeader(buffer, metadata);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  BinaryProtocolImpl::writeMessageBegin(buffer, metadata);
}

void TwitterProtocolImpl::updateMetadataWithRequestHeader(const ThriftObject& header_object,
                                                          MessageMetadata& metadata) {
  RequestHeader req_header(header_object);

  Http::HeaderMap& headers = metadata.headers();

  metadata.setTraceId(req_header.traceId());
  metadata.setSpanId(req_header.spanId());
  if (req_header.parentSpanId()) {
    metadata.setParentSpanId(*req_header.parentSpanId());
  }
  if (req_header.sampled()) {
    metadata.setSampled(*req_header.sampled());
  }
  if (req_header.clientId()) {
    headers.addReferenceKey(Headers::get().ClientId, req_header.clientId()->name_);
  }
  if (req_header.flags()) {
    metadata.setFlags(*req_header.flags());
  }
  for (const auto& context : *req_header.contexts()) {
    headers.addCopy(Http::LowerCaseString{context.key_}, context.value_);
  }
  if (req_header.dest()) {
    headers.addReferenceKey(Headers::get().Dest, *req_header.dest());
  }
  // TODO(zuercher): Delegations are stored as headers for now. Consider passing them as simple
  // objects
  for (const auto& delegation : *req_header.delegations()) {
    std::string key = fmt::format(":d:{}", delegation.src_);
    headers.addCopy(Http::LowerCaseString{key}, delegation.dst_);
  }
  if (req_header.traceIdHigh()) {
    metadata.setTraceIdHigh(*req_header.traceIdHigh());
  }
}

void TwitterProtocolImpl::writeRequestHeader(Buffer::Instance& buffer,
                                             const MessageMetadata& metadata) {
  RequestHeader req_header(metadata);
  req_header.write(buffer);
}

void TwitterProtocolImpl::updateMetadataWithResponseHeader(const ThriftObject& header_object,
                                                           MessageMetadata& metadata) {
  ResponseHeader resp_header(header_object);

  Http::HeaderMap& headers = metadata.headers();
  for (const auto& context : resp_header.contexts()) {
    headers.addCopy(Http::LowerCaseString(context.key_), context.value_);
  }

  SpanList& spans = resp_header.spans();
  std::copy(spans.begin(), spans.end(), std::back_inserter(metadata.mutableSpans()));
}

void TwitterProtocolImpl::writeResponseHeader(Buffer::Instance& buffer,
                                              const MessageMetadata& metadata) {
  ResponseHeader resp_header(metadata);
  resp_header.write(buffer);
}

ThriftObjectPtr TwitterProtocolImpl::newHeader() {
  return std::make_unique<ThriftObjectImpl>(headerObjectTransport(), headerObjectProtocol());
}

DecoderEventHandlerSharedPtr TwitterProtocolImpl::upgradeRequestDecoder() {
  return std::make_shared<ConnectionOptions>();
}

DirectResponsePtr TwitterProtocolImpl::upgradeResponse(const DecoderEventHandler& decoder) {
  ASSERT(dynamic_cast<const ConnectionOptions*>(&decoder) != nullptr);
  upgraded_ = true;
  return std::make_unique<UpgradeReply>();
};

ThriftObjectPtr TwitterProtocolImpl::attemptUpgrade(Transport& transport,
                                                    ThriftConnectionState& state,
                                                    Buffer::Instance& buffer) {
  // Check if we've already attempted to upgrade this connection.
  if (state.upgradeAttempted()) {
    upgraded_ = state.isUpgraded();
    return nullptr;
  }

  // Write upgrade request to buffer and return an object that can decode the response.
  MessageMetadata metadata;
  metadata.setMethodName(upgradeMethodName());
  metadata.setSequenceId(0);
  metadata.setMessageType(MessageType::Call);

  Buffer::OwnedImpl message;
  BinaryProtocolImpl::writeMessageBegin(message, metadata);
  writeStructBegin(message, StructNames::get().connectionOptionsStruct);
  writeFieldBegin(message, emptyString(), FieldType::Stop, 0);
  writeStructEnd(message);
  writeMessageEnd(message);
  transport.encodeFrame(buffer, metadata, message);

  return std::make_unique<UpgradeReply>(transport);
}

void TwitterProtocolImpl::completeUpgrade(ThriftConnectionState& state, ThriftObject& response) {
  UpgradeReply& upgrade_reply = dynamic_cast<UpgradeReply&>(response);

  if (upgrade_reply.fields().empty()) {
    state.markUpgraded();
    upgraded_ = true;
  } else {
    state.markUpgradeFailed();
    upgraded_ = false;
  }
}

bool TwitterProtocolImpl::isUpgradePrefix(Buffer::Instance& buffer) {
  // 12 bytes is the minimum length for the start of a binary protocol message.
  ASSERT(buffer.length() >= 12);

  // Must appear to be binary protocol.
  if (!isMagic(buffer.peekBEInt<uint16_t>())) {
    return false;
  }

  // Must have correct length message name length.
  if (buffer.peekBEInt<uint32_t>(4) != upgradeMethodName().length()) {
    return false;
  }

  // Given the fixed 8 bytes of message begin before the name, calculate how many bytes of message
  // name are available in the buffer.
  uint32_t available_len = static_cast<uint32_t>(
      std::min(static_cast<uint64_t>(upgradeMethodName().length()), buffer.length() - 8));
  ASSERT(available_len <= upgradeMethodName().length());
  ASSERT(buffer.length() >= available_len + 8);

  // Extract as much of the name as is available.
  absl::string_view available_name(
      static_cast<const char*>(buffer.linearize(available_len + 8)) + 8, available_len);

  absl::string_view full_name(upgradeMethodName());

  return full_name.compare(0, available_len, available_name) == 0;
}

class TwitterProtocolConfigFactory : public ProtocolFactoryBase<TwitterProtocolImpl> {
public:
  TwitterProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().TWITTER) {}
};

/**
 * Static registration for the Twitter protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(TwitterProtocolConfigFactory, NamedProtocolConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
