/**
 * Utility to count the sepnumber of separated items, e.g. lines.
 * A blank final line does not count.
 */
function count(text, separator) {
  const array = text.split(separator);
  let len = array.length;
  if (len > 0 && array[len - 1] == '') {
    // Don't count a blank final entry.
    --len;
  }
  return len;
}

/**
 * Blocks the async flow until the fetch completes and the page renders.
 */
async function waitForRender(iframe) {
  return new Promise((resolve, reject) => {
    iframe.contentWindow.setRenderTestHook(() => {
      resolve();
    });
  });
}

/**
 * Returns the text that hss been rendered into the 'PRE' element by the system.
 */
function activeContent(iframe) {
  const idoc = iframe.contentWindow.document;
  const pre = idoc.getElementById('active-content-pre');
  assertTrue(pre, "pre not found");
  return pre.textContent;
}

/**
 * Returns an element in the page under test.
 */
function activeElement(iframe, id) {
  const idoc = iframe.contentWindow.document;
  return idoc.getElementById(id);
}

/**
 * Tests the active page with default settings.
 */
async function testActiveDefaults(iframme) {
  await waitForRender(iframe);
  const content = activeContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') != -1, "stat not found");
  assertEq(50, count(content, "\n"));
}

/**
 * Tests the active page, having modified the time interval and the number
 * of elements.
 */
async function testActiveCustomMaxCount(iframme) {
  activeElement(iframe, 'active-update-interval').value = 1;
  activeElement(iframe, 'active-max-display-count').value = 5;

  // Render for two iterations as the first render may race with the update
  // to display-count. To avoid flakes, we render a second time.
  await waitForRender(iframe);
  await waitForRender(iframe);

  const content = activeContent(iframe);
  assertEq(5, count(content, "\n"));
}

/**
 * Tests the active page, having modified the time interval and the number
 * of elements.
 */
async function testActiveFiltered(iframme) {
  activeElement(iframe, 'active-update-interval').value = 1;
  activeElement(iframe, 'param-1-stats-filter').value = 'http';

  // Render for two iterations as the first render may race with the update
  // to filter. To avoid flakes, we render a second time.
  await waitForRender(iframe);
  await waitForRender(iframe);

  const content = activeContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') == -1, "non-matching stat found");
  assertTrue(content.indexOf('http.admin.downstream_cx_tx_bytes_total: ') != -1,
             "matching stat found");
}


addTest('/stats?format=active', "testActiveDefaults", testActiveDefaults);
addTest('/stats?format=active', "testActiveCustomMaxCount", testActiveCustomMaxCount);
addTest('/stats?format=active', "testActiveFiltered", testActiveFiltered);
