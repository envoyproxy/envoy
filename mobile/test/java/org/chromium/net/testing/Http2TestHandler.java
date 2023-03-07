package org.chromium.net.testing;

import static io.netty.buffer.Unpooled.copiedBuffer;
import static io.netty.buffer.Unpooled.unreleasableBuffer;
import static io.netty.handler.codec.http.HttpResponseStatus.BAD_REQUEST;
import static io.netty.handler.codec.http.HttpResponseStatus.OK;
import static io.netty.handler.logging.LogLevel.INFO;

import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufUtil;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.http2.AbstractHttp2ConnectionHandlerBuilder;
import io.netty.handler.codec.http2.DefaultHttp2Headers;
import io.netty.handler.codec.http2.Http2ConnectionDecoder;
import io.netty.handler.codec.http2.Http2ConnectionEncoder;
import io.netty.handler.codec.http2.Http2ConnectionHandler;
import io.netty.handler.codec.http2.Http2Exception;
import io.netty.handler.codec.http2.Http2Flags;
import io.netty.handler.codec.http2.Http2FrameListener;
import io.netty.handler.codec.http2.Http2FrameLogger;
import io.netty.handler.codec.http2.Http2Headers;
import io.netty.handler.codec.http2.Http2Settings;
import io.netty.util.CharsetUtil;

/**
 * HTTP/2 test handler for Cronet BidirectionalStream tests.
 */
public final class Http2TestHandler extends Http2ConnectionHandler implements Http2FrameListener {
  // Some Url Paths that have special meaning.
  public static final String ECHO_ALL_HEADERS_PATH = "/echoallheaders";
  public static final String ECHO_HEADER_PATH = "/echoheader";
  public static final String ECHO_METHOD_PATH = "/echomethod";
  public static final String ECHO_STREAM_PATH = "/echostream";
  public static final String ECHO_TRAILERS_PATH = "/echotrailers";
  public static final String SERVE_SIMPLE_BROTLI_RESPONSE = "/simplebrotli";
  public static final String REPORTING_COLLECTOR_PATH = "/reporting-collector";
  public static final String SUCCESS_WITH_NEL_HEADERS_PATH = "/success-with-nel";
  public static final String COMBINED_HEADERS_PATH = "/combinedheaders";
  public static final String HANGING_REQUEST_PATH = "/hanging-request";

  private static final String TAG = Http2TestHandler.class.getSimpleName();
  private static final Http2FrameLogger sLogger =
      new Http2FrameLogger(INFO, Http2TestHandler.class);
  private static final ByteBuf RESPONSE_BYTES =
      unreleasableBuffer(copiedBuffer("HTTP/2 Test Server", CharsetUtil.UTF_8));

  private HashMap<Integer, RequestResponder> mResponderMap = new HashMap<>();

  private ReportingCollector mReportingCollector;
  private String mServerUrl;
  private CountDownLatch mHangingUrlLatch;

  /**
   * Builder for HTTP/2 test handler.
   */
  public static final class Builder
      extends AbstractHttp2ConnectionHandlerBuilder<Http2TestHandler, Builder> {
    public Builder() { frameLogger(sLogger); }

    public Builder setReportingCollector(ReportingCollector reportingCollector) {
      mReportingCollector = reportingCollector;
      return this;
    }

    public Builder setServerUrl(String serverUrl) {
      mServerUrl = serverUrl;
      return this;
    }

    public Builder setHangingUrlLatch(CountDownLatch hangingUrlLatch) {
      mHangingUrlLatch = hangingUrlLatch;
      return this;
    }

    @Override
    public Http2TestHandler build() {
      return super.build();
    }

    @Override
    protected Http2TestHandler build(Http2ConnectionDecoder decoder, Http2ConnectionEncoder encoder,
                                     Http2Settings initialSettings) {
      Http2TestHandler handler = new Http2TestHandler(
          decoder, encoder, initialSettings, mReportingCollector, mServerUrl, mHangingUrlLatch);
      frameListener(handler);
      return handler;
    }

    private ReportingCollector mReportingCollector;
    private String mServerUrl;
    private CountDownLatch mHangingUrlLatch;
  }

