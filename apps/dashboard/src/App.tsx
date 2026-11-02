import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { api, clearStoredToken, getStoredToken } from './api';
import { User } from './types';
import { NavTab, NAV_ITEMS, Sidebar, visibleNavItems } from './components/Sidebar';
import { Navbar } from './components/Navbar';
import { ToastContainer, ToastMessage } from './components/Toast';
import { LoginView } from './views/LoginView';
import { OverviewView } from './views/OverviewView';
import { WebsitesView } from './views/WebsitesView';
import { PlacementsView } from './views/PlacementsView';
import { CampaignsView } from './views/CampaignsView';
import { ConversionsView } from './views/ConversionsView';
import { AutomationView } from './views/AutomationView';
import { SeoView } from './views/SeoView';
import { OptimizationView } from './views/OptimizationView';
import { BillingView } from './views/BillingView';
import { EmailMarketingView } from './views/EmailMarketingView';
import { AffiliateOffersView } from './views/AffiliateOffersView';
import { PublishersView } from './views/PublishersView';
import { InquiriesView } from './views/InquiriesView';
import { CrmView } from './views/CrmView';
import { AnalyticsView } from './views/AnalyticsView';
import { AuditLogView } from './views/AuditLogView';
import { SandboxView } from './views/SandboxView';
import { SettingsView } from './views/SettingsView';
import { GraduationCap, ShieldCheck } from 'lucide-react';

const VALID_TABS = new Set<NavTab>(NAV_ITEMS.map((item) => item.id));

