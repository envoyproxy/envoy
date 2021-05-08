package org.cronvoy;

import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.CronetEngine;
import org.chromium.net.ExperimentalBidirectionalStream;
import org.cronvoy.Annotations.StreamPriority;

/** Implementation of {@link ExperimentalBidirectionalStream.Builder}. */
final class BidirectionalStreamBuilderImpl extends ExperimentalBidirectionalStream.Builder {

  // All fields are temporary storage of ExperimentalBidirectionalStream configuration to be
  // copied to CronetBidirectionalStream.

  // CronetEngine to create the stream.
  private final CronvoyEngineBase cronetEngine;
  // URL to request.
  private final String url;
  // Callback to receive progress callbacks.
  private final BidirectionalStream.Callback callback;
  // Executor on which callbacks will be invoked.
  private final Executor executor;
  // List of request headers, stored as header field nbame and value pairs.
  private final List<Map.Entry<String, String>> requestHeaders = new ArrayList<>();

  // HTTP method for the request. Default to POST.
  private String httpMethod = "POST";
  // Priority of the stream. Default is medium.
  @StreamPriority private int priority = STREAM_PRIORITY_MEDIUM;

  private boolean delayRequestHeadersUntilFirstFlush;

  // Request reporting annotations.
  private Collection<Object> requestAnnotations;

  private boolean trafficStatsTagSet;
  private int trafficStatsTag;
  private boolean trafficStatsUidSet;
  private int trafficStatsUid;

  /**
   * Creates a builder for {@link BidirectionalStream} objects. All callbacks for generated {@code
   * BidirectionalStream} objects will be invoked on {@code executor}. {@code executor} must not run
   * tasks on the current thread, otherwise the networking operations may block and exceptions may
   * be thrown at shutdown time.
   *
   * @param url the URL for the generated stream
   * @param callback the {@link BidirectionalStream.Callback} object that gets invoked upon
   *     different events occurring
   * @param executor the {@link Executor} on which {@code callback} methods will be invoked
   * @param cronetEngine the {@link CronetEngine} used to create the stream
   */
  BidirectionalStreamBuilderImpl(String url, BidirectionalStream.Callback callback,
                                 Executor executor, CronvoyEngineBase cronetEngine) {
    super();
    if (url == null) {
      throw new NullPointerException("URL is required.");
    }
    if (callback == null) {
      throw new NullPointerException("Callback is required.");
    }
    if (executor == null) {
      throw new NullPointerException("Executor is required.");
    }
    if (cronetEngine == null) {
      throw new NullPointerException("CronetEngine is required.");
    }
    this.url = url;
    this.callback = callback;
    this.executor = executor;
    this.cronetEngine = cronetEngine;
  }

  @Override
  public BidirectionalStreamBuilderImpl setHttpMethod(String method) {
    if (method == null) {
      throw new NullPointerException("Method is required.");
    }
    httpMethod = method;
    return this;
  }

  @Override
  public BidirectionalStreamBuilderImpl addHeader(String header, String value) {
    if (header == null) {
      throw new NullPointerException("Invalid header name.");
    }
    if (value == null) {
      throw new NullPointerException("Invalid header value.");
    }
    requestHeaders.add(new AbstractMap.SimpleImmutableEntry<>(header, value));
    return this;
  }

  @Override
  public BidirectionalStreamBuilderImpl setPriority(@StreamPriority int priority) {
    this.priority = priority;
    return this;
  }

  @Override
  public BidirectionalStreamBuilderImpl
  delayRequestHeadersUntilFirstFlush(boolean delayRequestHeadersUntilFirstFlush) {
    this.delayRequestHeadersUntilFirstFlush = delayRequestHeadersUntilFirstFlush;
    return this;
  }

  @Override
  public ExperimentalBidirectionalStream.Builder addRequestAnnotation(Object annotation) {
    if (annotation == null) {
      throw new NullPointerException("Invalid metrics annotation.");
    }
    if (requestAnnotations == null) {
      requestAnnotations = new ArrayList<Object>();
    }
    requestAnnotations.add(annotation);
    return this;
  }

  @Override
  public ExperimentalBidirectionalStream.Builder setTrafficStatsTag(int tag) {
    trafficStatsTagSet = true;
    trafficStatsTag = tag;
    return this;
  }

  @Override
  public ExperimentalBidirectionalStream.Builder setTrafficStatsUid(int uid) {
    trafficStatsUidSet = true;
    trafficStatsUid = uid;
    return this;
  }

  @Override
  public ExperimentalBidirectionalStream build() {
    return cronetEngine.createBidirectionalStream(
        url, callback, executor, httpMethod, requestHeaders, priority,
        delayRequestHeadersUntilFirstFlush, requestAnnotations, trafficStatsTagSet, trafficStatsTag,
        trafficStatsUidSet, trafficStatsUid);
  }
}
