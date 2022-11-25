package org.chromium.net.testing;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * Stores Reporting API reports received by a test collector, providing helper methods for checking
 * whether expected reports were actually received.
 */
final class ReportingCollector {
  private ArrayList<JSONObject> mReceivedReports = new ArrayList<JSONObject>();
  private Semaphore mReceivedReportsSemaphore = new Semaphore(0);

  /**
   * Stores a batch of uploaded reports.
   * @param payload the POST payload from the upload
   * @return whether the payload was parsed successfully
   */
  public boolean addReports(String payload) {
    try {
      JSONArray reports = new JSONArray(payload);
      int elementCount = 0;
      synchronized (mReceivedReports) {
        for (int i = 0; i < reports.length(); i++) {
          JSONObject element = reports.optJSONObject(i);
          if (element != null) {
            mReceivedReports.add(element);
            elementCount++;
          }
        }
      }
      mReceivedReportsSemaphore.release(elementCount);
      return true;
    } catch (JSONException e) {
      return false;
    }
  }

  /**
   * Checks whether a report with the given payload exists or not.
   */
  public boolean containsReport(String expected) {
    try {
      JSONObject expectedReport = new JSONObject(expected);
      synchronized (mReceivedReports) {
        for (JSONObject received : mReceivedReports) {
          if (isJSONObjectSubset(expectedReport, received)) {
            return true;
          }
        }
      }
      return false;
    } catch (JSONException e) {
      return false;
    }
  }

  /**
   * Waits until the requested number of reports have been received, with a 5-second timeout.
   */
  public void waitForReports(int reportCount) {
    final int timeoutSeconds = 5;
    try {
      mReceivedReportsSemaphore.tryAcquire(reportCount, timeoutSeconds, TimeUnit.SECONDS);
    } catch (InterruptedException e) {
    }
  }

  /**
   * Checks whether one {@link JSONObject} is a subset of another. Any fields that appear in
   * {@code lhs} must also appear in {@code rhs}, with the same value. There can be extra fields
   * in {@code rhs}; if so, they are ignored.
   */
  private boolean isJSONObjectSubset(JSONObject lhs, JSONObject rhs) {
    Iterator<String> keys = lhs.keys();
    while (keys.hasNext()) {
      String key = keys.next();
      Object lhsElement = lhs.opt(key);
      Object rhsElement = rhs.opt(key);

      if (rhsElement == null) {
        // lhs has an element that doesn't appear in rhs
        return false;
      }

      if (lhsElement instanceof JSONObject) {
        if (!(rhsElement instanceof JSONObject)) {
          return false;
        }
        return isJSONObjectSubset((JSONObject)lhsElement, (JSONObject)rhsElement);
      }

      if (!lhsElement.equals(rhsElement)) {
        return false;
      }
    }
    return true;
  }
};
