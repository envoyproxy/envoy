package io.envoyproxy.envoymobile.engine;

import android.content.Context;
import android.net.ConnectivityManager;

public class AndroidEngineImpl {

  // Internal reference to helper object used to load and initialize the native library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile AndroidEngineImpl loader = null;

  private final EnvoyEngine envoyEngine;

  // Private helper class used by the load method to ensure the native library and its
  // dependencies are loaded and initialized at most once.
  private AndroidEngineImpl(Context context) {
    System.loadLibrary("envoy_jni");
    initialize((ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
    envoyEngine = new EnvoyEngineImpl();
  }

  // Load and initialize Envoy and its dependencies, but only once.
  public static void load(Context context) {
    if (loader != null) {
      return;
    }

    synchronized (AndroidEngineImpl.class) {
      if (loader != null) {
        return;
      }

      loader = new AndroidEngineImpl(context);
    }
  }

  public static int run(String config, String logLevel) {
    // TODO: Resolve the static loader instance
    return loader.envoyEngine.runWithConfig(config, logLevel);
  }

  private static native int initialize(ConnectivityManager connectivityManager);

  private static native boolean isAresInitialized();
}
