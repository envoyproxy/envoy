package io.envoyproxy.envoymobile.engine;

import android.app.Activity;
import android.app.Application.ActivityLifecycleCallbacks;
import android.annotation.TargetApi;
import android.os.Build;
import android.os.Bundle;

/*
 * This class may be registered to an app's lifecycle callbacks in order to perform custom handling
 * of lifecycle events, such as flushing stats when the app is paused.
 */
@TargetApi(Build.VERSION_CODES.LOLLIPOP)
public class AndroidAppLifecycleMonitor implements ActivityLifecycleCallbacks {
  @Override
  public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
    /* Required override for interface implementation */
  }

  @Override
  public void onActivityStarted(Activity activity) {
    /* Required override for interface implementation */
  }

  @Override
  public void onActivityResumed(Activity activity) {
    /* Required override for interface implementation */
  }

  @Override
  public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
    /* Required override for interface implementation */
  }

  @Override
  public void onActivityPaused(Activity activity) {
    /* Use for stats flushing when https://github.com/lyft/envoy-mobile/issues/754 is resolved */
  }

  @Override
  public void onActivityStopped(Activity activity) {
    /* Use for stats flushing when https://github.com/lyft/envoy-mobile/issues/754 is resolved */
  }

  @Override
  public void onActivityDestroyed(Activity activity) {
    /* Use for stats flushing when https://github.com/lyft/envoy-mobile/issues/754 is resolved */
  }
}
