### Overview

QUICHE is integrated in the way described below:

A combination of QUIC session and connection serves as a Network::Connection instance. More than that, QUIC session manages all the QUIC streams. QUIC codec is a very thin layer between QUIC session and HCM. It doesn't do de-multiplexing stream management, but only provides interfaces for HCM to communicate with QUIC session.

QUIC's Http::StreamEncoder and Http::StreamDecoder implementation is decoupled, encoder is implemented by EnvoyQuicStream which is a QUIC stream and owned by session. HCM owns the decoder which can be accessed by QUIC stream instances. And decoder also knows about stream encoder.

### Request piepline

QUIC stream calls decodeHeaders() to deliver request headers. Request body needs
to be delivered after headers are delieverd as some QUICHE implementation allows
body to arrive earlier than headers. If not read disabled, always deliever available data via decodeData(). Stream
doesn't buffer any readable data in QUICHE stream buffers.

### Response pipeline

HCM will call encoder's encodeHeaders() to write response headers, and then encodeData() and encodeTrailers(). encodeData() calls WriteBodySlices() to write response body. Quic stream in QUICHE can config it's send buffer threshold QuicStream::buffered_data_threshold_ to be high enough to take all the data passed in, so stream can have unlimited buffer.

### Flow Control

## Receive buffer

All the arrived out-of-order data is buffered in QUICH stream. And this buffered
in capped by max stream flow control window in QUICHE which is 64MB. Once they are put
in sequence and ready to be used, OnBodyDataAvailable() is called. Our stream
implementation overrides this call and call StreamDecoder::decodeData() in it.
Request and response body is buffered in each L7 filter if desired, and the
stream itself doesn't buffer any of them if not set to be blocked.

When upstream or any L7 filter reaches its buffer limit, it call
Http::Stream::readDisable() with false to set QUIC stream in block state. In
this state, even more request/reponse body is available to be delivered,
OnBodyDataAvailable() will not be called. As a result, downstream flow contol
will not shift as no data will be consumed. As both filters and upstream buffer
can call readDisable(), each stream has a counter of how many
times the HCM wants to block the stream. Until the counter is cleared, stream
will resume its state to be unblocked and thus delivering any new and existing
available data in QUICHE stream.

## Send buffer

We use the unlimited stream send buffer in QUICHE along with a book keeping data structure `EnvoyQuicSimulatedWatermarkBffer` to server the purpose of WatermarkBuffer in Envoy to prevent buffering too much in QUICHE.

When the bytes buffered in a stream's send buffer exceeds its high watermark, its inherited method StreamCallbackHelper::runHighWatermarkCallbacks() is called. The buffered bytes will go below stream's low watermark as the stream writes out data gradually via QuicStream::OnCanWrite(). And in this case StreamCallbackHelper::runLowWatermarkCallbacks() will be called. QUICHE buffers all the data upon QuicSpdyStream::WriteBodySlices(), assuming `buffered_data_threshold_` is set high enough, and then writes all or part of them according to flow control window and congestion control window. To prevent transient changes in buffered bytes from triggering these two watermark callbacks one immediately after the other, encodeData() and OnCanWrite() only update the watermark book keeping once at the end if buffered bytes are changed.

QUICHE doesn't buffer data in connection. And all the data is buffered in stream.To prevent the case where all streams collectively buffers a lot of data, there is also a simulated watermark buffer for each Quic connection which changes upon each stream write.

When the aggregated buffered bytes goes above high watermark, its registered network callbacks will call Network::ConnectionCallbacks::onAboveWriteBufferHighWatermark(). HCM as a such callback will notify each stream via QUIC codec Http::Connection::onUnderlyingConnectionAboveWriteBufferHighWatermark() which will call each stream's StreamCallbackHelper::runHighWatermarkCallbacks(). There might be a way to simply the call stack as Quic connection already knows about all the stream, there is no need to call to HCM and notify each stream via codec. But here we just follow the same logic as HTTP2 codec does. Same way, any QuicStream::OnCanWrite() may change the aggregated buffered bytes in the connection level book keeping as well. If the buffered bytes goes down below low watermark, same calls will be triggered to propergate onBelowWriteBufferLowWatermark() to each stream callbacks.
