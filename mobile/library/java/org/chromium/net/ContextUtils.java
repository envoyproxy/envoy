package org.chromium.net;

import android.app.Activity;
import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Handler;
import android.os.Process;
import android.preference.PreferenceManager;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

/**
 * This class provides Android application context related utility methods.
 */
public final class ContextUtils {
  private static final String TAG = "ContextUtils";
  private static Context sApplicationContext;

  private static boolean sSdkSandboxProcess;

  /**
   * Flag for {@link Context#registerReceiver}: The receiver can receive broadcasts from other
   * Apps. Has the same behavior as marking a statically registered receiver with "exported=true".
   *
   * TODO(mthiesse): Move to ApiHelperForT when we build against T SDK.
   */
  public static final int RECEIVER_EXPORTED = 0x2;
  public static final int RECEIVER_NOT_EXPORTED = 0x4;

  /**
   * Initialization-on-demand holder. This exists for thread-safe lazy initialization.
   */
  private static class Holder {
    // Not final for tests.
    private static SharedPreferences sSharedPreferences = fetchAppSharedPreferences();
  }

  /**
   * Get the Android application context.
   *
   * Under normal circumstances there is only one application context in a process, so it's safe
   * to treat this as a global. In WebView it's possible for more than one app using WebView to be
   * running in a single process, but this mechanism is rarely used and this is not the only
   * problem in that scenario, so we don't currently forbid using it as a global.
   *
   * Do not downcast the context returned by this method to Application (or any subclass). It may
   * not be an Application object; it may be wrapped in a ContextWrapper. The only assumption you
   * may make is that it is a Context whose lifetime is the same as the lifetime of the process.
   */
  public static Context getApplicationContext() { return sApplicationContext; }

  /**
   * Initializes the java application context.
   *
   * This should be called exactly once early on during startup, before native is loaded and
   * before any other clients make use of the application context through this class.
   *
   * @param appContext The application context.
   */
  public static void initApplicationContext(Context appContext) {
    // Conceding that occasionally in tests, native is loaded before the browser process is
    // started, in which case the browser process re-sets the application context.
    assert sApplicationContext == null || sApplicationContext == appContext ||
        ((ContextWrapper)sApplicationContext).getBaseContext() == appContext;
    initJavaSideApplicationContext(appContext);
  }

  /**
   * Only called by the static holder class and tests.
   *
   * @return The application-wide shared preferences.
   */
  @SuppressWarnings("DefaultSharedPreferencesCheck")
  private static SharedPreferences fetchAppSharedPreferences() {
    // This may need to create the prefs directory if we've never used shared prefs before, so
    // allow disk writes. This is rare but can happen if code used early in startup reads prefs.
    try (StrictModeContext ignored = StrictModeContext.allowDiskWrites()) {
      return PreferenceManager.getDefaultSharedPreferences(sApplicationContext);
    }
  }

  /**
   * This is used to ensure that we always use the application context to fetch the default shared
   * preferences. This avoids needless I/O for android N and above. It also makes it clear that
   * the app-wide shared preference is desired, rather than the potentially context-specific one.
   *
   * @return application-wide shared preferences.
   */
  public static SharedPreferences getAppSharedPreferences() { return Holder.sSharedPreferences; }

  /**
   * Occasionally tests cannot ensure the application context doesn't change between tests (junit)
   * and sometimes specific tests has its own special needs, initApplicationContext should be used
   * as much as possible, but this method can be used to override it.
   *
   * @param appContext The new application context.
   */
  @VisibleForTesting
  public static void initApplicationContextForTests(Context appContext) {
    initJavaSideApplicationContext(appContext);
    Holder.sSharedPreferences = fetchAppSharedPreferences();
  }

  /**
   * Tests that use the applicationContext may unintentionally use the Context
   * set by a previously run test.
   */
  @VisibleForTesting
  public static void clearApplicationContextForTests() {
    sApplicationContext = null;
    Holder.sSharedPreferences = null;
  }

  private static void initJavaSideApplicationContext(Context appContext) {
    assert appContext != null;
    // Guard against anyone trying to downcast.
    if (appContext instanceof Application) {
      appContext = new ContextWrapper(appContext);
    }
    sApplicationContext = appContext;
  }

  /**
   * As to Exported V.S. NonExported receiver, please refer to
   * https://developer.android.com/reference/android/content/Context#registerReceiver(android.content.BroadcastReceiver,%20android.content.IntentFilter,%20int)
   */
  public static Intent registerExportedBroadcastReceiver(Context context,
                                                         BroadcastReceiver receiver,
                                                         IntentFilter filter, String permission) {
    return registerBroadcastReceiver(context, receiver, filter, permission, /*scheduler=*/null,
                                     RECEIVER_EXPORTED);
  }

  public static Intent registerNonExportedBroadcastReceiver(Context context,
                                                            BroadcastReceiver receiver,
                                                            IntentFilter filter) {
    return registerBroadcastReceiver(context, receiver, filter, /*permission=*/null,
                                     /*scheduler=*/null, RECEIVER_NOT_EXPORTED);
  }

  public static Intent registerNonExportedBroadcastReceiver(Context context,
                                                            BroadcastReceiver receiver,
                                                            IntentFilter filter,
                                                            Handler scheduler) {
    return registerBroadcastReceiver(context, receiver, filter, /*permission=*/null, scheduler,
                                     RECEIVER_NOT_EXPORTED);
  }

  private static Intent registerBroadcastReceiver(Context context, BroadcastReceiver receiver,
                                                  IntentFilter filter, String permission,
                                                  Handler scheduler, int flags) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      return context.registerReceiver(receiver, filter, permission, scheduler, flags);
    } else {
      return context.registerReceiver(receiver, filter, permission, scheduler);
    }
  }
}
