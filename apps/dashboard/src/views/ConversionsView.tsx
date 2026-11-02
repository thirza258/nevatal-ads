import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  BadgeDollarSign,
  Check,
  CheckCircle2,
  Clipboard,
  Code2,
  ExternalLink,
  Filter,
  Loader2,
  Plus,
  RefreshCw,
  RotateCcw,
  Target,
  X,
} from 'lucide-react';
import { api } from '../api';
import {
  AffiliateOffer,
  Campaign,
  ConversionEvent,
  ConversionReconciliationRow,
  ConversionSource,
  ConversionStatus,
  ConversionSummary,
  Site,
} from '../types';
import { Modal } from '../components/Modal';

interface ConversionsViewProps {
  role: string;
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

const STATUS_STYLES: Record<ConversionStatus, string> = {
  pending: 'bg-amber-50 text-amber-700 border-amber-200',
  approved: 'bg-blue-50 text-blue-700 border-blue-200',
  reconciled: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  rejected: 'bg-rose-50 text-rose-700 border-rose-200',
};

function formatMoney(cents: number, currency = 'USD'): string {
  try {
    return new Intl.NumberFormat('en-US', {
      style: 'currency',
      currency: currency || 'USD',
      maximumFractionDigits: 2,
    }).format((cents || 0) / 100);
  } catch (_) {
    return `${currency || 'USD'} ${((cents || 0) / 100).toFixed(2)}`;
  }
}

function formatNumber(value: number): string {
  return new Intl.NumberFormat('en-US').format(value || 0);
}

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function dateDaysAgo(days: number): string {
  const date = new Date();
  date.setDate(date.getDate() - days);
  return date.toISOString().slice(0, 10);
}

function today(): string {
  return new Date().toISOString().slice(0, 10);
}

function shortUrl(value: string): string {
  if (!value) return '';
  try {
    const url = new URL(value);
    return url.hostname + (url.pathname === '/' ? '' : url.pathname);
  } catch (_) {
    return value;
  }
}

const EMPTY_SUMMARY: ConversionSummary = {
  conversions: 0,
  pending_conversions: 0,
  approved_conversions: 0,
  rejected_conversions: 0,
  reconciled_conversions: 0,
  gross_value_cents: 0,
  commission_cents: 0,
  approved_value_cents: 0,
  approved_commission_cents: 0,
  impressions: 0,
  clicks: 0,
  ctr: 0,
  conversion_rate: 0,
  funnel: [],
  provenance: {},
};

export const ConversionsView: React.FC<ConversionsViewProps> = ({ role, onNotify }) => {
  const canWrite = role === 'admin' || role === 'publisher';
  const [sites, setSites] = useState<Site[]>([]);
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [offers, setOffers] = useState<AffiliateOffer[]>([]);
  const [sources, setSources] = useState<ConversionSource[]>([]);
  const [summary, setSummary] = useState<ConversionSummary>(EMPTY_SUMMARY);
  const [conversions, setConversions] = useState<ConversionEvent[]>([]);
  const [reconciliation, setReconciliation] = useState<ConversionReconciliationRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [siteFilter, setSiteFilter] = useState('all');
  const [statusFilter, setStatusFilter] = useState<'all' | ConversionStatus>('all');
  const [startDate, setStartDate] = useState(dateDaysAgo(30));
  const [endDate, setEndDate] = useState(today());

  const [sourceModal, setSourceModal] = useState(false);
  const [sourceSite, setSourceSite] = useState('');
  const [sourceName, setSourceName] = useState('');
  const [sourceCampaign, setSourceCampaign] = useState('');
  const [sourceOffer, setSourceOffer] = useState('');
  const [createdSource, setCreatedSource] = useState<ConversionSource | null>(null);

  const siteNames = useMemo(
    () => new Map(sites.map((site) => [site.id, site.name])),
    [sites],
  );
  const campaignNames = useMemo(
    () => new Map(campaigns.map((campaign) => [campaign.id, campaign.name])),
    [campaigns],
  );
  const sourceNames = useMemo(
    () => new Map(sources.map((source) => [source.id, source.name])),
    [sources],
  );

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const siteId = siteFilter === 'all' ? undefined : siteFilter;
      const [sourceRows, summaryData, conversionRows, reconciliationRows] = await Promise.all([
        api.getConversionSources({ site_id: siteId }),
        api.getConversionSummary({
          site_id: siteId,
          start_date: startDate,
          end_date: endDate,
        }),
        api.getConversions({
          site_id: siteId,
          status: statusFilter === 'all' ? undefined : statusFilter,
          start_date: startDate,
          end_date: endDate,
          limit: 250,
        }),
        api.getConversionReconciliation({
          site_id: siteId,
          start_date: startDate,
          end_date: endDate,
        }),
      ]);
      setSources(sourceRows);
      setSummary(summaryData);
      setConversions(conversionRows);
      setReconciliation(reconciliationRows);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load conversion data');
    } finally {
      setLoading(false);
    }
  }, [endDate, onNotify, siteFilter, startDate, statusFilter]);

  useEffect(() => {
    let cancelled = false;
    Promise.all([api.getSites(), api.getCampaigns(), api.getAffiliateOffers()])
      .then(([siteRows, campaignRows, offerRows]) => {
        if (cancelled) return;
        setSites(siteRows);
        setCampaigns(campaignRows);
        setOffers(offerRows);
        if (siteRows.length > 0) setSourceSite((current) => current || siteRows[0].id);
      })
      .catch((err: any) => {
        if (!cancelled) onNotify('error', err?.message || 'Failed to load sites and campaigns');
      });
    return () => {
      cancelled = true;
    };
  }, [onNotify]);

  useEffect(() => {
    load();
  }, [load]);

  const sourcesForSite = useMemo(
    () => campaigns.filter((campaign) => !sourceSite || campaign.site_id === sourceSite),
    [campaigns, sourceSite],
  );
  const offersForPublisher = useMemo(() => {
    const site = sites.find((row) => row.id === sourceSite);
    if (!site?.publisher_id) return offers;
    return offers.filter((offer) => offer.publisher_id === site.publisher_id);
  }, [offers, sites, sourceSite]);

  const funnelMax = Math.max(1, ...summary.funnel.map((step) => step.count));

  const openSourceModal = () => {
    setSourceModal(true);
    setSourceName('');
    setSourceCampaign('');
    setSourceOffer('');
    setSourceSite(sites[0]?.id || '');
  };

  const handleCreateSource = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!sourceSite || !sourceName.trim()) return;
    setBusy(true);
    try {
      const result = await api.createConversionSource({
        site_id: sourceSite,
        name: sourceName.trim(),
        default_campaign_id: sourceCampaign || undefined,
        default_offer_id: sourceOffer || undefined,
      });
      setSources((current) => [result.source, ...current]);
      setCreatedSource(result.source);
      setSourceModal(false);
      onNotify('success', 'Conversion source created. Save the signing secret now.');
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create conversion source');
    } finally {
      setBusy(false);
    }
  };

  const toggleSource = async (source: ConversionSource) => {
    const status = source.status === 'active' ? 'disabled' : 'active';
    setBusy(true);
    try {
      const updated = await api.updateConversionSource(source.id, { status });
      setSources((current) => current.map((row) => (row.id === updated.id ? updated : row)));
      onNotify('success', `Source ${status === 'active' ? 'enabled' : 'disabled'}.`);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update source');
    } finally {
      setBusy(false);
    }
  };

  const updateStatus = async (conversion: ConversionEvent, status: ConversionStatus) => {
    if (busy) return;
    let notes = '';
    if (status === 'rejected') {
      const reason = window.prompt('Reconciliation note (optional)', conversion.notes || '');
      if (reason === null) return;
      notes = reason;
    }
    setBusy(true);
    try {
      const updated = await api.updateConversion(conversion.id, { status, notes });
      setConversions((current) => current.map((row) => (row.id === updated.id ? updated : row)));
      onNotify('success', `Conversion marked ${status}.`);
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update conversion');
    } finally {
      setBusy(false);
    }
  };

  const copy = async (value: string, label: string) => {
    try {
      await navigator.clipboard.writeText(value);
      onNotify('success', `${label} copied.`);
    } catch (_) {
      onNotify('error', 'Clipboard access is unavailable.');
    }
  };

  const origin = window.location.origin;
  const snippet = createdSource
    ? `<script async src="${origin}/sdk/v1/conversion.js"\n        data-source-key="${createdSource.public_key}"></script>

<script>
  // After a completed purchase on the advertiser's thank-you page:
  AdControlConversion.track({
    event_name: 'purchase',
    order_id: 'ORDER-12345',
    value: 49.99,
    commission: 4.99,
    currency: 'USD'
  });
</script>`
    : '';

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col gap-4 xl:flex-row xl:items-end xl:justify-between">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900">
            <Target className="h-6 w-6 text-slate-500" /> Conversions & attribution
          </h2>
          <p className="mt-1 max-w-3xl text-sm text-slate-500">
            Confirm sales, attribute them to campaigns, reconcile affiliate commissions, and connect
            first-touch and last-touch UTM data. Impressions and clicks remain first-party AdControl
            observations; conversions come from your pixel, browser SDK, or signed webhook.
          </p>
        </div>
        {canWrite && (
          <button
            type="button"
            onClick={openSourceModal}
            className="inline-flex items-center gap-1.5 self-start rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white transition-colors hover:bg-blue-700"
          >
            <Plus className="h-3.5 w-3.5" /> New conversion source
          </button>
        )}
      </div>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Conversions</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-slate-900">{formatNumber(summary.conversions)}</div>
          <div className="mt-1 text-xs text-slate-500">{summary.pending_conversions} awaiting review</div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Approved revenue</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-emerald-700">
            {formatMoney(summary.approved_value_cents)}
          </div>
          <div className="mt-1 text-xs text-slate-500">
            {formatMoney(summary.approved_commission_cents)} commission
          </div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Click → conversion</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-blue-700">
            {summary.conversion_rate.toFixed(2)}%
          </div>
          <div className="mt-1 text-xs text-slate-500">
            {formatNumber(summary.clicks)} clicks / {formatNumber(summary.impressions)} impressions
          </div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Commission reported</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-slate-900">
            {formatMoney(summary.commission_cents)}
          </div>
          <div className="mt-1 text-xs text-slate-500">Across all reconciliation states</div>
        </div>
      </div>

      <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_22rem]">
        <div className="rounded-xl border border-slate-200 bg-white p-5">
          <div className="flex items-center justify-between gap-3">
            <h3 className="text-sm font-bold text-slate-800">Conversion funnel</h3>
            <span className="text-[11px] text-slate-400">Selected period</span>
          </div>
          <div className="mt-4 space-y-3">
            {summary.funnel.map((step, index) => {
              const width = Math.max(2, (step.count / funnelMax) * 100);
              const label = step.stage.replace(/_/g, ' ');
              return (
                <div key={step.stage}>
                  <div className="mb-1 flex items-center justify-between text-xs">
                    <span className="font-medium capitalize text-slate-700">{label}</span>
                    <span className="tabular-nums text-slate-500">{formatNumber(step.count)}</span>
                  </div>
                  <div className="h-2.5 overflow-hidden rounded-full bg-slate-100">
                    <div
                      className={`h-full rounded-full ${
                        index === 0 ? 'bg-slate-500' : index === 1 ? 'bg-blue-500' : index === 2 ? 'bg-teal-500' : 'bg-emerald-500'
                      }`}
                      style={{ width: `${width}%` }}
                    />
                  </div>
                </div>
              );
            })}
            {summary.funnel.length === 0 && (
              <p className="text-sm text-slate-500">No funnel data for this period.</p>
            )}
          </div>
        </div>

        <div className="rounded-xl border border-slate-200 bg-white p-5">
          <h3 className="text-sm font-bold text-slate-800">Data provenance</h3>
          <dl className="mt-3 space-y-2 text-xs">
            {Object.entries(summary.provenance).map(([key, value]) => (
              <div key={key} className="flex items-start justify-between gap-3">
                <dt className="capitalize text-slate-500">{key}</dt>
                <dd className="max-w-[12rem] text-right font-medium text-slate-700">{value.replace(/_/g, ' ')}</dd>
              </div>
            ))}
          </dl>
          <p className="mt-3 border-t border-slate-100 pt-3 text-[11px] leading-5 text-slate-400">
            A click is not a sale. Only a pixel, SDK, or HMAC-signed webhook creates a conversion.
            Approved commission is shown separately from AdControl-observed advertising metrics.
          </p>
        </div>
      </div>

      <div className="rounded-xl border border-slate-200 bg-white shadow-sm">
        <div className="flex flex-col gap-3 border-b border-slate-100 p-4 lg:flex-row lg:items-center">
          <div className="flex flex-wrap items-center gap-2">
            <Filter className="h-4 w-4 text-slate-400" aria-hidden="true" />
            <label className="sr-only" htmlFor="conversion-site-filter">Site</label>
            <select
              id="conversion-site-filter"
              value={siteFilter}
              onChange={(event) => setSiteFilter(event.target.value)}
              className="rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none"
            >
              <option value="all">All sites</option>
              {sites.map((site) => (
                <option key={site.id} value={site.id}>{site.name}</option>
              ))}
            </select>
            <label className="sr-only" htmlFor="conversion-status-filter">Status</label>
            <select
              id="conversion-status-filter"
              value={statusFilter}
              onChange={(event) => setStatusFilter(event.target.value as 'all' | ConversionStatus)}
              className="rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none"
            >
              <option value="all">All statuses</option>
              <option value="pending">Pending</option>
              <option value="approved">Approved</option>
              <option value="reconciled">Reconciled</option>
              <option value="rejected">Rejected</option>
            </select>
            <input
              type="date"
              value={startDate}
              onChange={(event) => setStartDate(event.target.value)}
              aria-label="Start date"
              className="rounded-lg border border-slate-200 bg-white px-2 py-2 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
            />
            <span className="text-xs text-slate-400">to</span>
            <input
              type="date"
              value={endDate}
              onChange={(event) => setEndDate(event.target.value)}
              aria-label="End date"
              className="rounded-lg border border-slate-200 bg-white px-2 py-2 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
            />
          </div>
          <button
            type="button"
            onClick={load}
            disabled={loading}
            className="ml-auto inline-flex items-center gap-1.5 rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50 disabled:opacity-60"
          >
            <RefreshCw className={`h-3.5 w-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
          </button>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
              <tr>
                <th className="px-4 py-3 font-semibold">Occurred</th>
                <th className="px-4 py-3 font-semibold">Event / order</th>
                <th className="px-4 py-3 font-semibold">Source</th>
                <th className="px-4 py-3 font-semibold">Campaign</th>
                <th className="px-4 py-3 font-semibold">Value</th>
                <th className="px-4 py-3 font-semibold">Commission</th>
                <th className="px-4 py-3 font-semibold">Attribution</th>
                <th className="px-4 py-3 font-semibold">Status</th>
                {canWrite && <th className="px-4 py-3 text-right font-semibold">Actions</th>}
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {conversions.map((conversion) => {
                const lastTouch = conversion.last_touch || {};
                const firstTouch = conversion.first_touch || {};
                return (
                  <tr key={conversion.id} className="align-top hover:bg-slate-50/70">
                    <td className="whitespace-nowrap px-4 py-3 text-xs tabular-nums text-slate-600">
                      {formatDate(conversion.occurred_at)}
                    </td>
                    <td className="px-4 py-3">
                      <div className="font-medium text-slate-900">{conversion.event_name}</div>
                      <div className="text-[11px] text-slate-500">
                        {conversion.order_id || conversion.external_id}
                      </div>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-600">
                      <div>{sourceNames.get(conversion.source_id) || '—'}</div>
                      <div className="text-[11px] text-slate-400">{siteNames.get(conversion.site_id) || conversion.site_id}</div>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-600">
                      {campaignNames.get(conversion.campaign_id) || conversion.campaign_id || '—'}
                    </td>
                    <td className="whitespace-nowrap px-4 py-3 text-xs font-semibold tabular-nums text-slate-800">
                      {formatMoney(conversion.value_cents, conversion.currency)}
                    </td>
                    <td className="whitespace-nowrap px-4 py-3 text-xs tabular-nums text-emerald-700">
                      {formatMoney(conversion.commission_cents, conversion.currency)}
                    </td>
                    <td className="max-w-[17rem] px-4 py-3 text-[11px] text-slate-500">
                      <div className="truncate" title={conversion.page_url || ''}>
                        {shortUrl(conversion.page_url) || '—'}
                      </div>
                      <div className="mt-0.5 truncate" title={String(lastTouch.utm_campaign || '')}>
                        {conversion.utm_source || String(firstTouch.utm_source || '') || 'direct'}
                        {conversion.utm_medium ? ` / ${conversion.utm_medium}` : ''}
                        {lastTouch.utm_campaign ? ` / ${String(lastTouch.utm_campaign)}` : ''}
                      </div>
                      <div className="mt-0.5 text-slate-400">
                        {conversion.ingestion === 'webhook' ? 'S2S webhook' : conversion.ingestion}
                      </div>
                    </td>
                    <td className="px-4 py-3">
                      <span className={`inline-flex rounded-full border px-2 py-0.5 text-[11px] font-semibold ${STATUS_STYLES[conversion.status]}`}>
                        {conversion.status}
                      </span>
                      {conversion.notes && (
                        <div className="mt-1 max-w-[12rem] text-[11px] text-slate-500" title={conversion.notes}>
                          {conversion.notes}
                        </div>
                      )}
                    </td>
                    {canWrite && (
                      <td className="px-4 py-3 text-right">
                        <div className="inline-flex items-center gap-1">
                          {conversion.status === 'pending' && (
                            <>
                              <button
                                type="button"
                                onClick={() => updateStatus(conversion, 'approved')}
                                disabled={busy}
                                className="rounded-md border border-emerald-200 bg-emerald-50 p-1.5 text-emerald-700 hover:bg-emerald-100 disabled:opacity-50"
                                title="Approve"
                              >
                                <Check className="h-3.5 w-3.5" />
                              </button>
                              <button
                                type="button"
                                onClick={() => updateStatus(conversion, 'rejected')}
                                disabled={busy}
                                className="rounded-md border border-rose-200 bg-rose-50 p-1.5 text-rose-700 hover:bg-rose-100 disabled:opacity-50"
                                title="Reject"
                              >
                                <X className="h-3.5 w-3.5" />
                              </button>
                            </>
                          )}
                          {conversion.status === 'approved' && (
                            <button
                              type="button"
                              onClick={() => updateStatus(conversion, 'reconciled')}
                              disabled={busy}
                              className="inline-flex items-center gap-1 rounded-md border border-blue-200 bg-blue-50 px-2 py-1 text-[11px] font-semibold text-blue-700 hover:bg-blue-100 disabled:opacity-50"
                            >
                              <BadgeDollarSign className="h-3 w-3" /> Reconcile
                            </button>
                          )}
                        </div>
                      </td>
                    )}
                  </tr>
                );
              })}
              {!loading && conversions.length === 0 && (
                <tr>
                  <td colSpan={canWrite ? 9 : 8} className="px-4 py-12 text-center text-sm text-slate-500">
                    No conversions in this period. Create a source and send a test event.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
        {loading && (
          <div className="flex items-center justify-center gap-2 border-t border-slate-100 py-4 text-xs text-slate-500">
            <Loader2 className="h-3.5 w-3.5 animate-spin" /> Loading conversions…
          </div>
        )}
      </div>

      <div className="grid gap-4 xl:grid-cols-2">
        <div className="rounded-xl border border-slate-200 bg-white p-5">
          <div className="flex items-center justify-between">
            <h3 className="flex items-center gap-2 text-sm font-bold text-slate-800">
              <Code2 className="h-4 w-4 text-slate-400" /> Conversion sources
            </h3>
            {canWrite && (
              <button
                type="button"
                onClick={openSourceModal}
                className="text-xs font-semibold text-blue-700 hover:text-blue-900"
              >
                Add source
              </button>
            )}
          </div>
          <div className="mt-4 space-y-3">
            {sources.map((source) => (
              <div key={source.id} className="flex items-start justify-between gap-3 rounded-lg border border-slate-100 bg-slate-50/60 p-3">
                <div className="min-w-0">
                  <div className="flex items-center gap-2">
                    <span className="truncate text-sm font-semibold text-slate-800">{source.name}</span>
                    <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold ${
                      source.status === 'active'
                        ? 'border-emerald-200 bg-emerald-50 text-emerald-700'
                        : 'border-slate-200 bg-slate-100 text-slate-500'
                    }`}>
                      {source.status}
                    </span>
                  </div>
                  <div className="mt-1 truncate text-[11px] text-slate-500">
                    {siteNames.get(source.site_id) || source.site_id} ·{' '}
                    {campaignNames.get(source.default_campaign_id) || 'no default campaign'}
                  </div>
                  <div className="mt-1 font-mono text-[10px] text-slate-400">{source.public_key}</div>
                  <div className="text-[10px] text-slate-400">
                    Last event: {source.last_event_at ? formatDate(source.last_event_at) : 'never'}
                  </div>
                </div>
                {canWrite && (
                  <button
                    type="button"
                    onClick={() => toggleSource(source)}
                    disabled={busy}
                    className="shrink-0 rounded-md border border-slate-200 bg-white p-1.5 text-slate-500 hover:bg-slate-50 disabled:opacity-50"
                    title={source.status === 'active' ? 'Disable source' : 'Enable source'}
                  >
                    {source.status === 'active' ? <X className="h-3.5 w-3.5" /> : <RotateCcw className="h-3.5 w-3.5" />}
                  </button>
                )}
              </div>
            ))}
            {sources.length === 0 && (
              <p className="text-sm text-slate-500">No conversion sources yet. Create one to get a pixel, SDK key, and signed webhook URL.</p>
            )}
          </div>
        </div>

        <div className="rounded-xl border border-slate-200 bg-white p-5">
          <h3 className="flex items-center gap-2 text-sm font-bold text-slate-800">
            <BadgeDollarSign className="h-4 w-4 text-slate-400" /> Affiliate commission reconciliation
          </h3>
          <div className="mt-4 overflow-x-auto">
            <table className="w-full text-left text-xs">
              <thead className="text-[10px] uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="pb-2 font-semibold">Offer / campaign</th>
                  <th className="pb-2 font-semibold">Status</th>
                  <th className="pb-2 text-right font-semibold">Count</th>
                  <th className="pb-2 text-right font-semibold">Value</th>
                  <th className="pb-2 text-right font-semibold">Commission</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {reconciliation.map((row, index) => (
                  <tr key={`${row.offer_id}-${row.campaign_id}-${row.status}-${index}`}>
                    <td className="py-2 pr-3">
                      <div className="font-medium text-slate-800">
                        {row.offer_title || campaignNames.get(row.campaign_id) || row.offer_id || 'Unattributed'}
                      </div>
                      <div className="text-[10px] text-slate-400">
                        {row.campaign_name || campaignNames.get(row.campaign_id) || '—'}
                      </div>
                    </td>
                    <td className="py-2 pr-2">
                      <span className={`rounded-full border px-1.5 py-0.5 text-[10px] font-semibold ${STATUS_STYLES[row.status]}`}>
                        {row.status}
                      </span>
                    </td>
                    <td className="py-2 text-right tabular-nums text-slate-600">{formatNumber(row.conversions)}</td>
                    <td className="py-2 text-right tabular-nums text-slate-700">{formatMoney(row.value_cents)}</td>
                    <td className="py-2 text-right tabular-nums text-emerald-700">{formatMoney(row.commission_cents)}</td>
                  </tr>
                ))}
                {reconciliation.length === 0 && (
                  <tr>
                    <td colSpan={5} className="py-6 text-center text-slate-500">No commission data for this period.</td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <Modal isOpen={sourceModal} onClose={() => setSourceModal(false)} title="New conversion source">
        <form onSubmit={handleCreateSource} className="space-y-4">
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="conversion-source-site">Site</label>
            <select
              id="conversion-source-site"
              value={sourceSite}
              onChange={(event) => {
                setSourceSite(event.target.value);
                setSourceCampaign('');
                setSourceOffer('');
              }}
              className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              required
            >
              <option value="">Select a site</option>
              {sites.map((site) => (
                <option key={site.id} value={site.id}>{site.name} ({site.primary_domain})</option>
              ))}
            </select>
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="conversion-source-name">Name</label>
            <input
              id="conversion-source-name"
              value={sourceName}
              onChange={(event) => setSourceName(event.target.value)}
              placeholder="Thank-you page purchases"
              maxLength={120}
              className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              required
            />
          </div>
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="conversion-source-campaign">Default campaign</label>
              <select
                id="conversion-source-campaign"
                value={sourceCampaign}
                onChange={(event) => setSourceCampaign(event.target.value)}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              >
                <option value="">None</option>
                {sourcesForSite.map((campaign) => (
                  <option key={campaign.id} value={campaign.id}>{campaign.name}</option>
                ))}
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="conversion-source-offer">Default affiliate offer</label>
              <select
                id="conversion-source-offer"
                value={sourceOffer}
                onChange={(event) => setSourceOffer(event.target.value)}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              >
                <option value="">None</option>
                {offersForPublisher.map((offer) => (
                  <option key={offer.id} value={offer.id}>{offer.offer_name || offer.product_title}</option>
                ))}
              </select>
            </div>
          </div>
          <p className="rounded-lg bg-slate-50 p-3 text-xs leading-5 text-slate-500">
            The source key is public and only allows conversion events to be written. Server-to-server
            webhooks additionally require an HMAC signature generated with a secret that is shown once.
          </p>
          <div className="flex justify-end gap-2">
            <button
              type="button"
              onClick={() => setSourceModal(false)}
              className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={busy}
              className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60"
            >
              {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Plus className="h-3.5 w-3.5" />}
              Create source
            </button>
          </div>
        </form>
      </Modal>

      <Modal
        isOpen={!!createdSource}
        onClose={() => setCreatedSource(null)}
        title="Conversion source credentials"
        maxWidth="max-w-2xl"
      >
        {createdSource && (
          <div className="space-y-4">
            <div className="flex items-start gap-2 rounded-lg border border-amber-200 bg-amber-50 p-3 text-xs leading-5 text-amber-900">
              <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0" aria-hidden="true" />
              <span>
                Store the signing secret now. It is not shown again. The public source key is safe to
                include in the pixel or page snippet.
              </span>
            </div>
            <div>
              <div className="mb-1 text-xs font-semibold text-slate-700">Public source key</div>
              <div className="flex items-center gap-2">
                <code className="min-w-0 flex-1 break-all rounded-lg bg-slate-900 px-3 py-2 text-xs text-slate-100">
                  {createdSource.public_key}
                </code>
                <button
                  type="button"
                  onClick={() => copy(createdSource.public_key, 'Source key')}
                  className="rounded-lg border border-slate-200 p-2 text-slate-500 hover:bg-slate-50"
                  aria-label="Copy source key"
                >
                  <Clipboard className="h-4 w-4" />
                </button>
              </div>
            </div>
            <div>
              <div className="mb-1 text-xs font-semibold text-slate-700">Signing secret (shown once)</div>
              <div className="flex items-center gap-2">
                <code className="min-w-0 flex-1 break-all rounded-lg bg-slate-900 px-3 py-2 text-xs text-emerald-300">
                  {createdSource.signing_secret}
                </code>
                <button
                  type="button"
                  onClick={() => copy(createdSource.signing_secret || '', 'Signing secret')}
                  className="rounded-lg border border-slate-200 p-2 text-slate-500 hover:bg-slate-50"
                  aria-label="Copy signing secret"
                >
                  <Clipboard className="h-4 w-4" />
                </button>
              </div>
            </div>
            <div>
              <div className="mb-1 text-xs font-semibold text-slate-700">Browser snippet</div>
              <pre className="max-h-56 overflow-auto rounded-lg bg-slate-900 p-3 text-[11px] leading-5 text-slate-100">
                {snippet}
              </pre>
              <button
                type="button"
                onClick={() => copy(snippet, 'Snippet')}
                className="mt-2 inline-flex items-center gap-1.5 text-xs font-semibold text-blue-700 hover:text-blue-900"
              >
                <Clipboard className="h-3.5 w-3.5" /> Copy snippet
              </button>
            </div>
            <div className="grid gap-3 sm:grid-cols-2">
              <div>
                <div className="mb-1 text-xs font-semibold text-slate-700">Pixel URL</div>
                <code className="block break-all rounded-lg bg-slate-50 p-2 text-[10px] text-slate-600">
                  {origin}{createdSource.pixel_url}
                </code>
              </div>
              <div>
                <div className="mb-1 text-xs font-semibold text-slate-700">Webhook URL</div>
                <code className="block break-all rounded-lg bg-slate-50 p-2 text-[10px] text-slate-600">
                  {origin}{createdSource.webhook_url}
                </code>
              </div>
            </div>
            <div className="rounded-lg bg-slate-50 p-3 text-[11px] leading-5 text-slate-500">
              Webhook signature: <code>X-AdControl-Signature: sha256=&lt;hex HMAC-SHA256 of the raw body using the signing secret&gt;</code>.
              Use a unique <code>order_id</code> or <code>external_id</code> for idempotency.
            </div>
            <div className="flex justify-end">
              <button
                type="button"
                onClick={() => setCreatedSource(null)}
                className="inline-flex items-center gap-1.5 rounded-lg bg-slate-900 px-3 py-2 text-xs font-semibold text-white hover:bg-slate-800"
              >
                <ExternalLink className="h-3.5 w-3.5" /> Done
              </button>
            </div>
          </div>
        )}
      </Modal>
    </div>
  );
};
