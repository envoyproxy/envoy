package org.chromium.net.impl;

import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Annotations for "int" based Enums. */
public final class Annotations {
  /** Enum defined here: chromium/src/net/base/request_priority.h */
  @IntDef({RequestPriority.THROTTLED, RequestPriority.IDLE, RequestPriority.LOWEST,
           RequestPriority.LOW, RequestPriority.MEDIUM, RequestPriority.HIGHEST})
  @Retention(RetentionPolicy.SOURCE)
  public @interface RequestPriority {
    int THROTTLED = 0;
    int IDLE = 1; // Default "as resources available" level.
    int LOWEST = 2;
    int LOW = 3;
    int MEDIUM = 4;
    int HIGHEST = 5;
  }

  /** Subset of errors defined in chromium/src/net/base/net_error_list.h */
  @IntDef({NetError.ERR_HTTP2_PING_FAILED, NetError.ERR_QUIC_HANDSHAKE_FAILED})
  @Retention(RetentionPolicy.SOURCE)
  public @interface NetError {
    int ERR_NETWORK_CHANGED = -21;
    int ERR_HTTP2_PING_FAILED = -352;
    int ERR_QUIC_PROTOCOL_ERROR = -356;
    int ERR_QUIC_HANDSHAKE_FAILED = -358;
  }

  /** Enum defined here: chromium/src/components/cronet/url_request_context_config.h, line 37 */
  @IntDef({HttpCacheType.DISABLED, HttpCacheType.DISK, HttpCacheType.MEMORY})
  @Retention(RetentionPolicy.SOURCE)
  public @interface HttpCacheType {
    int DISABLED = 0;
    int DISK = 1;
    int MEMORY = 2;
  }

  private Annotations() {}
}
