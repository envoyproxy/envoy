package org.chromium.net;

import android.net.TrafficStats;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
/**
 * Class to wrap TrafficStats.setThreadStatsUid(int uid) and TrafficStats.clearThreadStatsUid()
 * which are hidden and so must be accessed via reflection.
 */
public class ThreadStatsUid {
  // Reference to TrafficStats.setThreadStatsUid(int uid).
  private static final Method sSetThreadStatsUid;
  // Reference to TrafficStats.clearThreadStatsUid().
  private static final Method sClearThreadStatsUid;
  // Get reference to TrafficStats.setThreadStatsUid(int uid) and
  // TrafficStats.clearThreadStatsUid() via reflection.
  static {
    try {
      sSetThreadStatsUid = TrafficStats.class.getMethod("setThreadStatsUid", Integer.TYPE);
      sClearThreadStatsUid = TrafficStats.class.getMethod("clearThreadStatsUid");
    } catch (NoSuchMethodException | SecurityException e) {
      throw new RuntimeException("Unable to get TrafficStats methods", e);
    }
  }
  /** Calls TrafficStats.setThreadStatsUid(uid) */
  public static void set(int uid) {
    try {
      sSetThreadStatsUid.invoke(null, uid); // Pass null for "this" as it's a static method.
    } catch (IllegalAccessException e) {
      throw new RuntimeException("TrafficStats.setThreadStatsUid failed", e);
    } catch (InvocationTargetException e) {
      throw new RuntimeException("TrafficStats.setThreadStatsUid failed", e);
    }
  }
  /** Calls TrafficStats.clearThreadStatsUid() */
  public static void clear() {
    try {
      sClearThreadStatsUid.invoke(null); // Pass null for "this" as it's a static method.
    } catch (IllegalAccessException e) {
      throw new RuntimeException("TrafficStats.clearThreadStatsUid failed", e);
    } catch (InvocationTargetException e) {
      throw new RuntimeException("TrafficStats.clearThreadStatsUid failed", e);
    }
  }
}
