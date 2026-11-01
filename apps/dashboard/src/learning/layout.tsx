import React, { ReactNode, useEffect, useMemo, useState } from 'react';
import {
  BookOpen,
  Compass,
  ExternalLink,
  FileText,
  Globe2,
  GraduationCap,
  LayoutDashboard,
  LogOut,
  Megaphone,
  Menu,
  NotebookPen,
  ShieldCheck,
  UserRound,
  X,
} from 'lucide-react';
import { Link, useRouter } from './router';
import { canAuthor, useAuth } from './state';

const SkipLink: React.FC = () => (
  <a
    href="#main-content"
    className="sr-only focus:not-sr-only focus:fixed focus:left-4 focus:top-4 focus:z-[100] focus:rounded-lg focus:bg-teal-900 focus:px-4 focus:py-3 focus:text-sm focus:font-semibold focus:text-white"
  >
    Skip to main content
  </a>
);

interface NavLinkProps {
  to: string;
  children: ReactNode;
  active: boolean;
  onClick?: () => void;
}

const NavLink: React.FC<NavLinkProps> = ({ to, children, active, onClick }) => (
  <Link
    to={to}
    onClick={onClick}
    aria-current={active ? 'page' : undefined}
    className={`rounded-lg px-3 py-2 text-sm font-semibold transition-colors ${
      active ? 'bg-teal-50 text-teal-900' : 'text-slate-700 hover:bg-slate-100 hover:text-slate-950'
    }`}
  >
    {children}
  </Link>
);

const navItems = [
  { to: '/courses', label: 'Courses', icon: BookOpen },
  { to: '/topics', label: 'Topics', icon: Compass },
  { to: '/learning-paths', label: 'Learning paths', icon: GraduationCap },
  { to: '/become-a-publisher', label: 'For publishers', icon: Globe2 },
  { to: '/become-a-client', label: 'For clients', icon: Megaphone },
  { to: '/docs', label: 'Docs', icon: FileText },
];

