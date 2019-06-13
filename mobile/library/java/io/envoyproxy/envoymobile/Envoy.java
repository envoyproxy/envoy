package io.envoyproxy.envoymobile;

import android.content.Context;
import android.net.ConnectivityManager;
import android.util.Log;

import java.io.*;

// Wrapper class that allows for easy calling of Envoy's JNI interface in native Java.
public class Envoy {

  public void load() { System.loadLibrary("envoy_jni"); }

  public void run(Context context, String config) {
    Thread thread = new Thread(new Runnable() {
      @Override
      public void run() {
        initialize((ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE));
        runEnvoy(config.trim());
      }
    });
    thread.start();
  }

  private native int initialize(ConnectivityManager connectivityManager);

  private native boolean isAresInitialized();

  private native int runEnvoy(String config);
}
