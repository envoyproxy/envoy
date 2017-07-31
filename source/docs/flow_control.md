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
WatermarkBuffer.   The high watermark path goes as follows:

 * When pending_recv_data\_ has too much data, the WatermarkBuffer calls
 ConnectionImpl::StreamImpl::pendingRecvBufferHighWatermark()
 * pendingRecvBufferHighWatermark calls readDisable(true) on the stream.

The low watermark path is similar

 * When pending_recv_data\_ is drained, the WatermarkBuffer calls
 ConnectionImpl::StreamImpl::pendingRecvBufferLowWatermark
 * pendingRecvBufferLowWatermarkwhich calls readDisable(false) on the stream.

## HTTP/2 filters

TODO(alyssawilk) implement and document.

# HTTP/2 codec upstream send buffer

The upstream send buffer Envoy::Http::Http pending_send_data\_ is H2 stream data destined for an
Envoy backend.   Data is added to this buffer after each filter in the chain is done processing,
and it backs up if there is insufficient connection or stream window to send the data.  The high
watermark path goes as follows:

 * When  pending_send_data\_ has too much data it calls
   ConnectionImpl::StreamImpl::pendingSendBufferHighWatermark()
 * pendingSendBufferHighWatermark() calls StreamCallbackHelper::runHighWatermarkCallbacks()
 * runHighWatermarkCallbacks() results in all subscribers of StreamCallbacks receiving a
   onAboveWriteBufferHighWatermark() callback.
 * When Envoy::Router::Filter receives onAboveWriteBufferHighWatermark() it
   calls StreamDecoderFilterCallback::onDecoderFilterAboveWriteBufferHighWatermark().
 * When Envoy::Http::ConnectionManagerImpl receives onDecoderFilterAboveWriteBufferHighWatermark()
   it calls readDisable(true) on the downstream stream to pause data.

For the low watermark path:

 * When pending_send_data\_ drains it calls
   ConnectionImpl::StreamImpl::pendingSendBufferLowWatermark()
 * pendingSendBufferLowWatermark() calls StreamCallbackHelper::runLowWatermarkCallbacks()
 * runLowWatermarkCallbacks() results in all subscribers of StreamCallbacks receiving a
   onBelowWriteBufferLowWatermark() callback.
 * When Envoy::Router::Filter receives onBelowWriteBufferLowWatermark() it
   calls StreamDecoderFilterCallback::onDecoderFilterBelowWriteBufferLowWatermark().
 * When Envoy::Http::ConnectionManagerImpl receives onDecoderFilterBelowWriteBufferLowWatermark()
   it calls readDisable(false) on the downstream stream to resume data.

# HTTP/2 network upstream network buffer

The upstream network buffer is HTTP/2 data for all streams destined for the
Envoy backend.   The high watermark path is as follows:

 * When Envoy::Network::ConnectionImpl write_buffer\_ has too much data it calls
   Network::ConnectionCallbacks::onAboveWriteBufferHighWatermark().
 * When Envoy::Http::CodecClient receives onAboveWriteBufferHighWatermark() it
   calls onAboveWriteBufferHighWatermark() on codec\_.
 * When Envoy::Http::Http2::ConnectionImpl receives onAboveWriteBufferHighWatermark() it calls
   runHighWatermarkCallbacks() for each stream of the connection.
 * runHighWatermarkCallbacks() results in all subscribers of StreamCallbacks receiving a
   onAboveWriteBufferHighWatermark() callback.
 * When Envoy::Router::Filter receives onAboveWriteBufferHighWatermark() it
   calls StreamDecoderFilterCallback::onDecoderFilterAboveWriteBufferHighWatermark().
 * When Envoy::Http::ConnectionManagerImpl receives onDecoderFilterAboveWriteBufferHighWatermark()
   it calls readDisable(true) on the downstream stream to pause data.

The low watermark path is as follows:

 * When Envoy::Network::ConnectionImpl write_buffer\_ is drained it calls
   Network::ConnectionCallbacks::onBelowWriteBufferLowWatermark().
 * When Envoy::Http::CodecClient receives onBelowWriteBufferLowWatermark() it
   calls onBelowWriteBufferLowWatermark() on codec\_.
 * When Envoy::Http::Http2::ConnectionImpl receives onBelowWriteBufferLowWatermark() it calls
   runLowWatermarkCallbacks() for each stream of the connection.
 * runLowWatermarkCallbacks() results in all subscribers of StreamCallbacks receiving a
   onBelowWriteBufferLowWatermark() callback.
 * When Envoy::Router::Filter receives onBelowWriteBufferLowWatermark() it
   calls StreamDecoderFilterCallback::onDecoderFilterBelowWriteBufferLowWatermark().
 * When Envoy::Http::ConnectionManagerImpl receives onDecoderFilterBelowWriteBufferLowWatermark()
   it calls readDisable(false) on the downstream stream to resume data.

# HTTP/2 codec downstream send buffer
# HTTP/2 network upstream network buffer

TODO(alyssawilk) implement and document.

### HTTP implementation details

TODO(alyssawilk) implement and document.
