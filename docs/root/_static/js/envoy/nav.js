/**
 * Sidebar navigation: an accordion over the toctree captions, plus the mobile
 * drawer.
 *
 * The Envoy tree has nine top-level areas and thousands of pages behind them.
 * sphinx_rtd_theme renders every caption and its subtree at the same visual
 * weight, which gives no sense of place. Here each caption becomes a
 * <details> and only the section containing the current page starts open.
 */

const DESKTOP_QUERY = '(max-width: 960px)';

/**
 * A line icon for each top-level area.
 *
 * Keyed on the link target rather than the label: the real tree has no toctree
 * captions, just nine `toctree-l1` links, and their titles are prose that gets
 * reworded far more often than their paths move. Anything unmatched simply has
 * no icon, which is why the fallback is nothing rather than a generic glyph.
 */
const AREA_ICONS = {
  about_docs: '<path d="M8 4.6v8"/><path d="M2.5 3.4h4A1.5 1.5 0 0 1 8 4.6v8a1.5 1.5 0 0 0-1.5-1.1h-4z"/><path d="M13.5 3.4h-4A1.5 1.5 0 0 0 8 4.6v8a1.5 1.5 0 0 1 1.5-1.1h4z"/>',
  intro: '<circle cx="8" cy="8" r="6"/><path d="M8 7.3v4.1M8 4.8v.2"/>',
  start: '<circle cx="8" cy="8" r="6"/><path d="M6.6 5.5l4.1 2.5-4.1 2.5z"/>',
  configuration: '<path d="M2 5.2h12M2 10.8h12"/><circle cx="6" cy="5.2" r="1.6"/><circle cx="10.4" cy="10.8" r="1.6"/>',
  operations: '<path d="M2.4 12a5.9 5.9 0 1 1 11.2 0"/><path d="M8 12l2.9-3.4"/>',
  extending: '<path d="M2.6 2.6h4.6v4.6H2.6z"/><path d="M8.8 2.6h4.6v4.6H8.8z"/><path d="M8.8 8.8h4.6v4.6H8.8z"/>',
  api: '<path d="M6.1 2.6C4.6 2.6 4.6 8 3.1 8c1.5 0 1.5 5.4 3 5.4"/><path d="M9.9 2.6c1.5 0 1.5 5.4 3 5.4-1.5 0-1.5 5.4-3 5.4"/>',
  faq: '<circle cx="8" cy="8" r="6"/><path d="M6.3 6.4a1.75 1.75 0 1 1 2.3 1.7c-.4.2-.6.5-.6.9v.3"/><path d="M8 11.6v.1"/>',
  version_history: '<circle cx="8" cy="8" r="6"/><path d="M8 4.7V8l2.4 1.5"/>',
};

/**
 * The first meaningful path segment of a top-level link.
 *
 * Sidebar hrefs are relative to the current page, so from deep in the API tree
 * they arrive as `../../start/start.html`; the leading hops have to come off
 * before the segment means anything.
 */
