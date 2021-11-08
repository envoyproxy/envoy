#pragma once

#include <cstdint>
#include <string>
#include <google/protobuf/message_lite.h>

#include "envoy/common/pure.h"
#include "common/common/assert.h"
#include "common/buffer/buffer_impl.h"
#include "envoy/extensions/filters/network/brpc_proxy/v3/brpc_meta.pb.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

static const char* BRPC_MAGIC_NUM = "PRPC";
static const int BRPC_HEAD_SIZE = 12;

/**
 *  * A brpc protocol error.
 *   */
class ProtocolError : public EnvoyException {
public:
	ProtocolError(const std::string& error) : EnvoyException(error) {}
};


class BrpcRequest {
public:
  virtual ~BrpcRequest() = default;

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

using BrpcRequestPtr = std::unique_ptr<BrpcRequest>;

/**
 * Decoder implementation of brpc protocol
 *
 * This implementation buffers when needed and will always consume all bytes passed for decoding.
 */

class BrpcMessage {
public:
	BrpcMessage():offset_(0),meta_size_(0),body_size_(0),request_id_(0),service_name_("echo"){}
	BrpcMessage(int error, std::string error_text, int64_t correlation_id){
		meta_.mutable_response()->set_error_code(error);
		meta_.mutable_response()->set_error_text(error_text);
		meta_.set_correlation_id(correlation_id);
		std::string s;
		meta_.SerializeToString(&s);
		meta_size_ = meta_.ByteSize();
		body_size_ = meta_size_;
		char buff[12];
		strncpy(buff, BRPC_MAGIC_NUM, 4);
#ifdef ABSL_IS_LITTLE_ENDIAN
		uint32_t* v1 = reinterpret_cast<uint32_t*>(buff+4);
		*v1 =   (((body_size_ & 0xFF) << 24) |
			((body_size_ & 0xFF00) << 8) |
			((body_size_ & 0xFF0000) >> 8) |
			((body_size_ & 0xFF000000) >> 24));
		uint32_t* v2 = reinterpret_cast<uint32_t*>(buff+8);
		*v2 =   (((meta_size_ & 0xFF) << 24) |
			((meta_size_ & 0xFF00) << 8) |
			((meta_size_ & 0xFF0000) >> 8) |
			((meta_size_ & 0xFF000000) >> 24));

#else
		uint32_t* v1 = reinterpret_cast<uint32_t*>(buff+4);
		uint32_t* v2 = reinterpret_cast<uint32_t*>(buff+8);
		*v1 = body_size_;
		*v2 = meta_size_;
#endif
		msg_.add(buff, 12);
		msg_.add(s.data(), s.size());
		request_id_ = correlation_id;
		
	}
	Buffer::Instance& get_buffer(){
		return msg_;
	}
	bool headerReady(uint32_t to_add){
		return to_add + offset_ >= BRPC_HEAD_SIZE;
	}
	void onheaderComplete(){
		if(!msg_.startsWith(BRPC_MAGIC_NUM)){
			throw ProtocolError("invalid brpc protocol");
		}
		char buf[8];
		msg_.copyOut(4, 8, buf);
#ifdef ABSL_IS_LITTLE_ENDIAN
		body_size_ = *(reinterpret_cast<uint32_t*>(buf));
		body_size_ =  ((body_size_ & 0xff00ff00UL) >> 8) | ((body_size_ & 0x00ff00ffUL) << 8);
		body_size_ = (body_size_ >> 16) | (body_size_ << 16);
		meta_size_ = *(reinterpret_cast<uint32_t*>(buf+4));
		meta_size_ =  ((meta_size_ & 0xff00ff00UL) >> 8) | ((meta_size_ & 0x00ff00ffUL) << 8);
		meta_size_ = (meta_size_ >> 16) | (meta_size_ << 16);
#else
		body_size_ = *(uint32_t*)(buf);
		meta_size_ = *(uint32_t*)(buf+4);
#endif
	}
	
	bool metaReady(uint32_t to_add){
		return to_add + offset_ >= BRPC_HEAD_SIZE + meta_size_;
	}
	void onmetaComplete(){
		std::unique_ptr<unsigned char[]> buf(new unsigned char[meta_size_]);;
		msg_.copyOut(BRPC_HEAD_SIZE, meta_size_, buf.get());
		meta_.ParseFromArray(buf.get(), meta_size_);
		request_id_ = meta_.correlation_id();
	}
	
	bool bodyReady(uint32_t to_add){
		return to_add + offset_ == BRPC_HEAD_SIZE + body_size_;
	}

	void onbodyReady(){
		return;
	}
	void onbodyComplete(){}
	void append(const char* buf, int size){
		msg_.add(buf, size);
		offset_ += size;
	}
	int64_t request_id() const {return request_id_;}
	std::string service_name() const {return service_name_;}
	bool has_meta(){return meta_size_>0;}
	bool has_body(){return body_size_>meta_size_;}
private:
	brpc::policy::RpcMeta meta_;
	Envoy::Buffer::OwnedImpl msg_;
	uint32_t offset_;
	uint32_t meta_size_;
	uint32_t body_size_;
	int64_t request_id_;
	std::string service_name_;
};

using BrpcMessagePtr = std::unique_ptr<BrpcMessage>;
using BrpcMessageSharedPtr = std::shared_ptr<BrpcMessage>;
using BrpcMessageConstSharedPtr = std::shared_ptr<const BrpcMessage>;

/**
 *  * Outbound request callbacks.
 *   */
class PoolCallbacks {
public:
	virtual ~PoolCallbacks() = default;
	virtual void onResponse(BrpcMessagePtr&& value) PURE;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

