package io.envoyproxy.envoymobile.utilities;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.robolectric.Shadows.shadowOf;

import io.envoyproxy.envoymobile.engine.JniLibrary;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitorV2;

import java.nio.charset.StandardCharsets;

import android.content.Context;
import android.Manifest;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.Network;
import android.net.NetworkInfo;

import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.GrantPermissionRule;

import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowNetworkInfo;

@RunWith(RobolectricTestRunner.class)
public final class AndroidNetworkTest {
  @Rule
  public GrantPermissionRule mRuntimePermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  private AndroidNetworkMonitorV2 mAndroidNetworkMonitor;
  private ConnectivityManager mConnectivityManager;
  private final EnvoyEngine mMockEnvoyEngine = mock(EnvoyEngine.class);

  @BeforeClass
  public static void beforeClass() {
    JniLibrary.loadTestLibrary();
  }

  @Before
  public void setUp() throws Exception {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    if (ContextUtils.getApplicationContext() == null) {
      ContextUtils.initApplicationContext(context.getApplicationContext());
    }
    AndroidNetworkMonitorV2.load(context, mMockEnvoyEngine);
    mAndroidNetworkMonitor = AndroidNetworkMonitorV2.getInstance();
    mConnectivityManager = mAndroidNetworkMonitor.getConnectivityManager();
  }

  @Test
  public void testGetDefaultNetworkHandle() {
    Network activeNetwork = mConnectivityManager.getActiveNetwork();
    long networkHandle = JniLibrary.callGetDefaultNetworkHandleFromNative();
    assertEquals(activeNetwork.getNetworkHandle(), networkHandle);

    NetworkInfo wifiNetworkInfo = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                                ConnectivityManager.TYPE_WIFI, 0,
                                                                true, NetworkInfo.State.CONNECTED);
    shadowOf(mConnectivityManager).setActiveNetworkInfo(wifiNetworkInfo);
    Network wifiNetwork = mConnectivityManager.getActiveNetwork();
    long wifiNetworkHandle = JniLibrary.callGetDefaultNetworkHandleFromNative();
    assertEquals(wifiNetwork.getNetworkHandle(), wifiNetworkHandle);
  }
}
