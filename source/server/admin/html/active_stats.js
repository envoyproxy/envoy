/**
 * This file contains JavaScript functionality to periodically fetch JSON stats from
 * the Envoy server, and display the top 50 (by default) stats in order of how often
 * they've changed since the page was brought up. This can be useful to find potentially
 * problematic listeners or clusters or other high-cardinality subsystems in Envoy whose
 * activity can be quickly examined for potential problems. The more active a stat, the
 * more likely it is to reflect behavior of note.
 */

// Follow-ups:
//   * top-n algorithm to limit compute overhead with high cardinality stats with user control of N.
//   * alternate sorting criteria, reverse-sort controls, etc.
//   * detect when user is not looking at page and stop or slow down pinging the server
//   * hierarchical display
//   * json flavor to send hierarchical names to save serialization/deserialization costs
//   * pause auto-refresh for at least 5 seconds when editng fields.
//   * don't auto-refresh when there is error -- provide a button to re-retry.
//   * update URL history after editing params
//   * erase spurious 'stats' link at lop left
//   * consider removing histogram mode during active display, and overlay summary graphics
//   * rename bucket mode "none" to "summary"
//   * improve graphics
//   * log Y axis
//   * integrate interval view.
//   * Remove empty buckets
//   * stretchable height
//   * separate /stats/active-html endpoint w/o 'used only' and 'buckets' choices.
//   * sort histograms by change-count
//   * incremental histogram update
//   * resize histograms

/**
 * Maps a stat name to a record containing name, value, and a use-count. This
 * map is rebuilt every 5 seconds.
 */
const nameStatsMap = new Map();

/**
 * The first time this script loads, it will write PRE element at the end of body.
 * This is hooked to the `DOMContentLoaded` event.
 */
let activeStatsPreElement = null;

/**
 * A table into which we render histograms.
 */
let activeStatsHistogramsDiv = null;

/**
 * A small div for displaying status and error messages.
 */
let statusDiv = null;

/**
 * This magic string is derived from C++ in StatsHtmlRender::urlHandler to uniquely
 * name each parameter. In the stats page there is only one parameter, so it's
 * always param-1. The reason params are numbered is that on the home page, "/",
 * all the admin endpoints have uniquely numbered parameters.
 */
const paramIdPrefix = 'param-1-stats-';

let postRenderTestHook = null;

/**
 * To make testing easier, provide a hook for tests to set, to enable tests
 * to block on rendering.
 *
 * @param {!function()} hook
 */
function setRenderTestHook(hook) { // eslint-disable-line no-unused-vars
  if (postRenderTestHook != null) {
    throw new Exception('setRenderTestHook called with hook already pending');
  }
  postRenderTestHook = hook;
}

/**
 * Hook that's run on DOMContentLoaded to create the HTML elements (just one
 * PRE right now) and kick off the periodic JSON updates.
 */
function initHook() {
  statusDiv = document.createElement('div');
  statusDiv.className = 'error-status-line';
  activeStatsPreElement = document.createElement('pre');
  activeStatsPreElement.id = 'active-content-pre';
  const table = document.createElement('table');
  table.classList.add('histogram-body', 'histogram-column');
  activeStatsHistogramsDiv = document.createElement('div');
  //activeStatsHistogramsTable.id = 'active-content-histograms-table';
  document.body.appendChild(statusDiv);
  document.body.appendChild(activeStatsPreElement);
  document.body.appendChild(activeStatsHistogramsDiv);
  loadStats();
}

/**
 * Initiates an Ajax request for the stats JSON based on the stats parameters.
 */
async function loadStats() {
  const makeQueryParam = (name) => name + '=' + encodeURIComponent(
      document.getElementById(paramIdPrefix + name).value);
  const params = ['filter', 'type'];
  const url = '/stats?format=json&usedonly&histogram_buckets=detailed&' +
       params.map(makeQueryParam).join('&');
  try {
    const response = await fetch(url);
    const data = await response.json();
    renderStats(data);
  } catch (e) {
    statusDiv.textContent = 'Error fetching ' + url + ': ' + e;
  }

  // Update stats every 5 seconds by default.
  window.setTimeout(loadStats, 1000*loadSettingOrUseDefault('active-update-interval', 5));
}

/**
 * Function used to sort stat records. The highest priority is update frequency,
 * then value (higher values are likely more interesting), and finally alphabetic,
 * in forward order.
 *
 * @param {!Object} a
 * @param {!Object} b
 * @return {number}
 */
function compareStatRecords(a, b) {
  // Sort higher change-counts first.
  if (a.change_count != b.change_count) {
    return b.change_count - a.change_count;
  }

  // Secondarily put higher values first -- they are often more interesting.
  if (b.value != a.value) {
    return b.value - a.value;
  }

  // Fall back to forward alphabetic sort.
  if (a.name < b.name) {
    return -1;
  }
  if (a.name > b.name) {
    return 1;
  }
  return 0;
}

