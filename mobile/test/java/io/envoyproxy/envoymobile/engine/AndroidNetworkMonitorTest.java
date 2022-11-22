package io.envoyproxy.envoymobile.engine;

import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.content.Intent;
import android.Manifest;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import androidx.test.filters.MediumTest;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.GrantPermissionRule;
import androidx.test.annotation.UiThreadTest;
import io.envoyproxy.envoymobile.MockEnvoyEngine;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowConnectivityManager;

/**
 * Tests functionality of AndroidNetworkMonitor
 */
@RunWith(RobolectricTestRunner.class)
public class AndroidNetworkMonitorTest {

  @Rule
  public GrantPermissionRule mRuntimePermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  private AndroidNetworkMonitor androidNetworkMonitor;
  private ShadowConnectivityManager connectivityManager;
  private Context context;

  @Before
  public void setUp() {
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    AndroidNetworkMonitor.load(context, new MockEnvoyEngine());
    androidNetworkMonitor = AndroidNetworkMonitor.getInstance();
    connectivityManager = shadowOf(androidNetworkMonitor.getConnectivityManager());
  }

  /**
   * Tests that isOnline() returns the correct result.
   */
  @Test
  @MediumTest
  @UiThreadTest
  public void testAndroidNetworkMonitorIsOnline() {
    Intent intent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
    // Set up network change
    androidNetworkMonitor.onReceive(context, intent);
    Assert.assertTrue(androidNetworkMonitor.isOnline());

    // Save old networkInfo and simulate a no network scenerio
    NetworkInfo networkInfo = androidNetworkMonitor.getConnectivityManager().getActiveNetworkInfo();
    connectivityManager.setActiveNetworkInfo(null);
    androidNetworkMonitor.onReceive(context, intent);
    Assert.assertFalse(androidNetworkMonitor.isOnline());

    // Bring back online since the AndroidNetworkMonitor class is a singleton
    connectivityManager.setActiveNetworkInfo(networkInfo);
    androidNetworkMonitor.onReceive(context, intent);
  }
}
