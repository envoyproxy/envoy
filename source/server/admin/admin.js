/* jshint esversion: 6 */

// Entry-point into the javascript/ajax populated outline-UI for stats.
function populateScopes(json) {
  populateScope(document.getElementById('scopes-outline'), json, '');
}

// Fetches a Stats::Scope from the server and then populates it into the UI. Note
// that the elements of the scope may have lots of hierarchy, which we will use to
// populate synthetic scopes rather than providing a flattened view of the entire
// scope.
function fetchScope(parent, scope, prefix) {
  let url = 'stats?scope=' + scope + '&show_json_scopes&format=json';
  url += '&type=' + document.getElementById('type').value;
  if (document.getElementById('usedonly').checked) {
    url += '&usedonly';
  }
  fetch(url).then((response) => response.json())
      .then((json) => populateScope(parent, json, scope, prefix));
}

// Having discovered a hierarchical stat name within a scope, we are going to
// make a synthetic scope in the UI, saving all the values we'll need inside
// using the same JSON format sent by the server. When the user expands into
// this synthetic hierarchy we can just populate the UI directly from the saved
// values.
function addNameToSyntheticScope(parent, synthetic_scope, scopes, name, value, prefix) {
  let json = scopes.get(synthetic_scope);
  const json_entry = {name: name, value: value};
  if (json) {
    json.stats.push(json_entry);
    return;
  }
  json = [];
  json.scopes = [];
  json.stats = [];
  json.stats.push(json_entry);
  scopes.set(synthetic_scope, json);
  if (prefix) {
    prefix += '.' + synthetic_scope;
  } else {
    prefix = synthetic_scope;
  }
  addScope(parent, synthetic_scope,
           (parent_tag) => populateScope(parent_tag, json, prefix));
}

// Adds a scope the the UI, underneath the specified parent HTML element.
// expand_fn is called when the user clicks the button to expand the scope.
// This enables a common UI to be used whether the scope is a Stats::Scope in
// the server, or simply name-based hierarchy that is discovered in JavaScript.
function addScope(parent, scope, expand_fn) {
  const li_tag = document.createElement('li');
  const span_tag = document.createElement('span');
  span_tag.setAttribute('class', 'caret');
  span_tag.textContent = scope;
  const children_tag = document.createElement('ul');
  children_tag.setAttribute('class', 'scope-children');
  li_tag.appendChild(span_tag);
  li_tag.appendChild(children_tag);
  parent.appendChild(li_tag);

  span_tag.addEventListener("click", () => {
    span_tag.classList.toggle("caret-down");
    if (children_tag.firstChild) {
      while (children_tag.firstChild) {
        children_tag.removeChild(children_tag.firstChild);
      }
    } else {
      expand_fn(children_tag);
    }
  });
}

function addServerScope(parent, sub_scope, prefix) {
  addScope(parent, sub_scope, (parent_tag) => fetchScope(parent_tag, sub_scope, prefix));
}

// Populates a scope into the parent, based on the specified json,
// which as the format:
//    {stats: [{"name": "name1", "value": value1},
//             {"name": "name2", "value": value2}],
//     scopes: ["scope1", "scope2, "scope3"]}
function populateScope(parent, json, prefix) {
  for (let sub_scope of json.scopes) {
    addServerScope(parent, sub_scope, prefix);
  }

  const scopes = new Map();
  for (let stat of json.stats) {
    processStat(parent, stat, scopes, prefix);
  }
}

// Process a stat from a scope, constructing a value-string for histograms.
function processStat(parent, stat, scopes, prefix) {
  if (stat.histograms && stat.histograms.computed_quantiles) {
    for (let histogram of stat.histograms.computed_quantiles) {
      let val_strs = [];
      for (let value of histogram.values) {
        if (value.interval || value.cumulative) {
          val_strs.push('(' + value.interval + ',' + value.cumulative + ')');
        }
      }
      const val_str = (val_strs.length == 0) ? 'no values' : ('[' + val_strs.join(',') + ']');
      addStat(parent, histogram.name, val_str, scopes, prefix);
    }
  } else {
    addStat(parent, stat.name, stat.value, scopes, prefix);
  }
}

// Adds a stat to the UI into the parent HTML element. If it looks liek
// the stat has some extra hierarchy in it, then synthesize that hierarchy
// into an expandable scope in the outline, collecting all the stats we'll
// need it to include.
function addStat(parent, name, value, scopes, prefix) {
  const full_name = name;
  if (prefix) {
    const prefix_dot = prefix + '.';
    if (!name.startsWith(prefix_dot)) {
      console.log('addStat: ' + name + ' does not start with ' + prefix_dot);
    }
    name = name.substring(prefix_dot.length);
  }

  // If the name has hierarchy in it, then create a scope node instead.
  const split = name.split('.');
  if (split.length >= 2) {
    addNameToSyntheticScope(parent, split[0], scopes, full_name, value, prefix);
  } else {
    const li_tag = document.createElement('li');
    li_tag.textContent = name + ": " + value;
    li_tag.setAttribute('title', full_name);
    parent.appendChild(li_tag);
  }
}
