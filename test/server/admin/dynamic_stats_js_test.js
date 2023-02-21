function count(text, separator) {
  return text.split(separator).length - 1;
}

function testDynamic(iframme) {
  return new Promise((resolve, reject) => {
    const idoc = iframe.contentWindow.document;
    const update_interval = idoc.getElementById('dynamic-update-interval');
    update_interval.value = 1;
    iframe.contentWindow.setRenderTestHook(() => {
      rejectExceptions(reject, () => {
        const pre = idoc.getElementById('dynamic-content-pre');
        assertTrue(pre, "pre not found");
        assertTrue(pre.textContent.indexOf('server.memory_allocated: ') != -1, "stat not found");
        assertEq(50, count(pre.textContent, "\n"));

        // Now set the update interval to 5 and re-count lines.
        const max_display_count = idoc.getElementById('dynamic-max-display-count');
        max_display_count.value = 5;
        iframe.contentWindow.setRenderTestHook(() => {
          rejectExceptions(reject, () => {
            assertEq(5, count(pre.textContent, "\n"));
            resolve();
          });
        });
      });
    });
  });
}

addTest('/stats?format=dynamic', "testDynamic", testDynamic);