export const LearningLayout: React.FC<{ children: ReactNode }> = ({ children }) => {
  const { path, navigate } = useRouter();
  const { user, authReady, signOut } = useAuth();
  const [mobileOpen, setMobileOpen] = useState(false);
  const [signingOut, setSigningOut] = useState(false);

  useEffect(() => {
    setMobileOpen(false);
  }, [path]);

  useEffect(() => {
    const main = document.getElementById('main-content');
    main?.focus({ preventScroll: true });
  }, [path]);

  useEffect(() => {
    if (!mobileOpen) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setMobileOpen(false);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [mobileOpen]);

  const learningUser = useMemo(
    () => !!user && (user.role === 'learner' || user.role === 'instructor' || user.role === 'editor' || user.role === 'admin'),
    [user],
  );
  const backOffice = useMemo(
    () => !!user && (user.role === 'admin' || user.role === 'publisher' || user.role === 'analyst'),
    [user],
  );

  const isActive = (to: string) => path === to || path.startsWith(`${to}/`);

  const handleSignOut = async () => {
    setSigningOut(true);
    try {
      await signOut();
    } finally {
      setSigningOut(false);
      navigate('/');
    }
  };

  const signedInActions = user ? (
    <>
      {learningUser && (
        <Link to="/my-learning" className="lc-button lc-button-secondary">
          <UserRound className="h-4 w-4" aria-hidden="true" />
          My learning
        </Link>
      )}
      {canAuthor(user) && (
        <Link to="/author" className="lc-button lc-button-secondary">
          <NotebookPen className="h-4 w-4" aria-hidden="true" />
          Author studio
        </Link>
      )}
      {backOffice && (
        <a href="/dashboard" className="lc-button lc-button-secondary">
          <LayoutDashboard className="h-4 w-4" aria-hidden="true" />
          Operations
        </a>
      )}
      <div className="flex items-center gap-1 rounded-lg border border-slate-200 bg-white py-1 pl-2 pr-1">
        <span className="max-w-[11rem] truncate px-1 text-xs font-medium text-slate-600" title={user.email}>
          {user.email}
        </span>
        <button
          type="button"
          onClick={handleSignOut}
          disabled={signingOut}
          className="rounded-md p-1.5 text-slate-500 hover:bg-slate-100 hover:text-slate-900 disabled:opacity-50"
          aria-label="Sign out"
        >
          <LogOut className="h-4 w-4" aria-hidden="true" />
        </button>
      </div>
    </>
  ) : (
    authReady && (
      <>
        <Link to="/login" className="lc-button lc-button-secondary">Sign in</Link>
        <Link to="/register" className="lc-button lc-button-primary">Create free account</Link>
      </>
    )
  );

  return (
    <div className="lc-surface flex min-h-screen flex-col text-slate-900">
      <SkipLink />

      <header className="sticky top-0 z-40 border-b border-slate-200/80 bg-[#f7f6f2]/95 backdrop-blur">
        <div className="mx-auto flex h-16 w-full max-w-7xl items-center justify-between gap-4 px-4 sm:px-6 lg:px-8">
          <Link to="/" className="flex shrink-0 items-center gap-2.5" aria-label="AdControl Learning home">
            <span className="flex h-9 w-9 items-center justify-center rounded-xl bg-teal-900 text-white shadow-sm">
              <GraduationCap className="h-5 w-5" aria-hidden="true" />
            </span>
            <span className="leading-tight">
              <span className="block text-sm font-bold tracking-tight text-slate-950">AdControl</span>
              <span className="block text-[11px] font-semibold uppercase tracking-[0.16em] text-teal-800">Learning</span>
            </span>
          </Link>

          <nav aria-label="Main" className="hidden items-center gap-1 lg:flex">
            {navItems.map((item) => (
              <NavLink key={item.to} to={item.to} active={isActive(item.to)}>
                {item.label}
              </NavLink>
            ))}
          </nav>

          <div className="hidden items-center gap-2 2xl:flex">{signedInActions}</div>

          <button
            type="button"
            className="lc-button lc-button-secondary px-3 2xl:hidden"
            aria-expanded={mobileOpen}
            aria-controls="learning-mobile-menu"
            onClick={() => setMobileOpen((open) => !open)}
          >
            {mobileOpen ? <X className="h-4 w-4" aria-hidden="true" /> : <Menu className="h-4 w-4" aria-hidden="true" />}
            <span className="sr-only sm:not-sr-only">Menu</span>
          </button>
        </div>

        {mobileOpen && (
          <div id="learning-mobile-menu" className="border-t border-slate-200 bg-white px-4 pb-5 pt-3 shadow-lg 2xl:hidden">
            <nav aria-label="Mobile" className="grid gap-1">
              {navItems.map((item) => {
                const Icon = item.icon;
                return (
                  <Link
                    key={item.to}
                    to={item.to}
                    onClick={() => setMobileOpen(false)}
                    aria-current={isActive(item.to) ? 'page' : undefined}
                    className={`flex items-center gap-2 rounded-lg px-3 py-2.5 text-sm font-semibold ${
                      isActive(item.to) ? 'bg-teal-50 text-teal-900' : 'text-slate-700 hover:bg-slate-100'
                    }`}
                  >
                    <Icon className="h-4 w-4" aria-hidden="true" />
                    {item.label}
                  </Link>
                );
              })}
              {learningUser && (
                <Link
                  to="/my-learning"
                  onClick={() => setMobileOpen(false)}
                  className="flex items-center gap-2 rounded-lg px-3 py-2.5 text-sm font-semibold text-slate-700 hover:bg-slate-100"
                >
                  <UserRound className="h-4 w-4" aria-hidden="true" />
                  My learning
                </Link>
              )}
            </nav>
            <div className="mt-4 flex flex-col gap-2 border-t border-slate-200 pt-4">
              {user ? (
                <>
                  <div className="truncate px-1 text-xs text-slate-500" title={user.email}>
                    Signed in as <span className="font-semibold text-slate-700">{user.email}</span>
                  </div>
                  {canAuthor(user) && (
                    <Link to="/author" onClick={() => setMobileOpen(false)} className="lc-button lc-button-secondary">
                      <NotebookPen className="h-4 w-4" aria-hidden="true" /> Author studio
                    </Link>
                  )}
                  {backOffice && (
                    <a href="/dashboard" className="lc-button lc-button-secondary">
                      <LayoutDashboard className="h-4 w-4" aria-hidden="true" /> Operations dashboard
                    </a>
                  )}
                  <button type="button" onClick={handleSignOut} disabled={signingOut} className="lc-button lc-button-secondary">
                    <LogOut className="h-4 w-4" aria-hidden="true" /> Sign out
                  </button>
                </>
              ) : (
                authReady && (
                  <>
                    <Link to="/login" onClick={() => setMobileOpen(false)} className="lc-button lc-button-secondary">
                      Sign in
                    </Link>
                    <Link to="/register" onClick={() => setMobileOpen(false)} className="lc-button lc-button-primary">
                      Create free account
                    </Link>
                  </>
                )
              )}
            </div>
          </div>
        )}
      </header>

      <main
        id="main-content"
        tabIndex={-1}
        className="w-full flex-1 outline-none"
      >
        {children}
      </main>

      <footer className="mt-16 border-t border-slate-200 bg-white">
        <div className="mx-auto grid w-full max-w-7xl gap-8 px-4 py-10 sm:px-6 lg:grid-cols-[1.5fr_1fr_1fr] lg:px-8">
          <div>
            <div className="flex items-center gap-2 font-bold text-slate-950">
              <ShieldCheck className="h-5 w-5 text-teal-800" aria-hidden="true" />
              AdControl Learning
            </div>
            <p className="mt-3 max-w-md text-sm leading-6 text-slate-600">
              Free, practical marketing education from the team behind AdControl. Learn the craft, then measure it honestly.
            </p>
            <p className="mt-4 text-xs leading-5 text-slate-500">
              Courses are educational. They do not automatically change any live advertising campaign or website.
            </p>
          </div>
          <div>
            <h2 className="text-xs font-bold uppercase tracking-[0.16em] text-slate-500">Learn</h2>
            <ul className="mt-3 space-y-2 text-sm">
              <li><Link to="/courses" className="text-slate-700 hover:text-teal-900">All courses</Link></li>
              <li><Link to="/topics" className="text-slate-700 hover:text-teal-900">Topics</Link></li>
              <li><Link to="/learning-paths" className="text-slate-700 hover:text-teal-900">Learning paths</Link></li>
              <li><Link to="/my-learning" className="text-slate-700 hover:text-teal-900">My learning</Link></li>
            </ul>
          </div>
          <div>
            <h2 className="text-xs font-bold uppercase tracking-[0.16em] text-slate-500">About</h2>
            <ul className="mt-3 space-y-2 text-sm">
              <li><Link to="/become-a-publisher" className="text-slate-700 hover:text-teal-900">Become a publisher</Link></li>
              <li><Link to="/become-a-client" className="text-slate-700 hover:text-teal-900">Become a client</Link></li>
              <li><Link to="/docs" className="text-slate-700 hover:text-teal-900">Documentation</Link></li>
              <li><Link to="/privacy" className="text-slate-700 hover:text-teal-900">Privacy</Link></li>
              <li><Link to="/terms" className="text-slate-700 hover:text-teal-900">Terms of Service</Link></li>
              <li><Link to="/accessibility" className="text-slate-700 hover:text-teal-900">Accessibility</Link></li>
              <li><a href="/dashboard" className="text-slate-700 hover:text-teal-900">Operations dashboard</a></li>
              <li>
                <a href="https://marketing.nevatal.id" className="inline-flex items-center gap-1 text-slate-700 hover:text-teal-900">
                  marketing.nevatal.id <ExternalLink className="h-3 w-3" aria-hidden="true" />
                </a>
              </li>
            </ul>
          </div>
        </div>
        <div className="border-t border-slate-100">
          <div className="mx-auto flex w-full max-w-7xl flex-col gap-1 px-4 py-4 text-xs text-slate-500 sm:flex-row sm:items-center sm:justify-between sm:px-6 lg:px-8">
            <span>© {new Date().getFullYear()} AdControl Learning · This project is under <a href="https://nevatal.id" className="text-teal-800 underline" target="_blank" rel="noopener noreferrer">nevatal.id</a> for educational purposes.</span>
            <span>Built for practical, responsible marketing.</span>
          </div>
        </div>
      </footer>
    </div>
  );
};
