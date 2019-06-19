package io.envoyproxy.envoymobile;

import android.content.Context;
import android.net.ConnectivityManager;
import android.util.Log;

import java.io.*;

// Wrapper class that allows for easy calling of Envoy's JNI interface in native Java.
public class Envoy {

  // Internal reference to helper object used to load and initialize the native library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile EnvoyLoader loader = null;

  // Dedicated thread for running this instance of Envoy.
  private final Thread runner;

  // Private helper class used by the load method to ensure the native library and its
  // dependencies are loaded and initialized at most once.
  private static class EnvoyLoader {

    EnvoyLoader(Context context) {
      System.loadLibrary("envoy_jni");
      initialize((ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
    }
  }

  // Load and initialize Envoy and its dependencies, but only once.
  public static void load(Context context) {
    if (loader != null) {
      return;
    }

    synchronized (Envoy.class) {
      if (loader != null) {
        return;
      }

      loader = new EnvoyLoader(context);
    }
  }

  // Create a new Envoy instance. The Envoy runner Thread is started as part of instance
  // initialization with the configuration provided. If the Envoy native library and its
  // dependencies haven't been loaded and initialized yet, this will happen lazily when
  // the first instance is created.
  public Envoy(final Context context, final String config) {
    // Lazily initialize Envoy and its dependencies, if necessary.
    load(context);

    runner = new Thread(new Runnable() {
      @Override
      public void run() {
        Envoy.this.run(config.trim());
      }
    });
    runner.start();
  }

  // Returns whether the Envoy instance is currently active and running.
  public boolean isRunning() {
    final Thread.State state = runner.getState();
    return state != Thread.State.NEW && state != Thread.State.TERMINATED;
  }

  // Returns whether the Envoy instance is terminated.
  public boolean isTerminated() { return runner.getState() == Thread.State.TERMINATED; }

  private static native int initialize(ConnectivityManager connectivityManager);

  private static native boolean isAresInitialized();

  private native int run(String config);
}