function tabFromLocation(): NavTab {
  const hash = window.location.hash.replace(/^#/, '');
  return VALID_TABS.has(hash as NavTab) ? (hash as NavTab) : 'overview';
}

function replaceTabHash(tab: NavTab) {
  const target = `#${tab}`;
  if (window.location.hash !== target) {
    window.history.replaceState(null, '', target);
  }
}

const App: React.FC = () => {
  const [user, setUser] = useState<User | null>(null);
  const [authReady, setAuthReady] = useState(false);
  const [activeTab, setActiveTab] = useState<NavTab>(tabFromLocation);
  const [quickCreate, setQuickCreate] = useState<NavTab | null>(null);
  const [toasts, setToasts] = useState<ToastMessage[]>([]);

  const navItems = useMemo(() => visibleNavItems(user?.role), [user?.role]);
  const canWrite = user?.role === 'admin' || user?.role === 'publisher';

  const toastTimers = useRef<Map<string, number>>(new Map());
  const lastToast = useRef<{ key: string; at: number } | null>(null);

  const dismissToast = useCallback((id: string) => {
    const timer = toastTimers.current.get(id);
    if (timer !== undefined) {
      window.clearTimeout(timer);
      toastTimers.current.delete(id);
    }
    setToasts((current) => current.filter((toast) => toast.id !== id));
  }, []);

  const notify = useCallback(
    (type: ToastMessage['type'], message: string) => {
      // Ignore an identical toast fired twice in quick succession (StrictMode,
      // double-click, or a duplicated request) so the UI stays readable.
      const key = `${type}:${message}`;
      const now = Date.now();
      if (lastToast.current && lastToast.current.key === key && now - lastToast.current.at < 1500) {
        return;
      }
      lastToast.current = { key, at: now };

      const id = `${now}-${Math.random().toString(36).slice(2, 8)}`;
      setToasts((current) => [...current.slice(-3), { id, type, message }]);
      const timer = window.setTimeout(() => dismissToast(id), 5000);
      toastTimers.current.set(id, timer);
    },
    [dismissToast],
  );

  useEffect(() => {
    const timers = toastTimers.current;
    return () => {
      timers.forEach((timer) => window.clearTimeout(timer));
      timers.clear();
    };
  }, []);

  // Restore a session from the stored bearer token on a hard refresh.
  useEffect(() => {
    let cancelled = false;
    const token = getStoredToken();
    if (!token) {
      setAuthReady(true);
      return () => {
        cancelled = true;
      };
    }

    api
      .getMe()
      .then((result) => {
        if (!cancelled) setUser(result.user);
      })
      .catch((err: any) => {
        if (cancelled) return;
        if (err?.status !== 401) {
          notify('error', 'Could not verify your session. Check the API and try again.');
        }
      })
      .finally(() => {
        if (!cancelled) setAuthReady(true);
      });

    return () => {
      cancelled = true;
    };
  }, [notify]);

  useEffect(() => {
    const handleUnauthorized = () => {
      setUser(null);
      setAuthReady(true);
      notify('info', 'Your session expired. Sign in again to continue.');
    };
    window.addEventListener('auth:unauthorized', handleUnauthorized);
    return () => window.removeEventListener('auth:unauthorized', handleUnauthorized);
  }, [notify]);

  useEffect(() => {
    const handlePopState = () => setActiveTab(tabFromLocation());
    window.addEventListener('popstate', handlePopState);
    return () => window.removeEventListener('popstate', handlePopState);
  }, []);

  // Keep a role from landing on a section it cannot use (for example an
  // administrator tab after signing in as a publisher).
  useEffect(() => {
    if (!user) return;
    if (!navItems.some((item) => item.id === activeTab)) {
      setActiveTab('overview');
      replaceTabHash('overview');
    }
  }, [activeTab, navItems, user]);

  useEffect(() => {
    document.title = user
      ? `${NAV_ITEMS.find((item) => item.id === activeTab)?.label ?? 'Dashboard'} — AdControl`
      : 'Sign in — AdControl';
  }, [activeTab, user]);

  // The create dialog opens from the initial prop of each view. Drop the flag on
  // the next tick so returning to that tab later does not reopen it.
  useEffect(() => {
    if (!quickCreate || quickCreate !== activeTab) return;
    const timer = window.setTimeout(() => setQuickCreate(null), 100);
    return () => window.clearTimeout(timer);
  }, [activeTab, quickCreate]);

  const navigate = useCallback((tab: NavTab) => {
    setQuickCreate(null);
    setActiveTab(tab);
    replaceTabHash(tab);
  }, []);

  const openQuickCreate = useCallback((tab: NavTab) => {
    setQuickCreate(tab);
    setActiveTab(tab);
    replaceTabHash(tab);
  }, []);

  const handleLoginSuccess = (nextUser: User) => {
    setUser(nextUser);
    setActiveTab('overview');
    replaceTabHash('overview');
    notify('success', `Signed in as ${nextUser.email}.`);
  };

  const handleLogout = async () => {
    try {
      await api.logout();
    } catch (_) {
      // Local token clearing below is authoritative for this browser.
    }
    clearStoredToken();
    setUser(null);
    setActiveTab('overview');
    replaceTabHash('overview');
    notify('info', 'Signed out.');
  };

  const handleSessionInvalidated = () => {
    clearStoredToken();
    setUser(null);
    setActiveTab('overview');
    replaceTabHash('overview');
    notify('info', 'Password changed. Sign in again with the new password.');
  };

  if (!authReady) {
    return (
      <div className="min-h-screen bg-slate-900 flex flex-col items-center justify-center gap-4 text-slate-300">
        <div className="w-12 h-12 rounded-xl bg-blue-600 flex items-center justify-center text-white shadow-lg shadow-blue-500/30">
          <ShieldCheck className="w-6 h-6" />
        </div>
        <div className="text-sm font-medium">Verifying administrator session…</div>
        <div className="h-1 w-40 bg-slate-800 rounded-full overflow-hidden">
          <div className="h-full w-1/2 bg-blue-500 rounded-full animate-pulse" />
        </div>
      </div>
    );
  }

  if (!user) {
    return (
      <>
        <LoginView onLoginSuccess={handleLoginSuccess} onError={(message) => notify('error', message)} />
        <ToastContainer toasts={toasts} onDismiss={dismissToast} />
      </>
    );
  }

  // Learning roles are intentionally rejected by the back-office API. Send them
  // to the public learning app rather than rendering empty operations views.
  const learningOnly = user.role === 'learner' || user.role === 'instructor' || user.role === 'editor';
  if (learningOnly) {
    return (
      <div className="min-h-screen bg-slate-950 px-4 py-16 text-slate-100">
        <div className="mx-auto max-w-lg rounded-2xl border border-slate-800 bg-slate-900 p-8 text-center shadow-xl">
          <div className="mx-auto flex h-12 w-12 items-center justify-center rounded-xl bg-teal-500/15 text-teal-300">
            <GraduationCap className="h-6 w-6" aria-hidden="true" />
          </div>
          <h1 className="mt-5 text-2xl font-bold tracking-tight">Your account uses AdControl Learning</h1>
          <p className="mt-3 text-sm leading-6 text-slate-400">
            {user.email} is a {user.role} account. The operations dashboard is limited to administrators,
            publishers, and analysts. Your courses, progress, and authoring tools live in the learning area.
          </p>
          <div className="mt-7 flex flex-col gap-2 sm:flex-row sm:justify-center">
            <a href="/" className="rounded-lg bg-teal-500 px-4 py-2.5 text-sm font-semibold text-slate-950 hover:bg-teal-400">
              Go to AdControl Learning
            </a>
            {user.role !== 'learner' && (
              <a href="/author" className="rounded-lg border border-slate-700 px-4 py-2.5 text-sm font-semibold text-slate-200 hover:bg-slate-800">
                Author studio
              </a>
            )}
            <button
              type="button"
              onClick={async () => {
                await handleLogout();
                window.location.assign('/');
              }}
              className="rounded-lg border border-slate-700 px-4 py-2.5 text-sm font-semibold text-slate-200 hover:bg-slate-800"
            >
              Sign out
            </button>
          </div>
        </div>
      </div>
    );
  }

  const currentView = (() => {
    switch (activeTab) {
      case 'websites':
        return (
          <WebsitesView
            onNotify={notify}
            canWrite={canWrite}
            isAdmin={user.role === 'admin'}
            openCreateModalDirectly={quickCreate === 'websites'}
          />
        );
      case 'placements':
        return (
          <PlacementsView
            onNotify={notify}
            canWrite={canWrite}
            openCreateModalDirectly={quickCreate === 'placements'}
          />
        );
      case 'campaigns':
        return (
          <CampaignsView
            onNotify={notify}
            role={user.role}
            openCreateModalDirectly={quickCreate === 'campaigns'}
          />
        );
      case 'conversions':
        return <ConversionsView role={user.role} onNotify={notify} />;
      case 'automation':
        return <AutomationView role={user.role} onNotify={notify} />;
      case 'seo':
        return <SeoView role={user.role} onNotify={notify} />;
      case 'optimization':
        return <OptimizationView onNotify={notify} />;
      case 'email':
        return <EmailMarketingView onNotify={notify} />;
      case 'billing':
        return <BillingView role={user.role} onNotify={notify} />;
      case 'affiliate':
        return <AffiliateOffersView onNotify={notify} role={user.role} />;
      case 'publishers':
        return <PublishersView onNotify={notify} />;
      case 'inquiries':
        return <InquiriesView onNotify={notify} />;
      case 'crm':
        return <CrmView onNotify={notify} />;
      case 'analytics':
        return <AnalyticsView onNotify={notify} showPublisherFilter={user.role === 'admin'} />;
      case 'audit':
        return <AuditLogView onNotify={notify} />;
      case 'sandbox':
        return <SandboxView onNotify={notify} />;
      case 'settings':
        return <SettingsView onNotify={notify} onSessionInvalidated={handleSessionInvalidated} />;
      case 'overview':
      default:
        return (
          <OverviewView
            onNavigate={navigate}
            role={user.role}
            onOpenCreateAd={() => openQuickCreate('campaigns')}
            onOpenCreateSite={() => openQuickCreate('websites')}
            onOpenCreatePlacement={() => openQuickCreate('placements')}
          />
        );
    }
  })();

  return (
    <div className="min-h-screen bg-slate-50 flex flex-col">
      <Navbar user={user} onLogout={handleLogout} onOpenSandbox={() => navigate('sandbox')} />

      <div className="flex flex-1 min-h-0">
        <div className="hidden lg:block w-64 shrink-0 bg-slate-900">
          <div className="sticky top-16">
            <Sidebar activeTab={activeTab} onTabChange={navigate} items={navItems} />
          </div>
        </div>

        <div className="flex-1 min-w-0 flex flex-col">
          <nav
            aria-label="Primary"
            className="lg:hidden sticky top-16 z-20 bg-slate-900 border-b border-slate-800 overflow-x-auto"
          >
            <div className="flex gap-1 px-2 py-2 min-w-max">
              {navItems.map((item) => {
                const active = item.id === activeTab;
                return (
                  <button
                    key={item.id}
                    onClick={() => navigate(item.id)}
                    aria-current={active ? 'page' : undefined}
                    className={`flex items-center gap-1.5 whitespace-nowrap px-3 py-2 rounded-lg text-xs font-semibold transition-colors ${
                      active ? 'bg-blue-600 text-white' : 'text-slate-300 hover:bg-slate-800'
                    }`}
                  >
                    {item.icon}
                    {item.label}
                  </button>
                );
              })}
            </div>
          </nav>

          <main className="flex-1 w-full max-w-[1600px] mx-auto px-4 sm:px-6 py-6 sm:py-8">
            {currentView}
          </main>

          <footer className="px-4 sm:px-6 py-4 border-t border-slate-200 text-[11px] text-slate-400 flex flex-col sm:flex-row items-center justify-between gap-1">
            <span>AdControl — self-hosted centralized ad delivery</span>
            <span>Changes propagate to connected sites in under 60 seconds.</span>
          </footer>
        </div>
      </div>

      <ToastContainer toasts={toasts} onDismiss={dismissToast} />
    </div>
  );
};

export default App;
