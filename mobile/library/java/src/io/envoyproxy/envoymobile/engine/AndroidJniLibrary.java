package io.envoyproxy.envoymobile.engine;

import android.content.Context;
import android.net.ConnectivityManager;

public class AndroidJniLibrary {

  // Internal reference to helper object used to load and initialize the native library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile AndroidLoader loader = null;

  public static void load(Context context) {
    if (loader != null) {
      return;
    }

    synchronized (AndroidLoader.class) {
      if (loader != null) {
        return;
      }

      JniLibrary.load();
      loader = new AndroidLoader(context);
    }
  }

  // Private helper class used by the load method to ensure the native library and its
  // dependencies are loaded and initialized at most once.
  private static class AndroidLoader {
    private AndroidLoader(Context context) {
      AndroidJniLibrary.initialize(
          (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
    }
  }

  /**
   * Native binding to register the ConnectivityManager to C-Ares
   *
   * @param connectivityManager Android's ConnectivityManager
   * @return int for successful initialization
   */
  protected static native int initialize(ConnectivityManager connectivityManager);

  protected static native int setPreferredNetwork(int network);
}
