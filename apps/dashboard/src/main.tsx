import React, { lazy, Suspense } from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';

// Kept in sync with Sidebar NAV_ITEMS. This small list avoids loading the whole
// back-office bundle just to route legacy /#campaigns bookmarks.
const DASHBOARD_TABS = new Set([
  'overview',
  'websites',
  'placements',
  'campaigns',
  'email',
  'affiliate',
  'publishers',
  'analytics',
  'audit',
  'sandbox',
  'settings',
]);

// The operations dashboard lives at /dashboard. Legacy bookmarks that put a
// dashboard tab in the root hash (/#campaigns) still open the back office,
// while ordinary anchors such as /#main-content stay in the public learning app.
function isDashboardLocation(): boolean {
  const path = window.location.pathname;
  if (path === '/dashboard' || path.startsWith('/dashboard/')) return true;
  if (path !== '/' && path !== '/index.html') return false;
  const hash = window.location.hash.replace(/^#/, '');
  return DASHBOARD_TABS.has(hash);
}

const App = lazy(() => import('./App'));
const LearningApp = lazy(() =>
  import('./learning/LearningApp').then((module) => ({ default: module.LearningApp })),
);

const Root = isDashboardLocation() ? App : LearningApp;

ReactDOM.createRoot(document.getElementById('root') as HTMLElement).render(
  <React.StrictMode>
    <Suspense
      fallback={
        <div
          role="status"
          className="min-h-screen bg-slate-950 text-slate-200 flex items-center justify-center p-6"
        >
          <span className="text-sm font-medium">Loading AdControl…</span>
        </div>
      }
    >
      <Root />
    </Suspense>
  </React.StrictMode>,
);
