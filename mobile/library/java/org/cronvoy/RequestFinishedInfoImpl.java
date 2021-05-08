package org.cronvoy;

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
final class RequestFinishedInfoImpl extends RequestFinishedInfo {

  private final String url;
  private final Collection<Object> annotations;
  private final RequestFinishedInfo.Metrics metrics;

  @FinishedReason private final int finishedReason;

  @Nullable private final UrlResponseInfo responseInfo;
  @Nullable private final CronetException exception;

  @IntDef({SUCCEEDED, FAILED, CANCELED})
  @Retention(RetentionPolicy.SOURCE)
  public @interface FinishedReason {}

  RequestFinishedInfoImpl(String url, Collection<Object> annotations,
                          RequestFinishedInfo.Metrics metrics, @FinishedReason int finishedReason,
                          @Nullable UrlResponseInfo responseInfo,
                          @Nullable CronetException exception) {
    this.url = url;
    this.annotations = annotations;
    this.metrics = metrics;
    this.finishedReason = finishedReason;
    this.responseInfo = responseInfo;
    this.exception = exception;
  }

  @Override
  public String getUrl() {
    return url;
  }

  @Override
  public Collection<Object> getAnnotations() {
    if (annotations == null) {
      return Collections.emptyList();
    }
    return annotations;
  }

  @Override
  public Metrics getMetrics() {
    return metrics;
  }

  @Override
  @FinishedReason
  public int getFinishedReason() {
    return finishedReason;
  }

  @Override
  @Nullable
  public UrlResponseInfo getResponseInfo() {
    return responseInfo;
  }

  @Override
  @Nullable
  public CronetException getException() {
    return exception;
  }
}
