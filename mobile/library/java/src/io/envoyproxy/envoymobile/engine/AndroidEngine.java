package io.envoyproxy.envoymobile.engine;

import android.content.Context;
import android.net.ConnectivityManager;
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus;

public class AndroidEngine {

  // Internal reference to helper object used to load and initialize the native library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile AndroidEngine loader = null;

  private final Engine engine;

  // Private helper class used by the load method to ensure the native library and its
  // dependencies are loaded and initialized at most once.
  private AndroidEngine(Context context) {
    System.loadLibrary("envoy_jni");
    initialize((ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
    engine = new EnvoyEngine();
  }

  // Load and initialize Envoy and its dependencies, but only once.
  public static void load(Context context) {
    if (loader != null) {
      return;
    }

    synchronized (AndroidEngine.class) {
      if (loader != null) {
        return;
      }

      loader = new AndroidEngine(context);
    }
  }

  public static EnvoyStatus run(String config, String logLevel) {
    return loader.engine.runEngine(config, logLevel);
  }

  private static native int initialize(ConnectivityManager connectivityManager);

  private static native boolean isAresInitialized();
}
