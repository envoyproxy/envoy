function count(text, separator) {
  return text.split(separator).length - 1;
}

function testDynamic(iframme) {
  return new Promise((resolve, reject) => {
    iframe.contentWindow.setRenderTestHook(() => {
      rejectExceptions(resolve, reject, () => {
        const pre = iframe.contentWindow.document.getElementById('dynamic-content-pre');
        assertTrue(pre, "pre not found");
        assertTrue(pre.textContent.indexOf('server.memory_allocated: ') != -1, "stat not found");
        assertEq(50, count(pre.textContent, "\n"));
      });
    });
  });
}

addTest('/stats?format=dynamic', "testDynamic", testDynamic);
