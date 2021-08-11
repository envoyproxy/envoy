### Overview

QUICHE is integrated in the way described below:

A combination of QUIC session and connection serves as a Network::Connection instance. More than that, QUIC session manages all the QUIC streams. The QUIC codec is a very thin layer between QUIC session and HCM. It doesn't do de-multiplexing or stream management, but only provides interfaces for the HCM to communicate with the QUIC session.

QUIC's Http::StreamEncoder and Http::StreamDecoder implementation is decoupled. The encoder is implemented by EnvoyQuicStream which is a QUIC stream and owned by session. The HCM owns the decoder which can be accessed by QUIC stream instances. And the decoder also knows about stream encoder.

### Request pipeline

The QUIC stream calls decodeHeaders() to deliver request headers. The request body needs to be delivered after headers are delivered as some QUICHE implementations allow the body to arrive earlier than headers. If not read disabled, it always deliver available data via decodeData(). The stream doesn't buffer any readable data in QUICHE stream buffers.

### Response pipeline

The HCM will call encoder's encodeHeaders() to write response headers, and then encodeData() and encodeTrailers(). encodeData() calls WriteBodySlices() to write out response body. The quic stream in QUICHE configures its send buffer threshold QuicStream::buffered_data_threshold_ to be high enough to take all the data passed in, so stream functionally has unlimited buffer.

### Flow Control

#### Receive buffer

All arrived out-of-order data is buffered in QUICHE stream. This buffer is capped by max stream flow control window in QUICHE which is 16MB. Once bytes are put in sequence and ready to be used, OnBodyDataAvailable() is called. The stream implementation overrides this call and calls StreamDecoder::decodeData() in it. Request and response body are buffered in each L7 filter if desired, and the stream itself doesn't buffer any of them unless set as read blocked.

When upstream or any L7 filter reaches its buffer limit, it will call Http::Stream::readDisable() with false to set QUIC stream to be read blocked. This takes effect in the next event loop. Once the stream is read blocked, even if more request/response body is available to be delivered, OnBodyDataAvailable() will not be called. As a result, downstream flow control will not shift as no data will be consumed. As both filters and upstream buffers can call readDisable(), each stream has a counter of how many times the HCM blocks the stream. When the counter is cleared, the stream will set its state to unblocked also in the next event loop and thus deliver any new and existing available data buffered in the QUICHE stream object. readDisable() can be called to block and unblock stream multiple times within one event loop, the stream blockage state is determined by the final state of the counter.

#### Send buffer

We use the unlimited stream send buffer in QUICHE along with a book keeping data structure `EnvoyQuicSimulatedWatermarkBuffer` to serve the function of WatermarkBuffer in Envoy to prevent buffering too much in QUICHE. The QUICHE send buffer stores bytes to be sent and bytes in flight. The former contributes to `EnvoyQuicSimulatedWatermarkBuffer`, and the latter contributes to congestion control window. The total memory is bounded by the high watermark of the connection and max congestion control window.

When the bytes buffered in a stream's send buffer exceeds its high watermark, its inherited method StreamCallbackHelper::runHighWatermarkCallbacks() is called. The buffered bytes will go below stream's low watermark as the stream writes out data gradually via QuicStream::OnCanWrite(). In this case StreamCallbackHelper::runLowWatermarkCallbacks() will be called. QUICHE buffers all the data upon QuicSpdyStream::WriteBodySlices(), assuming `buffered_data_threshold_` is set high enough, and then writes all or part of them according to flow control window and congestion control window. To prevent transient changes in buffered bytes from triggering these two watermark callbacks one immediately after the other, encodeData() and OnCanWrite() only update the watermark bookkeeping once at the end if buffered bytes are changed.

QUICHE doesn't buffer data at the local connection layer. All the data is buffered in the respective streams.To prevent the case where all streams collectively buffers a lot of data, there is also a simulated watermark buffer for each QUIC connection which is updated upon each stream write.

When the aggregated buffered bytes goes above high watermark, its registered network callbacks will call Network::ConnectionCallbacks::onAboveWriteBufferHighWatermark(). The HCM will notify each stream via QUIC codec Http::Connection::onUnderlyingConnectionAboveWriteBufferHighWatermark() which will call each stream's StreamCallbackHelper::runHighWatermarkCallbacks(). There might be a way to simply the call stack as Quic connection already knows about all the stream, there is no need to call to HCM and notify each stream via codec. But here we just follow the same logic as HTTP2 codec does. In the same way, any QuicStream::OnCanWrite() may change the aggregated buffered bytes in the connection level bookkeeping as well. If the buffered bytes goes down below the low watermark, the same calls will be triggered to propagate onBelowWriteBufferLowWatermark() to each stream.

As to Http::StreamEncoder::encodeHeaders()/encodeTrailers(), the accounting is done differently between Google QUIC and IETF QUIC:
 * In Google QUIC, encodeHeaders()/encodeTrailers() check the buffer size increase on header stream before and after writing headers/trailers. The bytes buffered in headers stream don't contribute to the data stream's send buffer watermark, but contribute to the connection's send buffer watermark. QuicSession::OnCanWrite() may drain header stream send buffer, so there we also check send buffer size decrease on header stream. Because there is no way to track which bytes in headers stream belongs to which data stream, if we had counted the send buffer increase in headers stream against the data stream on which encodeHeaders()/encodeTrailers() were called, there would be no way to discount these bytes when they were drained in OnCanWrite().
 * In IETF QUIC, encodeHeaders()/encodeTrailers() check the buffer size increase on the corresponding data stream which is similar to encodeData(). The buffered headers/trailers are only drained via QuicStream::OnCanWrite() so there is no need to check QuicSession::OnCanWrite.

When a stream gets closed due to an error, the send buffer might not be empty. In such case, these bytes are discounted from the send buffer watermarks of stream and connection to allow other streams to send. The send buffer will be released in the next event loop.

When a stream gets closed because FIN is sent and it also receives peer's FIN, the send buffer still stores in-flight data waiting to be ACKed. The stream object and its send buffer will be around till all its data are ACKed. In such case, the buffered bytes are discounted from the send buffer watermarks, but still consumes congestion control window. And the total amount of data buffered in such streams are bounded by congestion control window. And because of the congestion control window, there can be only a limited number of such streams. Plus, QUICHE will close the connection if there are too many outstanding packets. Although the connection can send more data in other streams or even open new streams, the total amount of buffer they can take is bounded by congestion control window and the send buffer watermarks.

### Debugging

QUICHE verbose logs (such as `QUICHE_VLOG(2)` for example) can be enabled by setting the `ENVOY_QUICHE_VERBOSITY` environment variable. For example, pass `--test_env="ENVOY_QUICHE_VERBOSITY=2"` to `bazel test`.
