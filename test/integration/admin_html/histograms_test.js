// Make a histogram with the specified values so we can test that all the
// features of this are rendered graphically.
const histogramJson = {'stats': [{
  'histograms': {
    'supported_percentiles': [0, 25, 50, 75, 90, 95, 99, 99.5, 99.9, 100],
    'details': [{
      'name': 'h1',
      'percentiles': [
        {'cumulative': 200, 'interval': 200},
        {'cumulative': 207.5, 'interval': 207.5},
        {'cumulative': 302.5, 'interval': 302.5},
        {'cumulative': 306.25, 'interval': 306.25},
        {'cumulative': 308.5, 'interval': 308.5},
        {'cumulative': 309.25, 'interval': 309.25},
        {'cumulative': 309.85, 'interval': 309.85},
        {'cumulative': 309.925, 'interval': 309.925},
        {'cumulative': 309.985, 'interval': 309.985},
        {'cumulative': 310, 'interval': 310},
      ],
      'totals': [
        {'lower_bound': 200, 'width': 10, 'count': 1},
        {'lower_bound': 300, 'width': 10, 'count': 2}],
      'intervals': [
        {'lower_bound': 200, 'width': 10, 'count': 1},
        {'lower_bound': 300, 'width': 10, 'count': 2}]}]}}]};

/**
 * Tests the rendering of histograms.
 *
 * @param {!Element} iframe the iframe we can use for rendering.
 */
async function testRenderHistogram(iframe) {
  const idoc = iframe.contentWindow.document;
  renderHistograms(idoc.body, histogramJson);
  const buckets = idoc.getElementsByClassName('histogram-bucket');
  assertEq(2, buckets.length);

  // The first bucket is to the left of the second bucket;
  assertLt(parseFloat(buckets[0].style.left), parseFloat(buckets[1].style.left));

  // The first bucket has a height between 25% and 75%.
  assertLt(25, parseFloat(buckets[0].style.height));
  assertLt(parseFloat(buckets[0].style.height), 75);

  // The second bucket as a 100% height.
  assertEq('100%', buckets[1].style.height);

  // There is one popup div and it is not visible initially.
  const popups = idoc.getElementsByClassName('histogram-popup');
  assertEq(1, popups.length);
  const popup = popups[0];
  assertEq('hidden', getComputedStyle(popup).visibility);

  // When the mouse enters the first bucket it renders a visible popup with
  // associated annotations.
  buckets[0].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(4, popup.children.length);
  assertEq('[200, 210)', popup.children[0].textContent);
  assertEq('P0: 200', popup.children[1].textContent);
  assertEq('Interval [200, 210): 1', popup.children[2].textContent);
  assertEq('P25: 207.5', popup.children[3].textContent);

  // 2 seconds after the mouse leaves, that area, the popup will be made invisible.
  buckets[0].dispatchEvent(new Event('mouseleave'));
  await asyncTimeout(3000);
  assertEq('hidden', getComputedStyle(popup).visibility);

  // Now enter the other bucket. Just check the 1st 2 of 10 buckets.
  buckets[1].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(10, popup.children.length);
  assertEq('[300, 310)', popup.children[0].textContent);
  assertEq('Interval [300, 310): 2', popup.children[1].textContent);
  buckets[1].dispatchEvent(new Event('mouseleave'));

  // Re-enter the first bucket. The popup will immediately move to that with no delay.
  buckets[0].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(4, popup.children.length);
  assertEq('[200, 210)', popup.children[0].textContent);
  buckets[1].dispatchEvent(new Event('mouseleave'));

  // There's exactly one annotations bucket.
  assertEq(1, idoc.getElementsByClassName('histogram-annotations').length);

  // There are 10 percentiles rendered each one to the right of the previous one.
  const percentiles = idoc.getElementsByClassName('histogram-percentile');
  assertEq(10, percentiles.length);
  let prevPercent = 0;
  for (percentile of percentiles) {
    const left = parseFloat(percentile.style.left);
    assertLt(prevPercent, left);
    prevPercent = left;
  }

  // There are 2 intervals rendered each one to the right of the previous one.
  const intervals = idoc.getElementsByClassName('histogram-interval');
  assertEq(2, intervals.length);
  assertLt(parseFloat(intervals[0].style.left), parseFloat(intervals[1].style.left));
}

addTest('?file=histograms_test.html', 'renderHistogram', testRenderHistogram);