function areaOf(href) {
  const path = href.split(/[?#]/)[0].replace(/^(?:\.\.?\/)+/, '');
  const [first, ...rest] = path.split('/');
  return rest.length ? first : first.replace(/\.html$/, '');
}

/** Prefixes each top-level entry with its area icon. */
function addIcons(menu) {
  menu.querySelectorAll(':scope > ul > li.toctree-l1 > a[href]').forEach((link) => {
    const paths = AREA_ICONS[areaOf(link.getAttribute('href'))];
    if (!paths || link.querySelector('.envoy-nav-icon')) {
      return;
    }

    const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    icon.setAttribute('class', 'envoy-nav-icon');
    icon.setAttribute('viewBox', '0 0 16 16');
    icon.setAttribute('width', '14');
    icon.setAttribute('height', '14');
    icon.setAttribute('fill', 'none');
    icon.setAttribute('stroke', 'currentColor');
    icon.setAttribute('stroke-width', '1.4');
    icon.setAttribute('stroke-linecap', 'round');
    icon.setAttribute('aria-hidden', 'true');
    icon.innerHTML = paths;

    link.prepend(icon);
    link.classList.add('envoy-nav-area');
  });
}

/**
 * Every entry in the API tree ends in "(proto)", which distinguishes none of
 * them and costs a wrapped line on the long ones. The full title stays in the
 * tooltip; the page's own heading is untouched.
 */
function trimTitles(menu) {
  menu.querySelectorAll('a[href]').forEach((link) => {
    const text = link.textContent.trim();
    if (!text.endsWith('(proto)')) {
      return;
    }

    link.title = text;
    const trimmed = text.replace(/\s*\(proto\)$/, '');
    const node = Array.from(link.childNodes)
      .reverse()
      .find((child) => child.nodeType === Node.TEXT_NODE && child.textContent.includes('(proto)'));

    if (node) {
      node.textContent = node.textContent.replace(/\s*\(proto\)\s*$/, '');
    } else {
      link.textContent = trimmed;
    }
  });
}

/** Groups each `p.caption` and the `ul` that follows it into a disclosure. */
function buildSections(menu) {
  const captions = Array.from(menu.querySelectorAll(':scope > p.caption'));
  if (!captions.length) {
    return;
  }

  captions.forEach((caption) => {
    const list = caption.nextElementSibling;
    if (!list || list.tagName !== 'UL') {
      return;
    }

    const label = caption.textContent.trim();
    const details = document.createElement('details');
    details.className = 'envoy-nav-section';

    const summary = document.createElement('summary');
    summary.textContent = label;
    details.append(summary);

    caption.replaceWith(details);
    details.append(list);

    // Open the section holding the current page; if the reader is on a page
    // that is in no toctree, leave the first section open as a starting point.
    if (list.querySelector('.current')) {
      details.open = true;
    }
  });

  const sections = Array.from(menu.querySelectorAll('.envoy-nav-section'));
  if (sections.length && !sections.some((section) => section.open)) {
    sections[0].open = true;
  }
}

function focusable(container) {
  return Array.from(container.querySelectorAll(
    'a[href], button:not([disabled]), input:not([disabled]), summary, ' +
    '[tabindex]:not([tabindex="-1"])'
  )).filter((element) => !element.hidden && element.offsetParent !== null);
}

export function init() {
  const menu = document.querySelector('.wy-menu-vertical');
  if (menu) {
    // The published tree has no toctree captions — it is a flat list of nine
    // `toctree-l1` links — so this is a no-op there and only groups builds that
    // do use captions.
    buildSections(menu);
    addIcons(menu);
    trimTitles(menu);

    // Keep the current page in view when the sidebar is taller than the pane.
    const current = menu.querySelector('a.current');
    if (current) {
      window.requestAnimationFrame(() => {
        current.scrollIntoView({block: 'nearest'});
      });
    }
  }

  const toggle = document.querySelector('[data-envoy-nav-toggle]');
  const sidebar = document.querySelector('.wy-nav-side');
  const backdrop = document.querySelector('.envoy-nav-backdrop');
  const content = document.querySelector('.wy-nav-content-wrap');
  const topbar = document.querySelector('.envoy-doc-topbar');
  const mobile = window.matchMedia(DESKTOP_QUERY);

  if (!toggle || !sidebar || !backdrop) {
    return;
  }

  sidebar.id = sidebar.id || 'envoy-doc-sidebar';

  let open = false;
  let returnFocus = null;

  function setOpen(next, restoreFocus) {
    open = next && mobile.matches;
    document.body.classList.toggle('envoy-nav-open', open);
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute(
      'aria-label', open ? 'Close documentation navigation' :
        'Open documentation navigation');
    backdrop.hidden = !open;

    const hidden = mobile.matches && !open;
    sidebar.inert = hidden;
    sidebar.toggleAttribute('aria-hidden', hidden);

    [content, topbar].forEach((element) => {
      if (!element) {
        return;
      }
      element.inert = open;
      element.toggleAttribute('aria-hidden', open);
    });

    if (open) {
      returnFocus = document.activeElement;
      const targets = focusable(sidebar);
      if (targets.length) {
        window.requestAnimationFrame(() => targets[0].focus());
      }
    } else if (restoreFocus && returnFocus instanceof HTMLElement) {
      returnFocus.focus();
    }
  }

  setOpen(false, false);

  toggle.addEventListener('click', () => setOpen(!open, true));

  document.querySelectorAll('[data-envoy-nav-close]').forEach((button) => {
    button.addEventListener('click', () => setOpen(false, true));
  });

  sidebar.addEventListener('click', (event) => {
    if (event.target.closest('a[href]') && mobile.matches) {
      setOpen(false, false);
    }
  });

  document.addEventListener('keydown', (event) => {
    if (!open) {
      return;
    }

    if (event.key === 'Escape') {
      event.preventDefault();
      setOpen(false, true);
      return;
    }

    if (event.key === 'Tab') {
      const targets = focusable(sidebar);
      if (!targets.length) {
        return;
      }

      const first = targets[0];
      const last = targets[targets.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }
  });

  mobile.addEventListener('change', () => setOpen(false, false));
}
