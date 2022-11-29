package org.chromium.net.testing;

import android.app.Application;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.AssetManager;
import android.os.Process;

/**
 * This class provides Android application context related utility methods.
 */
public final class ContextUtils {

  private static final String TAG = "ContextUtils";
  private static Context mApplicationContext;

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
  public static Context getApplicationContext() { return mApplicationContext; }

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
    assert mApplicationContext == null || mApplicationContext == appContext ||
        ((ContextWrapper)mApplicationContext).getBaseContext() == appContext;
    initJavaSideApplicationContext(appContext);
  }

  private static void initJavaSideApplicationContext(Context appContext) {
    assert appContext != null;
    // Guard against anyone trying to downcast.
    // TODO(carloseltureto) is this needed? if (BuildConfig.ENABLE_ASSERTS && ...
    if (appContext instanceof Application) {
      appContext = new ContextWrapper(appContext);
    }
    mApplicationContext = appContext;
  }

  /**
   * In most cases, {@link Context#getAssets()} can be used directly. Modified resources are
   * used downstream and are set up on application startup, and this method provides access to
   * regular assets before that initialization is complete.
   *
   * This method should ONLY be used for accessing files within the assets folder.
   *
   * @return Application assets.
   */
  public static AssetManager getApplicationAssets() {
    Context context = getApplicationContext();
    while (context instanceof ContextWrapper) {
      context = ((ContextWrapper)context).getBaseContext();
    }
    return context.getAssets();
  }

  /**
   * @return Whether the process is isolated.
   */
  public static boolean isIsolatedProcess() {
    // Was not made visible until Android P, but the method has always been there.
    return Process.isIsolated();
  }
}
