package io.envoyproxy.envoymobile;

import android.content.Context;
import android.net.ConnectivityManager;

public class EnvoyEngine {

  // Internal reference to helper object used to load and initialize the native library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile EnvoyEngine loader = null;

  // Private helper class used by the load method to ensure the native library and its
  // dependencies are loaded and initialized at most once.
  private EnvoyEngine(Context context) {
    System.loadLibrary("envoy_jni");
    initialize((ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
  }

  // Load and initialize Envoy and its dependencies, but only once.
  public static void load(Context context) {
    if (loader != null) {
      return;
    }

    synchronized (EnvoyEngine.class) {
      if (loader != null) {
        return;
      }

      loader = new EnvoyEngine(context);
    }
  }

  private static native int initialize(ConnectivityManager connectivityManager);

  private static native boolean isAresInitialized();

  public static native int run(String config);
}
