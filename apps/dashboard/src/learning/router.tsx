import React, {
  AnchorHTMLAttributes,
  createContext,
  ReactNode,
  MouseEvent,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from 'react';

interface LocationState {
  path: string;
  search: string;
  hash: string;
}

interface NavigateOptions {
  replace?: boolean;
  scroll?: boolean;
}

interface RouterValue extends LocationState {
  navigate: (to: string, options?: NavigateOptions) => void;
}

const RouterContext = createContext<RouterValue | null>(null);

function currentLocation(): LocationState {
  return {
    path: window.location.pathname || '/',
    search: window.location.search,
    hash: window.location.hash,
  };
}

function parseTarget(to: string): URL | null {
  try {
    const target = new URL(to, window.location.origin);
    if (target.origin !== window.location.origin) return null;
    return target;
  } catch (_) {
    return null;
  }
}

export function safeInternalPath(raw: string | null | undefined, fallback = '/'): string {
  if (!raw) return fallback;
  const target = parseTarget(raw);
  if (!target) return fallback;
  if (!target.pathname.startsWith('/') || target.pathname.startsWith('//')) return fallback;
  if (target.pathname === window.location.pathname &&
      target.search === window.location.search &&
      target.hash === window.location.hash) {
    return fallback;
  }
  return `${target.pathname}${target.search}${target.hash}`;
}

export const RouterProvider: React.FC<{ children: ReactNode }> = ({ children }) => {
  const [location, setLocation] = useState<LocationState>(currentLocation);

  useEffect(() => {
    const onPopState = () => setLocation(currentLocation());
    window.addEventListener('popstate', onPopState);
    return () => window.removeEventListener('popstate', onPopState);
  }, []);

  const navigate = useCallback((to: string, options: NavigateOptions = {}) => {
    const target = parseTarget(to);
    if (!target) return;
    const next = `${target.pathname}${target.search}${target.hash}`;
    if (options.replace) window.history.replaceState(null, '', next);
    else window.history.pushState(null, '', next);
    setLocation(currentLocation());
    if (options.scroll !== false) window.scrollTo({ top: 0, left: 0, behavior: 'auto' });
  }, []);

  const value = useMemo<RouterValue>(() => ({ ...location, navigate }), [location, navigate]);
  return <RouterContext.Provider value={value}>{children}</RouterContext.Provider>;
};

export function useRouter(): RouterValue {
  const value = useContext(RouterContext);
  if (!value) throw new Error('useRouter must be used inside RouterProvider');
  return value;
}

type LinkProps = AnchorHTMLAttributes<HTMLAnchorElement> & {
  to: string;
  children: ReactNode;
};

export const Link: React.FC<LinkProps> = ({ to, children, onClick, ...rest }) => {
  const { navigate } = useRouter();
  const external = /^https?:\/\//i.test(to);

  const handleClick = (event: MouseEvent<HTMLAnchorElement>) => {
    onClick?.(event);
    if (event.defaultPrevented || external) return;
    if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    if (rest.target && rest.target !== '_self') return;
    event.preventDefault();
    navigate(to);
  };

  return (
    <a href={to} onClick={handleClick} {...rest}>
      {children}
    </a>
  );
};
