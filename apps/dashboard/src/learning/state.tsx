import React, {
  DependencyList,
  ReactNode,
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from 'react';
import { learningApi } from './api';
import { LearningUser } from './types';

interface AuthValue {
  user: LearningUser | null;
  authReady: boolean;
  setUser: (user: LearningUser | null) => void;
  signOut: () => Promise<void>;
}

const AuthContext = createContext<AuthValue | null>(null);

export const AuthProvider: React.FC<{ children: ReactNode }> = ({ children }) => {
  const [user, setUser] = useState<LearningUser | null>(null);
  const [authReady, setAuthReady] = useState(false);

  useEffect(() => {
    let active = true;
    const token = localStorage.getItem('adcontrol_token');
    if (!token) {
      setAuthReady(true);
      return () => {
        active = false;
      };
    }
    learningApi
      .getMe()
      .then((nextUser) => {
        if (active) setUser(nextUser);
      })
      .catch(() => {
        if (active) setUser(null);
      })
      .finally(() => {
        if (active) setAuthReady(true);
      });
    return () => {
      active = false;
    };
  }, []);

  useEffect(() => {
    const handleUnauthorized = () => {
      setUser(null);
      setAuthReady(true);
    };
    window.addEventListener('learning:unauthorized', handleUnauthorized);
    return () => window.removeEventListener('learning:unauthorized', handleUnauthorized);
  }, []);

  const signOut = useCallback(async () => {
    try {
      await learningApi.logout();
    } finally {
      setUser(null);
    }
  }, []);

  const value = useMemo<AuthValue>(
    () => ({ user, authReady, setUser, signOut }),
    [authReady, signOut, user],
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
};

export function useAuth(): AuthValue {
  const value = useContext(AuthContext);
  if (!value) throw new Error('useAuth must be used inside AuthProvider');
  return value;
}

export interface RemoteState<T> {
  data: T | undefined;
  error: unknown;
  loading: boolean;
  reload: () => void;
}

export function useRemote<T>(load: () => Promise<T>, deps: DependencyList): RemoteState<T> {
  const [data, setData] = useState<T | undefined>(undefined);
  const [error, setError] = useState<unknown>(null);
  const [loading, setLoading] = useState(true);
  const [version, setVersion] = useState(0);

  useEffect(() => {
    let active = true;
    setLoading(true);
    setError(null);
    load()
      .then((result) => {
        if (active) setData(result);
      })
      .catch((err) => {
        if (active) setError(err);
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => {
      active = false;
    };
    // The caller owns the dependency list; load closes over those values.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [...deps, version]);

  const reload = useCallback(() => setVersion((current) => current + 1), []);
  return { data, error, loading, reload };
}

export function formatError(error: unknown): string {
  if (error instanceof Error && error.message) return error.message;
  return 'Something went wrong. Please try again.';
}

export function formatDate(value?: string): string {
  if (!value) return 'Not set';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
}

export function formatDateTime(value?: string): string {
  if (!value) return 'Not set';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

export function formatDuration(minutes: number): string {
  if (!minutes) return 'Self-paced';
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  const remainder = minutes % 60;
  return remainder ? `${hours} hr ${remainder} min` : `${hours} hr`;
}

export function formatMoney(cents: number | undefined, currency = 'USD'): string {
  const amount = (cents || 0) / 100;
  try {
    return new Intl.NumberFormat(undefined, {
      style: 'currency',
      currency: currency || 'USD',
    }).format(amount);
  } catch (_) {
    return `${(currency || 'USD').toUpperCase()} ${amount.toFixed(2)}`;
  }
}

export function levelLabel(level: string): string {
  if (level === 'advanced') return 'Advanced';
  if (level === 'intermediate') return 'Intermediate';
  return 'Beginner';
}

export function statusLabel(status: string): string {
  return status
    .split('_')
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(' ');
}

export function resourceKindLabel(kind: string): string {
  if (kind === 'download') return 'Download';
  if (kind === 'tool') return 'Tool';
  return 'Link';
}

export function roleLabel(role: string): string {
  return statusLabel(role);
}

export function isLearningUser(user: LearningUser | null): boolean {
  if (!user) return false;
  return user.role === 'learner' || user.role === 'instructor' || user.role === 'editor' || user.role === 'admin';
}

export function canAuthor(user: LearningUser | null): boolean {
  return !!user && (user.role === 'instructor' || user.role === 'editor' || user.role === 'admin');
}

export function canReview(user: LearningUser | null): boolean {
  return !!user && (user.role === 'editor' || user.role === 'admin');
}

export function slugify(value: string): string {
  return value
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .replace(/-{2,}/g, '-')
    .slice(0, 120);
}

export function loginPath(next: string): string {
  return `/login?next=${encodeURIComponent(next)}`;
}