/**
 * The active display has additional settings for tweaking it -- this helper extracts numeric
 * values from text widgets
 *
 * @param {string} id
 * @param {number} defaultValue
 * @return {number}
 */
function loadSettingOrUseDefault(id, defaultValue) {
  const elt = document.getElementById(id);
  const value = parseInt(elt.value);
  if (Number.isNaN(value) || value <= 0) {
    console.log('Invalid ' + id + ': invalid positive number');
    return defaultValue;
  }
  return value;
}

/**
 * Rendering function which interprets the Json response from the server, updates
 * the most-frequently-used map, reverse-sorts by use-count, and serializes the
 * top ordered stats into the PRE element created in initHook.
 *
 * @param {!Object} data
 */
function renderStats(data) {
  sortedStats = [];
  for (stat of data.stats) {
    if (!stat.name) {
      const histograms = stat.histograms;
      if (histograms) {
        if (histograms.supported_percentiles && histograms.details) {
          renderHistogramDetail(histograms.supported_percentiles, histograms.details);
        }
        continue;
      }
    }
    let statRecord = nameStatsMap.get(stat.name);
    if (statRecord) {
      if (statRecord.value != stat.value) {
        statRecord.value = stat.value;
        ++statRecord.change_count;
      }
    } else {
      statRecord = {name: stat.name, value: stat.value, change_count: 0};
      nameStatsMap.set(stat.name, statRecord);
    }
    sortedStats.push(statRecord);
  }

  // Sorts all the stats. This is inefficient; we should just pick the top N
  // based on field "active-max-display-count" and sort those. The best
  // algorithms for this require a heap or priority queue. JS implementations
  // of those can be found, but that would bloat this relatively modest amount
  // of code, and compel us to do a better job writing tests.
  sortedStats.sort(compareStatRecords);

  const max = loadSettingOrUseDefault('active-max-display-count', 50);
  let index = 0;
  let text = '';
  for (const statRecord of sortedStats) {
    if (++index > max) {
      break;
    }
    text += `${statRecord.name}: ${statRecord.value} (${statRecord.change_count})\n`;
  }
  activeStatsPreElement.textContent = text;

  // If a post-render test-hook has been established, call it, but clear
  // the hook first, so that the callee can set a new hook if it wants to.
  if (postRenderTestHook) {
    const hook = postRenderTestHook;
    postRenderTestHook = null;
    hook();
  }

  statusDiv.textContent = '';
}

const log_10 = Math.log(10);
function log10(num) {
  return Math.log(num) / log_10;
}

// https://stackoverflow.com/questions/4059147/check-if-a-variable-is-a-string-in-javascript
function isString(x) {
  return Object.prototype.toString.call(x) === "[object String]";
}

// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat/NumberFormat
let format = Intl.NumberFormat('en', {
  notation: 'compact',
  maximumFractionDigits: 2
}).format;

let formatPercent = Intl.NumberFormat('en', {
  style: 'percent',
  maximumFractionDigits: 2
}).format;

