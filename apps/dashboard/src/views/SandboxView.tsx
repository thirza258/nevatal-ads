import React, { useCallback, useEffect, useState } from 'react';
import { api } from '../api';
import { SandboxData, SandboxItem, Site } from '../types';
import {
  AlertTriangle,
  Check,
  Copy,
  ExternalLink,
  FlaskConical,
  Loader2,
  RefreshCw,
  ShieldCheck,
  ShoppingBag,
} from 'lucide-react';

interface SandboxViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

function SandboxCreative({ item }: { item: SandboxItem }) {
  const { placement, campaign, affiliate_offer: offer } = item;
  const [copied, setCopied] = useState(false);
  const format = campaign?.format || placement.format;
  const imageUrl = campaign?.image_url || '';
  const title = campaign?.title || campaign?.name || 'No active campaign';
  const body = campaign?.body || '';
  const cta = campaign?.cta_label || 'Learn more';
  const disclosure =
    item.disclosure_text ||
    (campaign?.source_type === 'affiliate'
      ? offer?.disclosure_text || 'Affiliate disclosure: we may earn a commission from purchases made through this link.'
      : '');
  const maxWidth = placement.width > 0 ? Math.min(placement.width, 680) : 680;
  const minHeight =
    placement.height > 0 ? Math.min(placement.height, 360) : format === 'text_link' ? 52 : 160;

  if (!campaign) {
    return (
      <div className="rounded-lg border border-dashed border-slate-300 bg-slate-50 p-5 text-sm text-slate-500">
        No deliverable campaign for placement <code className="font-mono text-slate-700">{placement.placement_key}</code>.
        Create and publish a campaign, or check its schedule and the site status.
      </div>
    );
  }

  return (
    <div
      className="rounded-lg border border-slate-200 bg-white overflow-hidden shadow-sm"
      style={{ maxWidth: `${maxWidth}px` }}
    >
      <div className="flex items-center justify-between px-3 py-1.5 bg-slate-50 border-b border-slate-200">
        <span className="flex items-center gap-1.5">
          <span className="text-[10px] font-bold uppercase tracking-wider text-slate-500 bg-slate-200/70 px-1.5 py-0.5 rounded">
            Ad
          </span>
          <span className="text-[10px] font-semibold uppercase tracking-wider text-slate-400">
            {campaign.source_type || 'house'}
          </span>
        </span>
        <span className="text-[10px] text-slate-400 font-mono">{placement.placement_key}</span>
      </div>

      {format === 'product_card' ? (
        <div className="flex flex-col">
          <div className="flex items-center justify-between px-4 py-2 bg-emerald-50/60 border-b border-emerald-100">
            <span className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wider text-emerald-800">
              <ShoppingBag className="w-3 h-3" /> Sponsored product
            </span>
            <span className="text-[10px] font-semibold text-emerald-700 truncate max-w-[180px]">
              {offer?.merchant_name || campaign.source_type || 'Product'}
            </span>
          </div>
          <div className="flex flex-col sm:flex-row">
            <div
              className="sm:w-2/5 bg-slate-100 flex items-center justify-center overflow-hidden"
              style={{ minHeight: `${minHeight}px` }}
            >
              {imageUrl ? (
                <img
                  src={imageUrl}
                  alt={campaign.alt_text || title}
                  className="w-full h-full object-cover"
                  loading="lazy"
                  referrerPolicy="no-referrer"
                  style={{ maxHeight: 220 }}
                />
              ) : (
                <div className="p-6 text-center text-xs text-slate-400">Product image required</div>
              )}
            </div>
            <div className="p-4 flex flex-col gap-2 flex-1 min-w-0">
              <h4 className="text-base font-semibold text-slate-900 leading-snug">{title}</h4>
              {body && <p className="text-xs text-slate-600 leading-relaxed line-clamp-3">{body}</p>}
              <div className="mt-auto pt-2 flex items-center justify-between gap-3">
                {offer?.price_text ? (
                  <span className="text-sm font-bold text-slate-900">{offer.price_text}</span>
                ) : (
                  <span className="text-[11px] text-slate-400">Price not provided</span>
                )}
                <span className="inline-block bg-emerald-600 text-white text-xs font-semibold px-3 py-1.5 rounded-md">
                  {cta}
                </span>
              </div>
            </div>
          </div>
          {disclosure && (
            <div className="px-4 py-2 border-t border-slate-100 flex items-start gap-1.5 text-[10px] leading-snug text-slate-500">
              <ShieldCheck className="w-3 h-3 shrink-0 mt-0.5 text-emerald-600" />
              <span>{disclosure}</span>
            </div>
          )}
        </div>
      ) : format === 'text_link' ? (
        <div className="px-4 py-3 flex items-center gap-2 min-h-[52px]">
          <a
            href={item.click_url || '#'}
            onClick={(event) => event.preventDefault()}
            className="text-sm font-semibold text-blue-700 underline underline-offset-2 flex items-center gap-1"
            title="Preview only"
          >
            {title} <ExternalLink className="w-3.5 h-3.5" />
          </a>
        </div>
      ) : (
        <div className="flex flex-col sm:flex-row">
          {(format === 'responsive_banner' || format === 'image' || format === 'native_card') && (
            <div
              className={`${format === 'native_card' ? 'sm:w-2/5' : 'w-full'} bg-slate-100 flex items-center justify-center overflow-hidden`}
              style={{ minHeight: `${minHeight}px` }}
            >
              {imageUrl ? (
                <img
                  src={imageUrl}
                  alt={campaign.alt_text || title}
                  className="w-full h-full object-cover"
                  loading="lazy"
                  referrerPolicy="no-referrer"
                  style={{ maxHeight: format === 'native_card' ? 220 : 360 }}
                />
              ) : (
                <div className="p-6 text-center">
                  <div className="text-sm font-semibold text-slate-600">{title}</div>
                  <div className="text-xs text-slate-400 mt-1">No creative image set</div>
                </div>
              )}
            </div>
          )}
          <div className="p-4 flex flex-col gap-2 flex-1 min-w-0">
            <h4 className="text-base font-semibold text-slate-900 leading-snug">{title}</h4>
            {body && <p className="text-xs text-slate-600 leading-relaxed line-clamp-3">{body}</p>}
            <div className="mt-auto pt-2 flex items-center justify-between gap-3">
              <span className="inline-block bg-blue-600 text-white text-xs font-semibold px-3 py-1.5 rounded-md">
                {cta}
              </span>
              <span className="text-[11px] text-slate-400 truncate max-w-[200px]" title={campaign.destination_url}>
                {campaign.destination_url}
              </span>
            </div>
          </div>
        </div>
      )}

      <div className="px-3 py-2 border-t border-slate-100 flex items-center justify-between gap-2">
        <span className="text-[11px] text-slate-500">
          Status: <strong className="text-slate-700">{campaign.status}</strong>
          {campaign.starts_at && <span className="ml-2">from {campaign.starts_at}</span>}
          {campaign.ends_at && <span className="ml-1">to {campaign.ends_at}</span>}
        </span>
        {item.click_url && (
          <button
            onClick={async () => {
              try {
                await navigator.clipboard.writeText(new URL(item.click_url!, window.location.origin).toString());
                setCopied(true);
                window.setTimeout(() => setCopied(false), 1600);
              } catch (_) {
                // Clipboard access can be blocked; the URL remains visible on hover.
              }
            }}
            className="inline-flex items-center gap-1 text-[11px] font-semibold text-blue-600 hover:text-blue-800"
          >
            {copied ? <Check className="w-3 h-3" /> : <Copy className="w-3 h-3" />}
            {copied ? 'Copied' : 'Click URL'}
          </button>
        )}
      </div>
    </div>
  );
}

