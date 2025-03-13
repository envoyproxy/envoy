package org.chromium.net.urlconnection;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import androidx.test.filters.SmallTest;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Test for CronetURLStreamHandlerFactory.
 */
@RunWith(RobolectricTestRunner.class)
@SuppressWarnings("deprecation")
public class CronetURLStreamHandlerFactoryTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRequireConfig() throws Exception {
    mTestRule.startCronetTestFramework();
    try {
      new CronvoyURLStreamHandlerFactory(null);
      fail();
    } catch (NullPointerException e) {
      assertEquals("CronetEngine is null.", e.getMessage());
    }
  }
}
