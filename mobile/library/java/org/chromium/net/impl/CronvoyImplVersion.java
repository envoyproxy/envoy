package org.chromium.net.impl;

// Version based on chrome/VERSION.
public final class CronvoyImplVersion {
  // TODO(carloseltuerto) make this class a template and use "@MAJOR@.@MINOR@.@BUILD@.@PATCH@"
  private static final String CRONET_VERSION = "99.0.4512.7";
  // TODO(carloseltuerto) make this class a template and use @API_LEVEL@;
  private static final int API_LEVEL = 14;
  // TODO(carloseltuerto) make this class a template and use "@LAST_CHANGE@";
  private static final String LAST_CHANGE = "20220222";

  /**
   * Private constructor. All members of this class should be static.
   */
  private CronvoyImplVersion() {}

  public static String getCronetVersionWithLastChange() {
    return CRONET_VERSION + "@" + LAST_CHANGE.substring(0, 8);
  }

  public static int getApiLevel() { return API_LEVEL; }

  public static String getCronetVersion() { return CRONET_VERSION; }

  public static String getLastChange() { return LAST_CHANGE; }
}
