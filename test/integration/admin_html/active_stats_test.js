// This file uses unit-test helper functions defined in web_test.js.
//
// See source/server/admin/javascript.md for background info.

/**
 * Utility to count the number of separated items, e.g. lines.
 * A blank final line does not count.
 *
 * @param {string} text
 * @param {string} separator
 * @return {number}
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
 *
 * @param {!HTMLElement} iframe
 * @return {!Promise}
 */
function waitForRender(iframe) {
  return new Promise((resolve, reject) => {
    iframe.contentWindow.setRenderTestHook(() => {
      resolve();
    });
  });
}

/**
 * Returns the text that has been rendered into the 'PRE' element by the system.
 *
 * @param {!HTMLElement} iframe
 * @return {string}
 */
function activeContent(iframe) {
  const idoc = iframe.contentWindow.document;
  const pre = idoc.getElementById('active-content-pre');
  assertTrue(pre, 'pre not found');
  return pre.textContent;
}

/**
 * Returns an element in the page under test.
 *
 * @param {!HTMLElement} iframe
 * @param {string} id
 * @return {?HTMLElement}
 */
function activeElement(iframe, id) {
  const idoc = iframe.contentWindow.document;
  return idoc.getElementById(id);
}

/**
 * Tests the active page with default settings.
 *
 * @param {!HTMLElement} iframe
 * @return {!Promise}
 */
async function testActiveDefaults(iframe) {
  await waitForRender(iframe);
  const content = activeContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') != -1, 'stat not found');
  assertEq(50, count(content, '\n'));
}

/**
 * Tests the active page, having modified the time interval and the number
 * of elements.
 *
 * @param {!HTMLElement} iframe
 * @return {!Promise}
 */
async function testActiveCustomMaxCount(iframe) {
  activeElement(iframe, 'active-update-interval').value = 1;
  activeElement(iframe, 'active-max-display-count').value = 5;

  // Render for two iterations as the first render may race with the update
  // to display-count. To avoid flakes, we render a second time.
  await waitForRender(iframe);
  await waitForRender(iframe);

  const content = activeContent(iframe);
  assertEq(5, count(content, '\n'));
}

/**
 * Tests the active page, having modified the time interval and the number
 * of elements.
 *
 * @param {!HTMLElement} iframe
 * @return {!Promise}
 */
async function testActiveFiltered(iframe) {
  activeElement(iframe, 'active-update-interval').value = 1;
  activeElement(iframe, 'param-1-stats-filter').value = 'http';

  // Render for two iterations as the first render may race with the update
  // to filter. To avoid flakes, we render a second time.
  await waitForRender(iframe);
  await waitForRender(iframe);

  const content = activeContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') == -1, 'non-matching stat found');
  assertTrue(content.indexOf('http.admin.downstream_cx_tx_bytes_total: ') != -1,
             'matching stat found');
}


addTest('/stats?format=active-html', 'testActiveDefaults', testActiveDefaults);
addTest('/stats?format=active-html', 'testActiveCustomMaxCount', testActiveCustomMaxCount);
addTest('/stats?format=active-html', 'testActiveFiltered', testActiveFiltered);