export const SandboxView: React.FC<SandboxViewProps> = ({ onNotify }) => {
  const [sites, setSites] = useState<Site[]>([]);
  const [selectedSite, setSelectedSite] = useState('');
  const [data, setData] = useState<SandboxData | null>(null);
  const [loading, setLoading] = useState(true);
  const [copied, setCopied] = useState(false);

  const loadSites = useCallback(async () => {
    try {
      const siteList = await api.getSites();
      setSites(siteList);
      if (!selectedSite && siteList.length > 0) setSelectedSite(siteList[0].id);
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load sites');
      setLoading(false);
    }
  }, [selectedSite, onNotify]);

  useEffect(() => {
    loadSites();
  }, [loadSites]);

  const loadSandbox = useCallback(async () => {
    if (!selectedSite) {
      setLoading(false);
      return;
    }
    setLoading(true);
    try {
      setData(await api.getSandbox(selectedSite));
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load sandbox preview');
      setData(null);
    } finally {
      setLoading(false);
    }
  }, [selectedSite, onNotify]);

  useEffect(() => {
    loadSandbox();
  }, [loadSandbox]);

  const snippet = data
    ? `<!-- Load once per page (shared head/body) -->
<script async src="${window.location.origin}/sdk/v1/adcontrol.js" data-site-key="${data.site.public_site_key}"></script>

<!-- Repeat one container per placement key -->
${data.placements
  .map((item) => `<div data-adcontrol-placement="${item.placement.placement_key}"></div>`)
  .join('\n')}`
    : '';

  const copySnippet = async () => {
    try {
      await navigator.clipboard.writeText(snippet);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1600);
      onNotify('success', 'Integration snippet copied.');
    } catch (_) {
      onNotify('error', 'Clipboard access was blocked by the browser.');
    }
  };

  const demoUrl = data
    ? (() => {
        const params = new URLSearchParams({ site_key: data.site.public_site_key });
        const placementKeys = data.placements.map((item) => item.placement.placement_key);
        if (placementKeys.length > 0) params.set('placements', placementKeys.join(','));
        return `/demo/?${params.toString()}`;
      })()
    : '';

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900 flex items-center gap-2">
            <FlaskConical className="w-6 h-6 text-slate-500" /> Live sandbox
          </h2>
          <p className="text-sm text-slate-500 mt-1">
            Preview exactly what the delivery API would serve for a site. This preview does not record impressions
            or clicks.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <select
            value={selectedSite}
            onChange={(event) => setSelectedSite(event.target.value)}
            className="px-3 py-2 text-xs border border-slate-200 rounded-lg bg-white text-slate-800"
            aria-label="Sandbox website"
          >
            {sites.length === 0 && <option value="">No websites</option>}
            {sites.map((site) => (
              <option key={site.id} value={site.id}>
                {site.name}
              </option>
            ))}
          </select>
          <button
            onClick={loadSandbox}
            disabled={loading || !selectedSite}
            className="inline-flex items-center gap-1.5 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-60 px-3 py-2 rounded-lg transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
          </button>
        </div>
      </div>

      <div className="flex items-start gap-2 rounded-lg border border-amber-200 bg-amber-50 p-3 text-xs text-amber-900 leading-relaxed">
        <AlertTriangle className="w-4 h-4 shrink-0 mt-0.5" />
        <div>
          Public delivery and click redirects enforce the site's Origin/Referer allowlist. Add this dashboard's
          host or your real website origin to the site allowlist before testing the SDK end-to-end. Click URLs are
          signed and expire after 24 hours.
        </div>
      </div>

      {sites.length === 0 && !loading ? (
        <div className="bg-white border border-slate-200 rounded-xl p-10 text-center text-sm text-slate-500">
          Create a website and placement first, then return to the sandbox.
        </div>
      ) : loading && !data ? (
        <div className="flex items-center justify-center h-48 text-slate-500">
          <Loader2 className="w-6 h-6 animate-spin" />
        </div>
      ) : data ? (
        <>
          {/* Simulated host page */}
          <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
            <div className="flex items-center gap-2 px-4 py-2.5 bg-slate-100 border-b border-slate-200">
              <div className="flex items-center gap-1.5">
                <span className="w-2.5 h-2.5 rounded-full bg-rose-400" />
                <span className="w-2.5 h-2.5 rounded-full bg-amber-400" />
                <span className="w-2.5 h-2.5 rounded-full bg-emerald-400" />
              </div>
              <div className="flex-1 mx-3 bg-white border border-slate-200 rounded-md px-3 py-1 text-[11px] text-slate-500 truncate">
                https://{data.site.primary_domain}/ — simulated host page
              </div>
              <span className="text-[10px] uppercase tracking-wider text-slate-400 font-semibold">Preview</span>
            </div>
            <div className="p-6 bg-slate-50/60 flex flex-col gap-6">
              <div className="h-3 w-1/2 bg-slate-200 rounded" />
              <div className="h-2 w-3/4 bg-slate-200/70 rounded" />
              {data.placements.length === 0 ? (
                <div className="rounded-lg border border-dashed border-slate-300 bg-white p-8 text-center text-sm text-slate-500">
                  This site has no active placements. Create a placement to preview it here.
                </div>
              ) : (
                data.placements.map((item) => (
                  <div key={item.placement.id} className="flex flex-col gap-2">
                    <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-400">
                      {item.placement.name}
                    </div>
                    <SandboxCreative item={item} />
                  </div>
                ))
              )}
              <div className="h-2 w-2/3 bg-slate-200/70 rounded" />
            </div>
          </div>

          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-5 flex flex-col gap-3">
              <div className="flex items-center justify-between">
                <h3 className="text-sm font-semibold text-slate-900">Integration snippet</h3>
                <button
                  onClick={copySnippet}
                  className="inline-flex items-center gap-1 text-xs font-semibold text-blue-600 hover:text-blue-800"
                >
                  {copied ? <Check className="w-3.5 h-3.5" /> : <Copy className="w-3.5 h-3.5" />}
                  {copied ? 'Copied' : 'Copy'}
                </button>
              </div>
              <pre className="text-[11px] leading-relaxed bg-slate-900 text-slate-100 rounded-lg p-4 overflow-x-auto">
                {snippet}
              </pre>
              <p className="text-xs text-slate-500 leading-relaxed">
                The SDK is asynchronous and fails open: if delivery is unavailable, your page keeps working and the
                placement stays empty (or uses reserved space per the placement fallback setting).
              </p>
            </div>

            <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-5 flex flex-col gap-3">
              <h3 className="text-sm font-semibold text-slate-900">Hosted demo page</h3>
              <p className="text-xs text-slate-500 leading-relaxed">
                The deployment includes a static demo host at <code className="bg-slate-100 px-1 py-0.5 rounded">/demo/</code>.
                It loads the real SDK. Add this host to the allowlist to let it fetch config and record events.
              </p>
              <div className="flex flex-wrap items-center gap-2">
                <a
                  href={demoUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-1.5 text-xs font-semibold text-white bg-blue-600 hover:bg-blue-700 px-3 py-2 rounded-lg"
                >
                  <ExternalLink className="w-3.5 h-3.5" /> Open demo
                </a>
                <span className="text-[11px] text-slate-400 font-mono break-all">{demoUrl}</span>
              </div>
              <div className="mt-1 text-xs text-slate-600 space-y-1.5">
                <p>
                  <span className="font-semibold text-slate-800">Allowed domains:</span>{' '}
                  {data.allowed_domains.join(', ') || data.site.primary_domain}
                </p>
                <p>
                  <span className="font-semibold text-slate-800">Site key:</span>{' '}
                  <code className="font-mono text-[11px] break-all">{data.site.public_site_key}</code>
                </p>
                <p>
                  <span className="font-semibold text-slate-800">Config version:</span> {data.site.config_version}
                </p>
              </div>
            </div>
          </div>
        </>
      ) : (
        <div className="bg-white border border-slate-200 rounded-xl p-10 text-center text-sm text-slate-500">
          Select a website to preview its delivery configuration.
        </div>
      )}
    </div>
  );
};
