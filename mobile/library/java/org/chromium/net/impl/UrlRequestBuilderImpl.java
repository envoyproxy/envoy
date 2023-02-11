package org.chromium.net.impl;

import android.util.Log;
import android.util.Pair;
import java.util.ArrayList;
import java.util.Collection;
import java.util.concurrent.Executor;
import org.chromium.net.CronetEngine;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlRequest;

/**
 * Implements {@link org.chromium.net.ExperimentalUrlRequest.Builder}.
 */
public class UrlRequestBuilderImpl extends ExperimentalUrlRequest.Builder {
  private static final String ACCEPT_ENCODING = "Accept-Encoding";
  private static final String TAG = UrlRequestBuilderImpl.class.getSimpleName();

  // All fields are temporary storage of ExperimentalUrlRequest configuration to be
  // copied to built ExperimentalUrlRequest.

  // CronetEngineBase to execute request.
  private final CronetEngineBase mCronetEngine;
  // URL to request.
  private final String mUrl;
  // Callback to receive progress callbacks.
  private final UrlRequest.Callback mCallback;
  // Executor to invoke callback on.
  private final Executor mExecutor;
  // HTTP method (e.g. GET, POST etc).
  private String mMethod;

  // List of request headers, stored as header field name and value pairs.
  private final ArrayList<Pair<String, String>> mRequestHeaders = new ArrayList<>();
  // Disable the cache for just this request.
  private boolean mDisableCache;
  // Disable connection migration for just this request.
  private boolean mDisableConnectionMigration;
  // Priority of request. Default is medium.
  @CronetEngineBase.RequestPriority private int mPriority = REQUEST_PRIORITY_MEDIUM;
  // Request reporting annotations. Avoid extra object creation if no annotations added.
  private Collection<Object> mRequestAnnotations;
  // If request is an upload, this provides the request body data.
  private UploadDataProvider mUploadDataProvider;
  // Executor to call upload data provider back on.
  private Executor mUploadDataProviderExecutor;
  private boolean mAllowDirectExecutor;
  private boolean mTrafficStatsTagSet;
  private int mTrafficStatsTag;
  private boolean mTrafficStatsUidSet;
  private int mTrafficStatsUid;
  private RequestFinishedInfo.Listener mRequestFinishedListener;
  // Idempotency of the request.
  @CronetEngineBase.Idempotency private int mIdempotency = DEFAULT_IDEMPOTENCY;

  /**
   * Creates a builder for {@link UrlRequest} objects. All callbacks for
   * generated {@link UrlRequest} objects will be invoked on
   * {@code executor}'s thread. {@code executor} must not run tasks on the
   * current thread to prevent blocking networking operations and causing
   * exceptions during shutdown.
   *
   * @param url URL for the generated requests.
   * @param callback callback object that gets invoked on different events.
   * @param executor {@link Executor} on which all callbacks will be invoked.
   * @param cronetEngine {@link CronetEngine} used to execute this request.
   */
  UrlRequestBuilderImpl(String url, UrlRequest.Callback callback, Executor executor,
                        CronetEngineBase cronetEngine) {
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
    mUrl = url;
    mCallback = callback;
    mExecutor = executor;
    mCronetEngine = cronetEngine;
  }

  @Override
  public ExperimentalUrlRequest.Builder setHttpMethod(String method) {
    if (method == null) {
      throw new NullPointerException("Method is required.");
    }
    mMethod = method;
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
    mRequestHeaders.add(Pair.create(header, value));
    return this;
  }

  @Override
  public UrlRequestBuilderImpl disableCache() {
    mDisableCache = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl disableConnectionMigration() {
    mDisableConnectionMigration = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setPriority(@CronetEngineBase.RequestPriority int priority) {
    mPriority = priority;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setIdempotency(@CronetEngineBase.Idempotency int idempotency) {
    mIdempotency = idempotency;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setUploadDataProvider(UploadDataProvider uploadDataProvider,
                                                     Executor executor) {
    if (uploadDataProvider == null) {
      throw new NullPointerException("Invalid UploadDataProvider.");
    }
    if (executor == null) {
      throw new NullPointerException("Invalid UploadDataProvider Executor.");
    }
    if (mMethod == null) {
      mMethod = "POST";
    }
    mUploadDataProvider = uploadDataProvider;
    mUploadDataProviderExecutor = executor;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl allowDirectExecutor() {
    mAllowDirectExecutor = true;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl addRequestAnnotation(Object annotation) {
    if (annotation == null) {
      throw new NullPointerException("Invalid metrics annotation.");
    }
    if (mRequestAnnotations == null) {
      mRequestAnnotations = new ArrayList<>();
    }
    mRequestAnnotations.add(annotation);
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setTrafficStatsTag(int tag) {
    mTrafficStatsTagSet = true;
    mTrafficStatsTag = tag;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setTrafficStatsUid(int uid) {
    mTrafficStatsUidSet = true;
    mTrafficStatsUid = uid;
    return this;
  }

  @Override
  public UrlRequestBuilderImpl setRequestFinishedListener(RequestFinishedInfo.Listener listener) {
    mRequestFinishedListener = listener;
    return this;
  }

  @Override
  public UrlRequestBase build() {
    final UrlRequestBase request = mCronetEngine.createRequest(
        mUrl, mCallback, mExecutor, mPriority, mRequestAnnotations, mDisableCache,
        mDisableConnectionMigration, mAllowDirectExecutor, mTrafficStatsTagSet, mTrafficStatsTag,
        mTrafficStatsUidSet, mTrafficStatsUid, mRequestFinishedListener, mIdempotency);
    if (mMethod != null) {
      request.setHttpMethod(mMethod);
    }
    for (Pair<String, String> header : mRequestHeaders) {
      request.addHeader(header.first, header.second);
    }
    if (mUploadDataProvider != null) {
      request.setUploadDataProvider(mUploadDataProvider, mUploadDataProviderExecutor);
    }
    return request;
  }
}
