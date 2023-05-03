package org.chromium.net.impl;

import androidx.annotation.IntDef;
import androidx.annotation.Nullable;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Collection;
import java.util.Collections;
import org.chromium.net.CronetException;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UrlResponseInfo;

/**
 * Implements information about a finished request. Passed to {@link RequestFinishedInfo.Listener}.
 */
public class CronvoyRequestFinishedInfoImpl extends RequestFinishedInfo {
  private final String mUrl;
  private final Collection<Object> mAnnotations;
  private final RequestFinishedInfo.Metrics mMetrics;

  @FinishedReason private final int mFinishedReason;

  @Nullable private final UrlResponseInfo mResponseInfo;
  @Nullable private final CronetException mException;

  @IntDef({SUCCEEDED, FAILED, CANCELED})
  @Retention(RetentionPolicy.SOURCE)
  public @interface FinishedReason {}

  public CronvoyRequestFinishedInfoImpl(String url, Collection<Object> annotations,
                                        RequestFinishedInfo.Metrics metrics,
                                        @FinishedReason int finishedReason,
                                        @Nullable UrlResponseInfo responseInfo,
                                        @Nullable CronetException exception) {
    mUrl = url;
    mAnnotations = annotations;
    mMetrics = metrics;
    mFinishedReason = finishedReason;
    mResponseInfo = responseInfo;
    mException = exception;
  }

  @Override
  public String getUrl() {
    return mUrl;
  }

  @Override
  public Collection<Object> getAnnotations() {
    if (mAnnotations == null) {
      return Collections.emptyList();
    }
    return mAnnotations;
  }

  @Override
  public Metrics getMetrics() {
    return mMetrics;
  }

  @Override
  @FinishedReason
  public int getFinishedReason() {
    return mFinishedReason;
  }

  @Override
  @Nullable
  public UrlResponseInfo getResponseInfo() {
    return mResponseInfo;
  }

  @Override
  @Nullable
  public CronetException getException() {
    return mException;
  }
}
