import React, { FormEvent, useState } from 'react';
import { ArrowRight, GraduationCap, LayoutDashboard, LockKeyhole, ShieldCheck } from 'lucide-react';
import { learningApi } from '../api';
import { Container } from '../components';
import { Link, safeInternalPath, useRouter } from '../router';
import { useSeo } from '../seo';
import { formatError, isLearningUser, useAuth } from '../state';

function nextPath(search: string, fallback: string): string {
  const requested = new URLSearchParams(search).get('next');
  const safe = safeInternalPath(requested, fallback);
  if (safe === '/dashboard' || safe.startsWith('/dashboard/')) return fallback;
  return safe;
}

const AuthShell: React.FC<{
  title: string;
  subtitle: string;
  children: React.ReactNode;
  footer: React.ReactNode;
}> = ({ title, subtitle, children, footer }) => (
  <Container className="py-12 sm:py-16">
    <div className="mx-auto grid max-w-5xl gap-8 lg:grid-cols-[minmax(0,1fr)_26rem] lg:items-start">
      <div className="hidden lg:block">
        <div className="lc-eyebrow">AdControl Learning</div>
        <h2 className="mt-3 text-4xl font-bold leading-tight tracking-[-0.03em] text-slate-950">
          Learn the craft. <br />Measure it honestly.
        </h2>
        <p className="mt-4 max-w-md text-base leading-7 text-slate-600">
          A free learning account keeps your progress and gives you a place to return to. All
          courses and features are 100% free with no payment details, billing, or subscription required.
        </p>
        <ul className="mt-8 space-y-4 text-sm text-slate-700">
          <li className="flex items-start gap-3">
            <span className="mt-0.5 flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-teal-50 text-teal-800">
              <GraduationCap className="h-4 w-4" aria-hidden="true" />
            </span>
            <span><strong className="font-semibold text-slate-900">Structured courses.</strong> Short lessons, practical exercises, and checks for understanding.</span>
          </li>
          <li className="flex items-start gap-3">
            <span className="mt-0.5 flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-teal-50 text-teal-800">
              <ShieldCheck className="h-4 w-4" aria-hidden="true" />
            </span>
            <span><strong className="font-semibold text-slate-900">Private by default.</strong> No tracking ads, no named-learner analytics, and a one-click data export.</span>
          </li>
          <li className="flex items-start gap-3">
            <span className="mt-0.5 flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-teal-50 text-teal-800">
              <LockKeyhole className="h-4 w-4" aria-hidden="true" />
            </span>
            <span><strong className="font-semibold text-slate-900">No automatic changes.</strong> Exercises never alter a live campaign or publisher site.</span>
          </li>
        </ul>
      </div>

      <div className="lc-card p-6 sm:p-8">
        <h1 className="text-2xl font-bold tracking-tight text-slate-950">{title}</h1>
        <p className="mt-2 text-sm leading-6 text-slate-600">{subtitle}</p>
        <div className="mt-6">{children}</div>
        <div className="mt-6 border-t border-slate-100 pt-5 text-sm text-slate-600">{footer}</div>
      </div>
    </div>
  </Container>
);

export const LoginPage: React.FC = () => {
  const { search, navigate } = useRouter();
  const { user, authReady, setUser, signOut } = useAuth();
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const fallback = '/my-learning';
  const destination = nextPath(search, fallback);

  useSeo({
    title: 'Sign in',
    description: 'Sign in to AdControl Learning to continue a course and keep your progress.',
    noindex: true,
  });

  const handleSubmit = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    setSubmitting(true);
    try {
      const response = await learningApi.login(email.trim(), password);
      setUser(response.user);
      if (!isLearningUser(response.user)) {
        window.location.assign('/dashboard');
        return;
      }
      navigate(destination, { replace: true });
    } catch (err) {
      setError(formatError(err));
    } finally {
      setSubmitting(false);
    }
  };

  if (authReady && user) {
    return (
      <Container className="py-16">
        <div className="lc-card mx-auto max-w-md p-8 text-center">
          <h1 className="text-2xl font-bold tracking-tight text-slate-950">You are already signed in</h1>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            Signed in as <strong className="font-semibold text-slate-900">{user.email}</strong>.
          </p>
          <div className="mt-6 flex flex-col gap-2">
            {isLearningUser(user) ? (
              <Link to={destination} className="lc-button lc-button-primary">
                Continue <ArrowRight className="h-4 w-4" aria-hidden="true" />
              </Link>
            ) : (
              <a href="/dashboard" className="lc-button lc-button-primary">
                <LayoutDashboard className="h-4 w-4" aria-hidden="true" />
                Open operations dashboard
              </a>
            )}
            <button type="button" onClick={() => signOut()} className="lc-button lc-button-secondary">
              Sign out and use another account
            </button>
          </div>
        </div>
      </Container>
    );
  }

  return (
    <AuthShell
      title="Sign in"
      subtitle="Continue a course, track your progress, and pick up where you left off."
      footer={
        <div className="space-y-2">
          <p>
            New here?{' '}
            <Link to={`/register?next=${encodeURIComponent(destination)}`} className="lc-link">
              Create a free account
            </Link>
            .
          </p>
          <p className="text-xs leading-5 text-slate-500">
            Advertising operations accounts use the{' '}
            <a href="/dashboard" className="font-semibold text-slate-600 underline underline-offset-2 hover:text-teal-900">
              operations dashboard
            </a>
            .
          </p>
        </div>
      }
    >
      <form onSubmit={handleSubmit} className="space-y-4" noValidate>
        {error && (
          <div className="rounded-lg border border-rose-200 bg-rose-50 p-3 text-sm text-rose-900" role="alert">
            {error}
          </div>
        )}
        <div>
          <label htmlFor="login-email" className="lc-label">Email address</label>
          <input
            id="login-email"
            type="email"
            required
            autoComplete="email"
            value={email}
            onChange={(event) => setEmail(event.target.value)}
            className="lc-input"
            placeholder="you@example.com"
          />
        </div>
        <div>
          <label htmlFor="login-password" className="lc-label">Password</label>
          <input
            id="login-password"
            type="password"
            required
            autoComplete="current-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            className="lc-input"
          />
        </div>
        <button type="submit" disabled={submitting} className="lc-button lc-button-primary w-full">
          {submitting ? 'Signing in…' : 'Sign in'}
        </button>
      </form>
    </AuthShell>
  );
};

