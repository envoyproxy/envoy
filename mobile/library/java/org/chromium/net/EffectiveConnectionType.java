package org.chromium.net;

import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@IntDef({EffectiveConnectionType.TYPE_UNKNOWN, EffectiveConnectionType.TYPE_OFFLINE,
         EffectiveConnectionType.TYPE_SLOW_2G, EffectiveConnectionType.TYPE_2G,
         EffectiveConnectionType.TYPE_3G, EffectiveConnectionType.TYPE_4G,
         EffectiveConnectionType.TYPE_LAST})
@Retention(RetentionPolicy.SOURCE)
public @interface EffectiveConnectionType {
  /** Effective connection type reported when the network quality is unknown. */
  int TYPE_UNKNOWN = 0;
  /**
   * Effective connection type reported when the Internet is unreachable because the device does not
   * have a connection (as reported by underlying platform APIs). Note that due to rare but
   * potential bugs in the platform APIs, it is possible that effective connection type is reported
   * as TYPE_OFFLINE. Callers must use caution when using acting on this.
   */
  int TYPE_OFFLINE = 1;
  /**
   * Effective connection type reported when the network has the quality of a poor 2G connection.
   */
  int TYPE_SLOW_2G = 2;
  /**
   * Effective connection type reported when the network has the quality of a faster 2G connection.
   */
  int TYPE_2G = 3;
  /** Effective connection type reported when the network has the quality of a 3G connection. */
  int TYPE_3G = 4;
  /** Effective connection type reported when the network has the quality of a 4G connection. */
  int TYPE_4G = 5;
  /** Last value of the effective connection type. This value is unused. */
  int TYPE_LAST = 6;
}