  private class RequestResponder {
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      encoder().writeHeaders(ctx, streamId, createResponseHeadersFromRequestHeaders(headers), 0,
                             endOfStream, ctx.newPromise());
      ctx.flush();
    }

    int onDataRead(ChannelHandlerContext ctx, int streamId, ByteBuf data, int padding,
                   boolean endOfStream) {
      int processed = data.readableBytes() + padding;
      encoder().writeData(ctx, streamId, data.retain(), 0, true, ctx.newPromise());
      ctx.flush();
      return processed;
    }

    void sendResponseString(ChannelHandlerContext ctx, int streamId, String responseString) {
      ByteBuf content = ctx.alloc().buffer();
      ByteBufUtil.writeAscii(content, responseString);
      encoder().writeHeaders(ctx, streamId, createDefaultResponseHeaders(), 0, false,
                             ctx.newPromise());
      encoder().writeData(ctx, streamId, content, 0, true, ctx.newPromise());
      ctx.flush();
    }
  }

  private class EchoStreamResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      // Send a frame for the response headers.
      encoder().writeHeaders(ctx, streamId, createResponseHeadersFromRequestHeaders(headers), 0,
                             endOfStream, ctx.newPromise());
      ctx.flush();
    }

    @Override
    int onDataRead(ChannelHandlerContext ctx, int streamId, ByteBuf data, int padding,
                   boolean endOfStream) {
      int processed = data.readableBytes() + padding;
      encoder().writeData(ctx, streamId, data.retain(), 0, endOfStream, ctx.newPromise());
      ctx.flush();
      return processed;
    }
  }

  private class CombinedHeadersResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      ByteBuf content = ctx.alloc().buffer();
      ByteBufUtil.writeAscii(content, "GET");
      Http2Headers responseHeaders = new DefaultHttp2Headers().status(OK.codeAsText());
      // Upon receiving, the following two headers will be jointed by '\0'.
      responseHeaders.add("foo", "bar");
      responseHeaders.add("foo", "bar2");
      encoder().writeHeaders(ctx, streamId, responseHeaders, 0, false, ctx.newPromise());
      encoder().writeData(ctx, streamId, content, 0, true, ctx.newPromise());
      ctx.flush();
    }
  }

  private class HangingRequestResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      try {
        mHangingUrlLatch.await();
      } catch (InterruptedException e) {
      }
    }
  }

  private class EchoHeaderResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      String[] splitPath = headers.path().toString().split("\\?");
      if (splitPath.length <= 1) {
        sendResponseString(ctx, streamId, "Header name not found.");
        return;
      }

      String headerName = splitPath[1].toLowerCase(Locale.US);
      if (headers.get(headerName) == null) {
        sendResponseString(ctx, streamId, "Header not found:" + headerName);
        return;
      }

      sendResponseString(ctx, streamId, headers.get(headerName).toString());
    }
  }

  private class EchoAllHeadersResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      StringBuilder response = new StringBuilder();
      for (Map.Entry<CharSequence, CharSequence> header : headers) {
        response.append(header.getKey() + ": " + header.getValue() + "\r\n");
      }
      sendResponseString(ctx, streamId, response.toString());
    }
  }

  private class EchoMethodResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      sendResponseString(ctx, streamId, headers.method().toString());
    }
  }

  private class EchoTrailersResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      encoder().writeHeaders(ctx, streamId, createDefaultResponseHeaders(), 0, false,
                             ctx.newPromise());
      encoder().writeData(ctx, streamId, RESPONSE_BYTES.duplicate(), 0, false, ctx.newPromise());
      Http2Headers responseTrailers =
          createResponseHeadersFromRequestHeaders(headers).add("trailer", "value1", "Value2");
      encoder().writeHeaders(ctx, streamId, responseTrailers, 0, true, ctx.newPromise());
      ctx.flush();
    }
  }

  // A RequestResponder that serves a simple Brotli-encoded response.
  private class ServeSimpleBrotliResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      Http2Headers responseHeaders = new DefaultHttp2Headers().status(OK.codeAsText());
      byte[] quickfoxCompressed = {0x0b, 0x15, -0x80, 0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69,
                                   0x63, 0x6b, 0x20,  0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66,
                                   0x6f, 0x78, 0x20,  0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f,
                                   0x76, 0x65, 0x72,  0x20, 0x74, 0x68, 0x65, 0x20, 0x6c, 0x61,
                                   0x7a, 0x79, 0x20,  0x64, 0x6f, 0x67, 0x03};
      ByteBuf content = copiedBuffer(quickfoxCompressed);
      responseHeaders.add("content-encoding", "br");
      encoder().writeHeaders(ctx, streamId, responseHeaders, 0, false, ctx.newPromise());
      encoder().writeData(ctx, streamId, content, 0, true, ctx.newPromise());
      ctx.flush();
    }
  }

  // A RequestResponder that implements a Reporting collector.
  private class ReportingCollectorResponder extends RequestResponder {
    private ByteArrayOutputStream mPartialPayload = new ByteArrayOutputStream();

    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {}

    @Override
    int onDataRead(ChannelHandlerContext ctx, int streamId, ByteBuf data, int padding,
                   boolean endOfStream) {
      int processed = data.readableBytes() + padding;
      try {
        data.readBytes(mPartialPayload, data.readableBytes());
      } catch (IOException e) {
      }
      if (endOfStream) {
        processPayload(ctx, streamId);
      }
      return processed;
    }

    private void processPayload(ChannelHandlerContext ctx, int streamId) {
      boolean succeeded = false;
      try {
        String payload = mPartialPayload.toString(CharsetUtil.UTF_8.name());
        succeeded = mReportingCollector.addReports(payload);
      } catch (UnsupportedEncodingException e) {
      }
      Http2Headers responseHeaders;
      if (succeeded) {
        responseHeaders = new DefaultHttp2Headers().status(OK.codeAsText());
      } else {
        responseHeaders = new DefaultHttp2Headers().status(BAD_REQUEST.codeAsText());
      }
      encoder().writeHeaders(ctx, streamId, responseHeaders, 0, true, ctx.newPromise());
      ctx.flush();
    }
  }

  // A RequestResponder that serves a successful response with Reporting and NEL headers
  private class SuccessWithNELHeadersResponder extends RequestResponder {
    @Override
    void onHeadersRead(ChannelHandlerContext ctx, int streamId, boolean endOfStream,
                       Http2Headers headers) {
      Http2Headers responseHeaders = new DefaultHttp2Headers().status(OK.codeAsText());
      responseHeaders.add("report-to", getReportToHeader());
      responseHeaders.add("nel", getNELHeader());
      encoder().writeHeaders(ctx, streamId, responseHeaders, 0, true, ctx.newPromise());
      ctx.flush();
    }

    @Override
    int onDataRead(ChannelHandlerContext ctx, int streamId, ByteBuf data, int padding,
                   boolean endOfStream) {
      int processed = data.readableBytes() + padding;
      return processed;
    }

    private String getReportToHeader() {
      return String.format("{\"group\": \"nel\", \"max_age\": 86400, "
                               + "\"endpoints\": [{\"url\": \"%s%s\"}]}",
                           mServerUrl, REPORTING_COLLECTOR_PATH);
    }

    private String getNELHeader() {
      return "{\"report_to\": \"nel\", \"max_age\": 86400, \"success_fraction\": 1.0}";
    }
  }

  private static Http2Headers createDefaultResponseHeaders() {
    return new DefaultHttp2Headers().status(OK.codeAsText());
  }

  private static Http2Headers createResponseHeadersFromRequestHeaders(Http2Headers requestHeaders) {
    // Create response headers by echoing request headers.
    Http2Headers responseHeaders = new DefaultHttp2Headers().status(OK.codeAsText());
    for (Map.Entry<CharSequence, CharSequence> header : requestHeaders) {
      if (!header.getKey().toString().startsWith(":")) {
        responseHeaders.add("echo-" + header.getKey(), header.getValue());
      }
    }

    responseHeaders.add("echo-method", requestHeaders.get(":method").toString());
    return responseHeaders;
  }

  private Http2TestHandler(Http2ConnectionDecoder decoder, Http2ConnectionEncoder encoder,
                           Http2Settings initialSettings, ReportingCollector reportingCollector,
                           String serverUrl, CountDownLatch hangingUrlLatch) {
    super(decoder, encoder, initialSettings);
    mReportingCollector = reportingCollector;
    mServerUrl = serverUrl;
    mHangingUrlLatch = hangingUrlLatch;
  }

  @Override
  public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
    super.exceptionCaught(ctx, cause);
    Log.e(TAG, "An exception was caught", cause);
    ctx.close();
    throw new Exception("Exception Caught", cause);
  }

  @Override
  public int onDataRead(ChannelHandlerContext ctx, int streamId, ByteBuf data, int padding,
                        boolean endOfStream) throws Http2Exception {
    RequestResponder responder = mResponderMap.get(streamId);
    if (endOfStream) {
      mResponderMap.remove(streamId);
    }
    return responder.onDataRead(ctx, streamId, data, padding, endOfStream);
  }

  @Override
  public void onHeadersRead(ChannelHandlerContext ctx, int streamId, Http2Headers headers,
                            int padding, boolean endOfStream) throws Http2Exception {
    String path = headers.path().toString();
    RequestResponder responder;
    if (path.startsWith(ECHO_STREAM_PATH)) {
      responder = new EchoStreamResponder();
    } else if (path.startsWith(ECHO_TRAILERS_PATH)) {
      responder = new EchoTrailersResponder();
    } else if (path.startsWith(ECHO_ALL_HEADERS_PATH)) {
      responder = new EchoAllHeadersResponder();
    } else if (path.startsWith(ECHO_HEADER_PATH)) {
      responder = new EchoHeaderResponder();
    } else if (path.startsWith(ECHO_METHOD_PATH)) {
      responder = new EchoMethodResponder();
    } else if (path.startsWith(SERVE_SIMPLE_BROTLI_RESPONSE)) {
      responder = new ServeSimpleBrotliResponder();
    } else if (path.startsWith(REPORTING_COLLECTOR_PATH)) {
      responder = new ReportingCollectorResponder();
    } else if (path.startsWith(SUCCESS_WITH_NEL_HEADERS_PATH)) {
      responder = new SuccessWithNELHeadersResponder();
    } else if (path.startsWith(COMBINED_HEADERS_PATH)) {
      responder = new CombinedHeadersResponder();
    } else if (path.startsWith(HANGING_REQUEST_PATH)) {
      responder = new HangingRequestResponder();
    } else {
      responder = new RequestResponder();
    }

    responder.onHeadersRead(ctx, streamId, endOfStream, headers);

    if (!endOfStream) {
      mResponderMap.put(streamId, responder);
    }
  }

  @Override
  public void onHeadersRead(ChannelHandlerContext ctx, int streamId, Http2Headers headers,
                            int streamDependency, short weight, boolean exclusive, int padding,
                            boolean endOfStream) throws Http2Exception {
    onHeadersRead(ctx, streamId, headers, padding, endOfStream);
  }

  @Override
  public void onPriorityRead(ChannelHandlerContext ctx, int streamId, int streamDependency,
                             short weight, boolean exclusive) throws Http2Exception {}

  @Override
  public void onRstStreamRead(ChannelHandlerContext ctx, int streamId, long errorCode)
      throws Http2Exception {}

  @Override
  public void onSettingsAckRead(ChannelHandlerContext ctx) throws Http2Exception {}

  @Override
  public void onSettingsRead(ChannelHandlerContext ctx, Http2Settings settings)
      throws Http2Exception {}

  @Override
  public void onPingRead(ChannelHandlerContext ctx, long data) throws Http2Exception {}

  @Override
  public void onPingAckRead(ChannelHandlerContext ctx, long data) throws Http2Exception {}

  @Override
  public void onPushPromiseRead(ChannelHandlerContext ctx, int streamId, int promisedStreamId,
                                Http2Headers headers, int padding) throws Http2Exception {}

  @Override
  public void onGoAwayRead(ChannelHandlerContext ctx, int lastStreamId, long errorCode,
                           ByteBuf debugData) throws Http2Exception {}

  @Override
  public void onWindowUpdateRead(ChannelHandlerContext ctx, int streamId, int windowSizeIncrement)
      throws Http2Exception {}

  @Override
  public void onUnknownFrame(ChannelHandlerContext ctx, byte frameType, int streamId,
                             Http2Flags flags, ByteBuf payload) throws Http2Exception {}
}
