#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "source/extensions/filters/network/brpc_proxy/codec_impl.h"
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
	while(remaining || state_ == State::Init){
		switch(state_) {
		case State::Init: {
			ENVOY_LOG(trace, "parse new message:");
			msgptr_ = std::make_unique<BrpcMessage>();
			state_ = State::Head;
			msgptr_->pre_add(buffer);
			break;	
		}
		case State::Head:{
			ENVOY_LOG(trace, "parse header:");
			if(msgptr_->headerReady()){
				msgptr_->add();
				msgptr_->pre_add(buffer);
				msgptr_->onheaderComplete();
				state_ = State::Meta;
			} else {
				remaining--;
				buffer++;
				msgptr_->to_add();
			}
			break;				
		}
		case State::Meta:{
			ENVOY_LOG(trace, "parse meta:");
			if(msgptr_->metaReady()){
				msgptr_->add();
				msgptr_->pre_add(buffer);
				msgptr_->onmetaComplete();
				state_ = State::Body;
			} else {
				remaining--;
				buffer++;
				msgptr_->to_add();
			}			
			break;
		}
		case State::Body:{
			ENVOY_LOG(trace, "parse body:");
			if(msgptr_->bodyReady()){
				msgptr_->add();
				msgptr_->onbodyComplete();
				state_ = State::Complete;
			} else {
				remaining--;
				buffer++;
				msgptr_->to_add();
			}			
			break;			
		}
		case State::Complete: {
			callbacks_.onMessage(std::move(msgptr_));
			state_ = State::Init;
			break;
		}
		default:
			throw ProtocolError("invalid state");
		}
	}
	msgptr_->add();
}


void EncoderImpl::encode(BrpcMessage& value, Buffer::Instance& out) {
	out.move(value.get_buffer());
}


} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