function renderHistogramDetail(supported_percentiles, detail) {
  activeStatsHistogramsDiv.replaceChildren();
  for (histogram of detail) {
    const div = document.createElement('div');
    const label = document.createElement('div');
    label.className = 'histogram-name';
    label.textContent = histogram.name;

    let i = 0;
    const percentile_values = histogram.percentiles;
    let minValue = null;
    let maxValue = null;
    const updateMinMaxValue = (value) => {
      if (minValue == null) {
        minValue = value;
        maxValue = value;
      } else if (value < minValue) {
        minValue = value;
      } else if (value > minValue) {
        maxValue = value;
      }
    };

    for (i = 0; i < supported_percentiles.length; ++i) {
      updateMinMaxValue(percentile_values[i].cumulative);
    }
    const graphics = document.createElement('div');
    const labels = document.createElement('div');
    const percentiles = document.createElement('div');
    div.appendChild(label);
    div.appendChild(graphics);
    div.appendChild(labels);
    div.appendChild(percentiles);
    graphics.className = 'histogram-graphics';
    percentiles.className = 'histogram-percentiles';
    labels.className = 'histogram-labels';
    activeStatsHistogramsDiv.appendChild(div);

    const numBuckets = histogram.detail.length;
    if (numBuckets == 0) {
      continue;
    }

    // First we can over the detailed bucket value and count info, and determine
    // some limits so we can scale the histogram data to (for now) consume
    // the desired width and height, which we'll express as percentages, so
    // the graphics stretch when the user stretches the window.
    let maxCount = 0;
    let percentileIndex = 0;
    let prevBucket = null;

    for (bucket of histogram.detail) {
      maxCount = Math.max(maxCount, bucket.count);
      updateMinMaxValue(bucket.value);

      // Attach percentile records with values between the previous bucket and
      // this one. We will drop percentiles before the first bucket. Thus each
      // bucket starting with the second one will have a 'percentiles' property
      // with pairs of [percentile, value], which can then be used while
      // rendering the bucket.
      if (prevBucket != null) {
        bucket.percentiles = [];
        for (; percentileIndex < percentile_values.length; ++percentileIndex) {
          const percentileValue = percentile_values[percentileIndex].cumulative;
          if (percentileValue > bucket.value) {
            break; // not increment index; re-consider percentile for next bucket.
          }
          bucket.percentiles.push([supported_percentiles[percentileIndex], percentileValue]);
        }
      }
      prevBucket = bucket;
    }

    const height = maxValue - minValue;
    const scaledValue = value => 9*((value - minValue) / height) + 1; // between 1 and 10;
    const valueToPercent = value => Math.round(80 * log10(scaledValue(value)));

    // Lay out the buckets evenly, independent of the bucket values. It's up
    // to the circlhist library to space out the buckets in a way that shapes
    // the data.
    //
    // We will not draw percentile lines outside of the bucket values. E.g. we
    // may skip drawing outer percentiles like P0 and P100 etc.
    //
    // We lay out horzontally based on CSS percentage so users can see the
    // graphics better if they make the window wider. We do this by inventing
    // arbitrary "virtual pixels" (variables with Vpx suffix) during the
    // computation in JS and converting them to percentages for writing element
    // style.
    const bucketWidthVpx = 20;
    const marginWidthVpx = 40;
    const percentileWidthAndMarginVpx = 20;
    const widthVpx = numBuckets * (bucketWidthVpx + marginWidthVpx);

    const toPercent = vpx => formatPercent(vpx / widthVpx);

    let leftVpx = marginWidthVpx / 2;
    let prevVpx = 0;
    for (i = 0; i < histogram.detail.length; ++i) {
      const bucket = histogram.detail[i];

      if (bucket.percentiles && bucket.percentiles.length > 0) {
        // Keep track of where we write each percentile so if the next one is very close,
        // we can minimize overlapping the text. This is not perfect as the JS is not
        // tracking how wide the text actually is.
        let prevPercentileLabelVpx = 0;

        for (percentile of bucket.percentiles) {
          // Find the ideal place to draw the percentile bar, by linearly
          // interpolating between the current bucket and the previous bucket.
          // We know that the next bucket does not come into play becasue
          // the percentiles held underneath a bucket are based on a value that
          // is at most as large as the current bucket.
          let percentileVpx = leftVpx;
          const prevBucket = histogram.detail[i - 1];
          const bucketDelta = bucket.value - prevBucket.value;
          if (bucketDelta > 0) {
            const weight = (bucket.value - percentile[1]) / bucketDelta;
            percentileVpx = weight * prevVpx + (1 - weight) * leftVpx;
          }

          // We always put the marker proportionally between this bucket and
          // the next one.
          const span = document.createElement('span');
          span.className = 'histogram-percentile';
          let percentilePercent = toPercent(percentileVpx);
          span.style.left = percentilePercent;

          // We try to put the text there too unless it's too close to the previous one.
          if (percentileVpx < prevPercentileLabelVpx) {
            percentileVpx = prevPercentileLabelVpx;
            percentilePercent = toPercent(percentileVpx);
          }
          prevPercentileLabelVpx = percentileVpx + percentileWidthAndMarginVpx;

          const percentilePLabel = document.createElement('span');
          percentilePLabel.className = 'percentile-label';
          percentilePLabel.style.bottom = 0;
          percentilePLabel.textContent = 'P' + percentile[0];
          percentilePLabel.style.left = percentilePercent; // percentileLeft;

          const percentileVLabel = document.createElement('span');
          percentileVLabel.className = 'percentile-label';
          percentileVLabel.style.bottom = '30%';
          percentileVLabel.textContent = format(percentile[1]);
          percentileVLabel.style.left = percentilePercent; // percentileLeft;

          percentiles.appendChild(span);
          percentiles.appendChild(percentilePLabel);
          percentiles.appendChild(percentileVLabel);
        }
      }

      // Now draw the bucket.
      const bucketSpan = document.createElement('span');
      bucketSpan.className = 'histogram-bucket';
      const heightPercent = maxCount == 0 ? 0 : formatPercent(bucket.count / maxCount);
      bucketSpan.style.height = heightPercent;
      const left = toPercent(leftVpx);
      bucketSpan.style.left = left;
      const label = document.createElement('span');
      label.textContent = format(bucket.value);
      label.style.left = left;

      const bucketLabel = document.createElement('span');
      bucketLabel.className = 'bucket-label';
      bucketLabel.textContent = format(bucket.count);
      bucketLabel.style.left = left;
      bucketLabel.style.bottom = heightPercent;

      graphics.appendChild(bucketSpan);
      graphics.appendChild(bucketLabel);
      labels.appendChild(label);
      prevVpx = leftVpx;
      leftVpx += bucketWidthVpx + marginWidthVpx;;
    }
  }
}

// We don't want to trigger any DOM manipulations until the DOM is fully loaded.
addEventListener('DOMContentLoaded', initHook);
