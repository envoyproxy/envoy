package org.cronvoy;

import android.util.Log;
import android.util.Pair;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.concurrent.Executor;
import org.chromium.net.CronetEngine;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlRequest;
import org.cronvoy.Annotations.Idempotency;
import org.cronvoy.Annotations.RequestPriority;

/** Implements {@link org.chromium.net.ExperimentalUrlRequest.Builder}. */
final class UrlRequestBuilderImpl extends ExperimentalUrlRequest.Builder {

  private static final String ACCEPT_ENCODING = "Accept-Encoding";
  private static final String TAG = UrlRequestBuilderImpl.class.getSimpleName();
  // All fields are temporary storage of ExperimentalUrlRequest configuration to be
  // copied to built ExperimentalUrlRequest.
  // CronvoyEngineBase to execute request.
  private final CronvoyEngineBase cronetEngine;
  // URL to request.
  private final String url;
  // Callback to receive progress callbacks.
  private final UrlRequest.Callback callback;
  // Executor to invoke callback on.
  private final Executor executor;
  // HTTP method (e.g. GET, POST etc).
  private String method;
  // List of request headers, stored as header field name and value pairs.
  private final List<Pair<String, String>> requestHeaders = new ArrayList<>();
  // Disable the cache for just this request.
  private boolean disableCache;
  // Disable connection migration for just this request.
  private boolean disableConnectionMigration;
  // Priority of request. Default is medium.
  @RequestPriority private int priority = REQUEST_PRIORITY_MEDIUM;
  // Request reporting annotations. Avoid extra object creation if no annotations added.
  private Collection<Object> requestAnnotations;
  // If request is an upload, this provides the request body data.
  private UploadDataProvider uploadDataProvider;
  // Executor to call upload data provider back on.
  private Executor uploadDataProviderExecutor;
  private boolean allowDirectExecutor;
  private boolean trafficStatsTagSet;
  private int trafficStatsTag;
  private boolean trafficStatsUidSet;
  private int trafficStatsUid;
  private RequestFinishedInfo.Listener requestFinishedListener;
  // Idempotency of the request.
  @Idempotency private int idempotency = DEFAULT_IDEMPOTENCY;

  /**
   * Creates a builder for {@link UrlRequest} objects. All callbacks for generated {@link
   * UrlRequest} objects will be invoked on {@code executor}'s thread. {@code executor} must not run
   * tasks on the current thread to prevent blocking networking operations and causing exceptions
   * during shutdown.
   *
   * @param url URL for the generated requests.
   * @param callback callback object that gets invoked on different events.
   * @param executor {@link Executor} on which all callbacks will be invoked.
   * @param cronetEngine {@link CronetEngine} used to execute this request.
   */
  UrlRequestBuilderImpl(String url, UrlRequest.Callback callback, Executor executor,
                        CronvoyEngineBase cronetEngine) {
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
  public ExperimentalUrlRequest.Builder setHttpMethod(String method) {
    if (method == null) {
      throw new NullPointerException("Method is required.");
    }
    this.method = method;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl addHeader(String header, String value) {
    if (header == null) {
      throw new NullPointerException("Invalid header name.");
    }
    if (value == null) {
      throw new NullPointerException("Invalid header value.");
    }
    if (ACCEPT_ENCODING.equalsIgnoreCase(header)) {
      Log.w(TAG,
            "It's not necessary to set Accept-Encoding on requests - cronet will do"
                + " this automatically for you, and setting it yourself has no "
                + "effect. See https://crbug.com/581399 for details.",
            new Exception());
      return this;
    }
    requestHeaders.add(Pair.create(header, value));
    return this;
  }

  @Override
  public UrlRequestBuilderImpl disableCache() {
    disableCache = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl disableConnectionMigration() {
    disableConnectionMigration = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setPriority(@RequestPriority int priority) {
    this.priority = priority;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setIdempotency(@Idempotency int idempotency) {
    this.idempotency = idempotency;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setUploadDataProvider(UploadDataProvider uploadDataProvider,
                                                     Executor uploadDataProviderExecutor) {
    if (uploadDataProvider == null) {
      throw new NullPointerException("Invalid UploadDataProvider.");
    }
    if (uploadDataProviderExecutor == null) {
      throw new NullPointerException("Invalid UploadDataProvider Executor.");
    }
    if (method == null) {
      method = "POST";
    }
    this.uploadDataProvider = uploadDataProvider;
    this.uploadDataProviderExecutor = uploadDataProviderExecutor;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl allowDirectExecutor() {
    allowDirectExecutor = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl addRequestAnnotation(Object annotation) {
    if (annotation == null) {
      throw new NullPointerException("Invalid metrics annotation.");
    }
    if (requestAnnotations == null) {
      requestAnnotations = new ArrayList<>();
    }
    requestAnnotations.add(annotation);
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setTrafficStatsTag(int tag) {
    trafficStatsTagSet = true;
    trafficStatsTag = tag;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setTrafficStatsUid(int uid) {
    trafficStatsUidSet = true;
    trafficStatsUid = uid;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setRequestFinishedListener(RequestFinishedInfo.Listener listener) {
    requestFinishedListener = listener;
    return this;
  }

  @Override
  public UrlRequestBase build() {
    final UrlRequestBase request = cronetEngine.createRequest(
        url, callback, executor, priority, requestAnnotations, disableCache,
        disableConnectionMigration, allowDirectExecutor, trafficStatsTagSet, trafficStatsTag,
        trafficStatsUidSet, trafficStatsUid, requestFinishedListener, idempotency);
    if (method != null) {
      request.setHttpMethod(method);
    }
    for (Pair<String, String> header : requestHeaders) {
      request.addHeader(header.first, header.second);
    }
    if (uploadDataProvider != null) {
      request.setUploadDataProvider(uploadDataProvider, uploadDataProviderExecutor);
    }
    return request;
  }
}
