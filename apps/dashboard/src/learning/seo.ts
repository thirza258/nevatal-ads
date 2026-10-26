import { useEffect } from 'react';

export const DEFAULT_TITLE = 'AdControl Learning — practical marketing skills';
export const DEFAULT_DESCRIPTION =
  'Free, practical courses on digital marketing, discovery, paid growth, affiliate publishing, and honest measurement from AdControl.';

export function canonicalOrigin(): string {
  return window.location.origin.replace(/\/+$/, '');
}

function upsertMeta(attribute: 'name' | 'property', key: string, content: string) {
  let element = document.head.querySelector<HTMLMetaElement>(`meta[${attribute}="${key}"]`);
  if (!element) {
    element = document.createElement('meta');
    element.setAttribute(attribute, key);
    document.head.appendChild(element);
  }
  element.setAttribute('content', content);
}

function upsertCanonical(href: string | null) {
  let element = document.head.querySelector<HTMLLinkElement>('link[rel="canonical"]');
  if (!href) {
    element?.remove();
    return;
  }
  if (!element) {
    element = document.createElement('link');
    element.rel = 'canonical';
    document.head.appendChild(element);
  }
  element.href = href;
}

export interface SeoOptions {
  title: string;
  description: string;
  path?: string;
  noindex?: boolean;
  type?: 'website' | 'article';
}

export function useSeo({ title, description, path, noindex = false, type = 'website' }: SeoOptions) {
  useEffect(() => {
    const fullTitle = title === DEFAULT_TITLE ? title : `${title} — AdControl Learning`;
    document.title = fullTitle;
    const canonical = noindex ? null : `${canonicalOrigin()}${path || window.location.pathname}`;

    upsertMeta('name', 'description', description);
    upsertMeta('name', 'robots', noindex ? 'noindex, nofollow' : 'index, follow');
    upsertMeta('property', 'og:title', fullTitle);
    upsertMeta('property', 'og:description', description);
    upsertMeta('property', 'og:type', type);
    upsertMeta('property', 'og:site_name', 'AdControl Learning');
    if (canonical) upsertMeta('property', 'og:url', canonical);
    upsertCanonical(canonical);
  }, [description, noindex, path, title, type]);
}