export const RegisterPage: React.FC = () => {
  const { search, navigate } = useRouter();
  const { user, authReady, setUser } = useAuth();
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [confirm, setConfirm] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const destination = nextPath(search, '/my-learning');

  useSeo({
    title: 'Create your free account',
    description: 'Create a free AdControl Learning account to enroll in courses and save progress.',
    noindex: true,
  });

  const handleSubmit = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    if (password.length < 12) {
      setError('Use a password of at least 12 characters.');
      return;
    }
    if (password !== confirm) {
      setError('The two passwords do not match.');
      return;
    }
    setSubmitting(true);
    try {
      const response = await learningApi.register(email.trim(), password);
      setUser(response.user);
      navigate(destination, { replace: true });
    } catch (err) {
      setError(formatError(err));
    } finally {
      setSubmitting(false);
    }
  };

  if (authReady && user) {
    return (
      <Container className="py-16">
        <div className="lc-card mx-auto max-w-md p-8 text-center">
          <h1 className="text-2xl font-bold tracking-tight text-slate-950">You are already signed in</h1>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            Signed in as <strong className="font-semibold text-slate-900">{user.email}</strong>.
          </p>
          <div className="mt-6">
            {isLearningUser(user) ? (
              <Link to={destination} className="lc-button lc-button-primary">Continue learning</Link>
            ) : (
              <a href="/dashboard" className="lc-button lc-button-primary">Open operations dashboard</a>
            )}
          </div>
        </div>
      </Container>
    );
  }

  return (
    <AuthShell
      title="Create your free account"
      subtitle="One account for every course. Create it here, then choose a course from the catalog."
      footer={
        <p>
          Already have an account?{' '}
          <Link to={`/login?next=${encodeURIComponent(destination)}`} className="lc-link">Sign in</Link>
          . By creating an account you agree to the{' '}
          <Link to="/privacy" className="lc-link">privacy notice</Link>.
        </p>
      }
    >
      <form onSubmit={handleSubmit} className="space-y-4" noValidate>
        {error && (
          <div className="rounded-lg border border-rose-200 bg-rose-50 p-3 text-sm text-rose-900" role="alert">
            {error}
          </div>
        )}
        <div>
          <label htmlFor="register-email" className="lc-label">Email address</label>
          <input
            id="register-email"
            type="email"
            required
            autoComplete="email"
            value={email}
            onChange={(event) => setEmail(event.target.value)}
            className="lc-input"
            placeholder="you@example.com"
          />
        </div>
        <div>
          <label htmlFor="register-password" className="lc-label">Password</label>
          <input
            id="register-password"
            type="password"
            required
            minLength={12}
            maxLength={128}
            autoComplete="new-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            className="lc-input"
            aria-describedby="register-password-hint"
          />
          <p id="register-password-hint" className="mt-1.5 text-xs leading-5 text-slate-500">
            At least 12 characters. A passphrase is easier to remember and harder to guess.
          </p>
        </div>
        <div>
          <label htmlFor="register-confirm" className="lc-label">Confirm password</label>
          <input
            id="register-confirm"
            type="password"
            required
            minLength={12}
            maxLength={128}
            autoComplete="new-password"
            value={confirm}
            onChange={(event) => setConfirm(event.target.value)}
            className="lc-input"
          />
        </div>
        <button type="submit" disabled={submitting} className="lc-button lc-button-primary w-full">
          {submitting ? 'Creating your account…' : 'Create free account'}
        </button>
        <p className="text-center text-xs leading-5 text-slate-500">
          Creating an account does not enroll you in a course automatically. Choose a course after signing in.
        </p>
      </form>
    </AuthShell>
  );
};
