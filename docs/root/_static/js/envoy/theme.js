/** Colour theme toggle. The pre-paint application lives in envoy-theme.js. */

const KEY = 'envoy-docs-theme';
const media = window.matchMedia('(prefers-color-scheme: dark)');

function stored() {
  try {
    const value = window.localStorage.getItem(KEY);
    return value === 'light' || value === 'dark' ? value : null;
  } catch (error) {
    return null;
  }
}

/** The theme currently on screen, whether chosen or inherited. */
export function current() {
  return document.documentElement.dataset.envoyTheme ||
    (media.matches ? 'dark' : 'light');
}

function apply(theme) {
  document.documentElement.dataset.envoyTheme = theme;
  document.documentElement.style.colorScheme = theme;

  try {
    window.localStorage.setItem(KEY, theme);
  } catch (error) {
    // The theme still applies for this page view when storage is unavailable.
  }

  document.querySelectorAll('[data-envoy-theme-toggle]').forEach((button) => {
    button.setAttribute(
      'aria-label', theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
  });
}

export function init() {
  const toggles = document.querySelectorAll('[data-envoy-theme-toggle]');
  if (!toggles.length) {
    return;
  }

  apply(current());

  toggles.forEach((button) => {
    button.addEventListener('click', () => {
      apply(current() === 'dark' ? 'light' : 'dark');
    });
  });

  // Readers who never pressed the toggle keep following the system.
  media.addEventListener('change', () => {
    if (!stored()) {
      delete document.documentElement.dataset.envoyTheme;
      document.documentElement.style.colorScheme = '';
    }
  });
}
