// Follow-ups:
//   * top-n algo to limit compute overhead with high cardinality stats with user control of N.
//   * alternate sorting criteria, reverse-sort controls, etc.
//   * render histograms
//   * detect when user is not looking at page and stop or slow down pinging the server
//   * hieararchical display
//   * json flavor to send hierarchical names to save serialization/deserialization costs


/**
 * Maps a stat name to a record containing name, value, and a use-count. This
 * map is rebuilt every 5 seconds.
 */
let current_stats = new Map();

/**
 * The first time this script loads, it will write PRE element at the end of body.
 * This is hooked to the DOMContentLoaded event.
 */
let dynamic_stats_pre_element = null;

/**
 * This magic string is derived from C++ in StatsHtmlRender::urlHandler to uniquely
 * name each paramter. In the stats page there is only one parameter, so it's
 * always param-1. The reason params are numbered is that on the home page, "/",
 * all the admin endpoints have uniquely numbered parameters.
 */
const param_id_prefix = "param-1-stats-";

/**
 * To make testing easier, provide a hook for tests to set, to enable tests
 * to block on rendering.
 */
post_render_test_hook = null;

window['setRenderTestHook'] = (hook) => {
  console.log('setting hook');
  post_render_test_hook = hook;
};

/**
 * Hook that's run on DOMContentLoaded to create the HTML elements (just one
 * PRE right now) and kick off the periodic JSON updates.
 */
function initHook() {
  dynamic_stats_pre_element = document.createElement("pre");
  dynamic_stats_pre_element.id = 'dynamic-content-pre';
  document.body.appendChild(dynamic_stats_pre_element);
  loadStats();
}

/**
 * Initiates an ajax request for the stats JSON based on the stats parameters.
 */
function loadStats() {
  const makeQueryParam = (name) => name + "=" + document.getElementById(param_id_prefix + name).value;
  const params = ["filter", "type", "histogram_buckets"];
  const url = "/stats?format=json&usedonly&" + params.map(makeQueryParam).join("&");
  fetch(url).then((response) => response.json()).then((data) => renderStats(data));
}

/**
 * Function used to sort stat records. The highest priority is update frequency,
 * then value (higher values are likely more interesting), and finally alphabetic,
 * in forward order.
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
 * The dynamic display has additional settings for tweaking it -- this helper extracts numeric
 * values from text widgets
 */
function loadSettingOrUseDefault(id, default_value) {
  const elt = document.getElementById(id);
  const value = parseInt(elt.value);
  if (Number.isNaN(value)) {
    console.log("Invalid " + id + ": not a number");
    return default_value;
  }
  return value;
}

/**
 * Rendering function which interprets the Json response from the server, updates
 * the most-frequently-used map, reverse-sorts by use-count, and serializes the
 * top ordered stats into the PRE element created in initHook.
 */
function renderStats(data) {
  sorted_stats = [];
  let prev_stats = current_stats;
  current_stats = new Map();
  for (const stat of data.stats) {
    let stat_record = prev_stats.get(stat.name);
    if (stat_record) {
      if (stat_record.value != stat.value) {
        stat_record.value = stat.value;
        ++stat_record.change_count;
      }
    } else {
      stat_record = {name: stat.name, value: stat.value, change_count: 0};
    }
    current_stats.set(stat.name, stat_record);
    sorted_stats.push(stat_record);
  }
  prev_stats = null;

  // Sorts all the stats. This is inefficient; we should just pick the top N
  // based on field "dynamic-max-display-count" and sort those. The best
  // algorithms for this require a heap or priority queue. JS implementations
  // of those can be found, but that would bloat this relatively modest amount
  // of code, and compell us to do a better job writing tests.
  sorted_stats.sort(compareStatRecords);

  const max = loadSettingOrUseDefault("dynamic-max-display-count", 50);
  let index = 0;
  let text = "";
  for (const stat_record of sorted_stats) {
    if (++index > max) {
      break;
    }
    text += stat_record.name + ": " + stat_record.value + " (" +
        stat_record.change_count + ")" + "\n";
  }
  dynamic_stats_pre_element.textContent = text;

  // If a post-render test-hook has been established, call it, but clear
  // the hook first, so that the callee can set a new hook if it wants to.
  if (post_render_test_hook) {
    const hook = post_render_test_hook;
    post_render_test_hook = null;
    hook();
  }

  // Update stats every 5 seconds by default.
  window.setTimeout(loadStats, 1000*loadSettingOrUseDefault("dynamic-update-interval", 5));
}

// We don't want to trigger any DOM manipulations until the DOM is fully loaded.
addEventListener("DOMContentLoaded", initHook);
