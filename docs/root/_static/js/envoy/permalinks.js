/**
 * Permalinks: every addressable definition gets one, and clicking one copies it.
 *
 * Sphinx puts a permalink anchor on headings only. protodoc emits each field
 * and enum value as a definition list carrying the symbol's target, so those
 * are addressable too but have nothing to grab; here they get the same anchor
 * a heading has.
 *
 * Clicking any anchor then copies its absolute URL. The click still jumps to
 * the target as before, so nothing that worked stops working and the address
 * bar remains the fallback where the clipboard is out of reach. A modified
 * click is opening the link somewhere else and is left alone.
 */

/** How long the "Copied" confirmation stays up. */
const COPIED_MS = 1600;

/** A targeted definition list with a single term: the shape protodoc emits. */
const DEFINITION_TERMS = 'dl[id] > dt:only-of-type';

function linkDefinitions(content) {
  content.querySelectorAll(DEFINITION_TERMS).forEach((term) => {
    const link = document.createElement('a');
    link.className = 'headerlink';
    link.href = `#${term.parentElement.id}`;
    link.title = 'Link to this definition';
    term.append(link);
  });
}

/** Sighted readers see it on the anchor; the live region says it aloud. */
function flash(link, status) {
  link.classList.add('envoy-copied');
  status.textContent = 'Link copied';
  window.setTimeout(() => {
    link.classList.remove('envoy-copied');
    status.textContent = '';
  }, COPIED_MS);
}

function copyOnClick(content) {
  content.querySelectorAll('a.headerlink').forEach((link) => {
    link.title = 'Copy link';
  });

  const status = document.createElement('div');
  status.className = 'envoy-visually-hidden';
  status.setAttribute('role', 'status');
  content.append(status);

  content.addEventListener('click', async (event) => {
    const link = event.target.closest('a.headerlink');
    if (!link || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) {
      return;
    }

    try {
      await navigator.clipboard.writeText(link.href);
    } catch {
      return;
    }
    flash(link, status);
  });
}

export function init() {
  const content = document.querySelector('.envoy-content-main');
  if (!content) {
    return;
  }

  linkDefinitions(content);

  if (navigator.clipboard) {
    copyOnClick(content);
  }
}
