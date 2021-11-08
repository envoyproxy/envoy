#include "common/common/assert.h"
#include "common/common/logger.h"
#include "extensions/filters/network/brpc_proxy/codec_impl.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

void DecoderImpl::decode(Buffer::Instance& data) {
	for (const Buffer::RawSlice& slice : data.getRawSlices()) {
		parseSlice(slice);
	}
	data.drain(data.length());
}

void DecoderImpl::parseSlice(const Buffer::RawSlice& slice) {
	const char* buffer = reinterpret_cast<const char*>(slice.mem_);
	int64_t remaining = slice.len_;
	int64_t offset = 0;
	while(remaining || state_ == State::Complete){
		switch(state_) {
		case State::Init: {
			ENVOY_LOG(trace, "parse new message: remaining{}",remaining);
			msgptr_ = std::make_unique<BrpcMessage>();
			state_ = State::Head;
			break;	
		}
		case State::Head:{
			ENVOY_LOG(trace, "parse header: remaining {}",remaining);
			remaining--;
			offset++;
			if(msgptr_->headerReady(offset)){
				msgptr_->append(buffer,offset);
				msgptr_->onheaderComplete();
				buffer += offset;
				offset = 0;
				state_ = State::Meta;
				if(!msgptr_->has_meta()){
					state_ = State::Body;
					if(!msgptr_->has_body()){
						state_ = State::Complete;
					}
				}
			}
			break;
		}
		case State::Meta:{
                        ENVOY_LOG(trace, "parse meta: remaining {}", remaining);
                        remaining--;
                        offset++;
                        if(msgptr_->metaReady(offset)){
                                msgptr_->append(buffer,offset);
                               	msgptr_->onmetaComplete();
                                buffer += offset;
                                offset = 0;
                                state_ = State::Body;
				if(!msgptr_->has_body()){
					state_ = State::Complete;
				}
			}
			break;
		}
		case State::Body:{
			ENVOY_LOG(trace, "parse body: remaining{}",remaining);
			remaining--;
			offset++;
			if(msgptr_->bodyReady(offset)){
				msgptr_->append(buffer,offset);
				msgptr_->onbodyComplete();
				buffer += offset;
				offset = 0;
				state_ = State::Complete;
			}
			break;
		}
		case State::Complete: {
			ENVOY_LOG(trace, "parse complete:"); 
			callbacks_.onMessage(std::move(msgptr_));
			state_ = State::Init;
			break;
		}
		default:
			throw ProtocolError("invalid state");
		}
	}
	if(offset){
		msgptr_->append(buffer,offset);
	}
}


void EncoderImpl::encode(BrpcMessage& value, Buffer::Instance& out) {
	out.move(value.get_buffer());
}


} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

