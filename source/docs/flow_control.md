### Overview

Flow control in envoy is done by having limits on each buffer, and watermark callbacks.  When a
buffer contains more data than the configured limit, the high watermark callback will fire, kicking
off a chain of events which eventually informs the data source to stop sending data.  This back-off
may be immediate (stop reading from a socket) or gradual (stop HTTP/2 window updates) so all
buffer limits in Envoy are considered soft limits.  When the buffer eventually drains (generally to
half of of the high watermark to avoid thrashing back and forth) the low watermark callback will
fire, informing the sender it can resume sending data.

### TCP implementation details

TODO(alyssawilk) document the existing connection level flow control and configuration.

### HTTP2 implementation details

Because the various buffers in the HTTP/2 stack are fairly complicated, each path from a buffer
going over the watermark limit to disabling data from the data source is documented separately.

TODO(alyssawilk) snag diagram from H2 flow control google doc for various components.

For HTTP/2, when filters, streams, or connections back up, the end result is readDisable(true)
being called on the source stream.  This results in the stream ceasing to consume window, and so
not sending further flow control window updates to the peer.  This will result in the peer
eventually stopping sending data when the available window is consumed (or nghttp2 closing the
connection if the peer violates the flow control limit).  When readDisable(false) is called, any
outstanding unconsumed data is immediately consumed, which results in resuming window updates to the
peer and the resumption of data.

Note that readDisable() on the stream may be called by multiple entities.  It is called when any
filter buffers too much, when the upstream stream backs up and has too much data buffered, or the
upstream connection has too much data buffered.  Because of this, readDisable() manitains a count of
the number of times it has been disabled and only resumes reads when each caller has called the
equiavlent low watermark callback.

## HTTP/2 codec recv buffer

Given the HTTP/2 Envoy::Http::Http2 pending_recv_data\_ is processed immediately there's no real
need for buffer limits, but for consistency and to future-proof the implementation, it is a
WatermarkBuffer.  When the high watermark triggers, it calls
ConnectionImpl::StreamImpl::pendingRecvBufferHighWatermark() which calls readDisable(true) on the
stream.

When the buffer is drained, the WatermarkBuffer calls
ConnectionImpl::StreamImpl::pendingRecvBufferLowWatermark which calls readDisable(false) on the
stream.

## HTTP/2 filters

TODO(alyssawilk) implement and document.

# HTTP/2 codec upstream send buffer

The upstream send buffer Envoy::Http::Http pending_send_data\_ is H2 stream data destined for an
Envoy backend.   Data is added to this buffer after each filter in the chain is done processing,
and it backs up if there is insufficient connection or stream window to send the data.  When this
buffer backs up, the Watermark buffer calls
ConnectionImpl::StreamImpl::pendingSendBufferHighWatermark() which calls
StreamCallbackHelper::runHighWatermarkCallbacks().  This results in all subscribers of the
StreamCallbacks receiving onAboveWriteBufferHighWatermark() callback.  The key subscriber in this
case is Envoy::Router::Filter.  When the router filter receives the high
watermark callback it in turn calls
StreamDecoderFilterCallback::onDecoderFilterAboveWriteBufferHighWatermark().
This is picked up by Envoy::Http::ConnectionManagerImpl which can finally
call readDisable(true) on the downstream stream to stop data flowing from the downstream peer.

The low watermark path is the same.  When pending_send_data\_ is drained, the
Watermark buffer calls ConnectionImpl::StreamImpl::pendingSendBufferLowWatermark, which calls
StreamCallbackHelper::runLowWatermarkCallbacks.  This
kicks off StreamCallbacks::onAboveWriteBufferHighWatermark() which are picked up
the the router filter, which in turn calls
StreamDecoderFilterCallback::onDecoderFilterBelowWriteBufferLowWatermark which
causes the Envoy::Http::ConnectionManagerImpl to call readDisable(false) on the
downstream stream to resume the flow of data.

# HTTP/2 network upstream network buffer

The upstream network buffer is HTTP/2 data for all streams destined for the
Envoy backend.   When the Network::Connection has too much data buffered in write_buffer\_ it calls
the Network::ConnectionCallbacks onAboveWriteBufferHighWatermark().

The Envoy::Http::CodecClient is subscribed to this callback.  It informs the
Envoy::Http::Http2::ConnectionImpl of the event via ClientConnection::onAboveWriteBufferHighWatermark()

When the Envoy::Http::Http2::ConnectionImpl receives the high watermark callback it calls
runHighWatermarkCallbacks() for each stream on that connection.  From there on,
the code path is the same as the HTTP/2 codec upstream send buffer path above.
The stream calls the stream callbacks, the router receives them and passes them
to the connection manager which disables reads on the stream.

As always the low watermark path is the same.  The Network::Connection calls
onBelowWriteBufferLowWatermark() the Envoy::Http::CodecClient passes it to the codec via
ClientConnection::onBelowWriteBufferLowWatermark(), the Envoy::Http::Http2::ConnectionImpl passes the
change on to each stream, and so on.

# HTTP/2 codec downstream send buffer
# HTTP/2 network upstream network buffer

TODO(alyssawilk) implement and document.

### HTTP implementation details

TODO(alyssawilk) implement and document.
