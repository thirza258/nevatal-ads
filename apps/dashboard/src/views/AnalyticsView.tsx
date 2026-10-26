import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { api } from '../api';
import {
  AnalyticsBreakdownRow,
  AnalyticsSummary,
  Campaign,
  Placement,
  Publisher,
  Site,
  TimeseriesPoint,
} from '../types';
import { TrendChart } from '../components/TrendChart';
import {
  BarChart3,
  CalendarRange,
  Download,
  ExternalLink,
  Eye,
  Info,
  Loader2,
  MousePointerClick,
  Percent,
  RefreshCw,
  Ticket,
} from 'lucide-react';

type BreakdownDimension = 'site' | 'placement' | 'campaign' | 'source';

interface AnalyticsViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  showPublisherFilter?: boolean;
}

const numberFormat = new Intl.NumberFormat('en-US');
const formatCount = (value: number) => numberFormat.format(Math.round(value));
const formatPercent = (value: number) => `${Number(value || 0).toFixed(2)}%`;

function todayInput(): string {
  return new Date().toISOString().slice(0, 10);
}

function daysAgoInput(days: number): string {
  const date = new Date();
  date.setUTCDate(date.getUTCDate() - (days - 1));
  return date.toISOString().slice(0, 10);
}

const EMPTY_SUMMARY: AnalyticsSummary = {
  impressions: 0,
  clicks: 0,
  outbound_redirects: 0,
  affiliate_clicks: 0,
  ctr: 0,
  confirmed_conversions: 0,
  confirmed_commission: 0,
};

export const AnalyticsView: React.FC<AnalyticsViewProps> = ({ onNotify, showPublisherFilter = false }) => {
  const [sites, setSites] = useState<Site[]>([]);
  const [placements, setPlacements] = useState<Placement[]>([]);
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [publishers, setPublishers] = useState<Publisher[]>([]);

  const [publisherFilter, setPublisherFilter] = useState('all');
  const [siteFilter, setSiteFilter] = useState('all');
  const [placementFilter, setPlacementFilter] = useState('all');
  const [campaignFilter, setCampaignFilter] = useState('all');
  const [preset, setPreset] = useState<'7' | '30' | '90' | 'custom'>('7');
  const [startDate, setStartDate] = useState(() => daysAgoInput(7));
  const [endDate, setEndDate] = useState(() => todayInput());
  const [dimension, setDimension] = useState<BreakdownDimension>('campaign');

  const [summary, setSummary] = useState<AnalyticsSummary>(EMPTY_SUMMARY);
  const [timeseries, setTimeseries] = useState<TimeseriesPoint[]>([]);
  const [breakdown, setBreakdown] = useState<AnalyticsBreakdownRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [exporting, setExporting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reloadKey, setReloadKey] = useState(0);

  useEffect(() => {
    let cancelled = false;
    const loadOptions = async () => {
      try {
        const [siteList, placementList, campaignList, publisherList] = await Promise.all([
          api.getSites(),
          api.getPlacements(),
          api.getCampaigns(),
          showPublisherFilter ? api.getPublishers().catch(() => [] as Publisher[]) : Promise.resolve([] as Publisher[]),
        ]);
        if (cancelled) return;
        setSites(siteList);
        setPlacements(placementList);
        setCampaigns(campaignList);
        setPublishers(publisherList);
      } catch (err: any) {
        if (!cancelled) onNotify('error', err.message || 'Failed to load analytics filters');
      }
    };
    loadOptions();
    return () => {
      cancelled = true;
    };
  }, [onNotify, showPublisherFilter]);

  const filterPayload = useMemo(
    () => ({
      publisher_id: publisherFilter !== 'all' ? publisherFilter : undefined,
      site_id: siteFilter !== 'all' ? siteFilter : undefined,
      placement_id: placementFilter !== 'all' ? placementFilter : undefined,
      campaign_id: campaignFilter !== 'all' ? campaignFilter : undefined,
      start_date: startDate || undefined,
      end_date: endDate || undefined,
    }),
    [publisherFilter, siteFilter, placementFilter, campaignFilter, startDate, endDate],
  );

  useEffect(() => {
    let cancelled = false;
    const invalidRange = Boolean(startDate && endDate && startDate > endDate);
    if (invalidRange) {
      setError('The start date must be on or before the end date.');
      setLoading(false);
      return;
    }

    setLoading(true);
    setError(null);
    const load = async () => {
      try {
        const [summaryData, seriesData, breakdownData] = await Promise.all([
          api.getAnalyticsSummary(filterPayload),
          api.getAnalyticsTimeseries(filterPayload),
          api.getAnalyticsBreakdown({ ...filterPayload, dimension }),
        ]);
        if (cancelled) return;
        setSummary(summaryData);
        setTimeseries(seriesData);
        setBreakdown(breakdownData);
      } catch (err: any) {
        if (!cancelled) {
          setError(err.message || 'Failed to load analytics');
          onNotify('error', err.message || 'Failed to load analytics');
        }
      } finally {
        if (!cancelled) setLoading(false);
      }
    };
    load();
    return () => {
      cancelled = true;
    };
  }, [filterPayload, dimension, reloadKey, onNotify]);

  const placementOptions = useMemo(() => {
    if (siteFilter === 'all') return [];
    return placements.filter((placement) => placement.site_id === siteFilter);
  }, [placements, siteFilter]);

  const campaignOptions = useMemo(() => {
    return campaigns.filter((campaign) => {
      if (siteFilter !== 'all' && campaign.site_id !== siteFilter) return false;
      if (placementFilter !== 'all' && campaign.placement_id !== placementFilter) return false;
      return true;
    });
  }, [campaigns, siteFilter, placementFilter]);

  const handlePreset = (value: '7' | '30' | '90') => {
    setPreset(value);
    setStartDate(daysAgoInput(Number(value)));
    setEndDate(todayInput());
  };

  const siteOptions = useMemo(() => {
    if (publisherFilter === 'all') return sites;
    return sites.filter((site) => site.publisher_id === publisherFilter);
  }, [sites, publisherFilter]);

  const handlePublisherChange = (value: string) => {
    setPublisherFilter(value);
    setSiteFilter('all');
    setPlacementFilter('all');
    setCampaignFilter('all');
  };

  const handleSiteChange = (value: string) => {
    setSiteFilter(value);
    setPlacementFilter('all');
    setCampaignFilter('all');
  };

  const handlePlacementChange = (value: string) => {
    setPlacementFilter(value);
    setCampaignFilter('all');
  };

  const handleExport = useCallback(async () => {
    setExporting(true);
    try {
      await api.downloadAnalyticsCsv(filterPayload);
      onNotify('success', 'CSV export downloaded.');
    } catch (err: any) {
      onNotify('error', err.message || 'CSV export failed');
    } finally {
      setExporting(false);
    }
  }, [filterPayload, onNotify]);

  const impressionData = timeseries.map((point) => ({ label: point.date, value: point.impressions }));
  const clickData = timeseries.map((point) => ({ label: point.date, value: point.clicks }));
  const ctrData = timeseries.map((point) => ({ label: point.date, value: point.ctr }));

  const rangeLabel =
    startDate || endDate ? `${startDate || 'start'} to ${endDate || 'today'} (UTC)` : 'All time (UTC)';

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col lg:flex-row lg:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Analytics</h2>
          <p className="text-sm text-slate-500 mt-1">
            Impressions, clicks, and CTR for {rangeLabel}. All dates are UTC.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={() => setReloadKey((value) => value + 1)}
            className="inline-flex items-center gap-1.5 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 px-3 py-2 rounded-lg transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
            Refresh
          </button>
          <button
            onClick={handleExport}
            disabled={exporting}
            className="inline-flex items-center gap-1.5 text-xs font-semibold text-white bg-blue-600 hover:bg-blue-700 disabled:opacity-60 px-3 py-2 rounded-lg transition-colors"
          >
            {exporting ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <Download className="w-3.5 h-3.5" />}
            {exporting ? 'Exporting…' : 'Export CSV'}
          </button>
        </div>
      </div>

      {/* One filter row scoping everything below it. */}
      <div className="bg-white border border-slate-200 rounded-xl p-3 flex flex-wrap items-center gap-2 shadow-sm">
        <div className="flex items-center gap-1 bg-slate-100 rounded-lg p-1">
          {(['7', '30', '90'] as const).map((value) => (
            <button
              key={value}
              onClick={() => handlePreset(value)}
              className={`px-2.5 py-1.5 text-xs font-semibold rounded-md transition-colors ${
                preset === value ? 'bg-white text-slate-900 shadow-sm' : 'text-slate-500 hover:text-slate-800'
              }`}
              aria-pressed={preset === value}
            >
              {value === '7' ? 'Last 7 days' : value === '30' ? 'Last 30 days' : 'Last 90 days'}
            </button>
          ))}
          <button
            onClick={() => setPreset('custom')}
            className={`px-2.5 py-1.5 text-xs font-semibold rounded-md transition-colors flex items-center gap-1 ${
              preset === 'custom' ? 'bg-white text-slate-900 shadow-sm' : 'text-slate-500 hover:text-slate-800'
            }`}
            aria-pressed={preset === 'custom'}
          >
            <CalendarRange className="w-3.5 h-3.5" /> Custom
          </button>
        </div>

        <label className="flex items-center gap-1.5 text-xs text-slate-500">
          From
          <input
            type="date"
            value={startDate}
            max={endDate || undefined}
            onChange={(event) => {
              setPreset('custom');
              setStartDate(event.target.value);
            }}
            className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
          />
        </label>
        <label className="flex items-center gap-1.5 text-xs text-slate-500">
          To
          <input
            type="date"
            value={endDate}
            min={startDate || undefined}
            onChange={(event) => {
              setPreset('custom');
              setEndDate(event.target.value);
            }}
            className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
          />
        </label>

        <div className="h-6 w-px bg-slate-200 mx-1 hidden sm:block" />

        {showPublisherFilter && (
          <select
            value={publisherFilter}
            onChange={(event) => handlePublisherChange(event.target.value)}
            className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
            aria-label="Filter by publisher"
          >
            <option value="all">All publishers</option>
            {publishers.map((publisher) => (
              <option key={publisher.id} value={publisher.id}>
                {publisher.name}
              </option>
            ))}
          </select>
        )}

        <select
          value={siteFilter}
          onChange={(event) => handleSiteChange(event.target.value)}
          className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
          aria-label="Filter by website"
        >
          <option value="all">All websites</option>
          {siteOptions.map((site) => (
            <option key={site.id} value={site.id}>
              {site.name}
            </option>
          ))}
        </select>

        <select
          value={placementFilter}
          onChange={(event) => handlePlacementChange(event.target.value)}
          className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white disabled:opacity-50"
          aria-label="Filter by placement"
          disabled={siteFilter === 'all'}
        >
          <option value="all">{siteFilter === 'all' ? 'All placements' : 'All site placements'}</option>
          {placementOptions.map((placement) => (
            <option key={placement.id} value={placement.id}>
              {placement.name}
            </option>
          ))}
        </select>

        <select
          value={campaignFilter}
          onChange={(event) => setCampaignFilter(event.target.value)}
          className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
          aria-label="Filter by campaign"
        >
          <option value="all">All campaigns</option>
          {campaignOptions.map((campaign) => (
            <option key={campaign.id} value={campaign.id}>
              {campaign.name}
            </option>
          ))}
        </select>
      </div>

      {error && (
        <div className="rounded-lg border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-800">
          {error}
        </div>
      )}

      <div className={`flex flex-col gap-6 transition-opacity ${loading ? 'opacity-60' : 'opacity-100'}`}>
        {/* KPI row: observed delivery metrics only; commissions are not connected. */}
        <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-5 gap-4">
          <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-slate-500">
              <span className="w-2 h-2 rounded-full" style={{ background: 'var(--series-1)' }} />
              <Eye className="w-3.5 h-3.5" /> Impressions
            </div>
            <div className="mt-2 text-3xl font-bold text-slate-900">{formatCount(summary.impressions)}</div>
            <p className="mt-1 text-xs text-slate-500">At least 50% visible for 1 second</p>
          </div>
          <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-slate-500">
              <span className="w-2 h-2 rounded-full" style={{ background: 'var(--series-2)' }} />
              <MousePointerClick className="w-3.5 h-3.5" /> Clicks
            </div>
            <div className="mt-2 text-3xl font-bold text-slate-900">{formatCount(summary.clicks)}</div>
            <p className="mt-1 text-xs text-slate-500">Signed click redirects observed</p>
          </div>
          <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-slate-500">
              <ExternalLink className="w-3.5 h-3.5" /> Outbound redirects
            </div>
            <div className="mt-2 text-3xl font-bold text-slate-900">{formatCount(summary.outbound_redirects)}</div>
            <p className="mt-1 text-xs text-slate-500">Visitors redirected off-site via /r/</p>
          </div>
          <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-slate-500">
              <Ticket className="w-3.5 h-3.5" /> Affiliate clicks
            </div>
            <div className="mt-2 text-3xl font-bold text-slate-900">{formatCount(summary.affiliate_clicks)}</div>
            <p className="mt-1 text-xs text-slate-500">Clicks on affiliate-sourced creatives</p>
          </div>
          <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-slate-500">
              <span className="w-2 h-2 rounded-full" style={{ background: 'var(--series-3)' }} />
              <Percent className="w-3.5 h-3.5" /> Click-through rate
            </div>
            <div className="mt-2 text-3xl font-bold text-slate-900">{formatPercent(summary.ctr)}</div>
            <p className="mt-1 text-xs text-slate-500">Clicks ÷ impressions</p>
          </div>
        </div>

        <div className="flex items-start gap-2 rounded-lg border border-sky-200 bg-sky-50 px-4 py-3 text-xs text-sky-900">
          <Info className="w-4 h-4 shrink-0 mt-0.5" />
          <p>
            These are <span className="font-semibold">AdControl-observed delivery metrics</span>. They are not
            externally confirmed conversions or commissions: no affiliate-network API is connected, so confirmed
            conversions ({formatCount(summary.confirmed_conversions)}) and confirmed commission (
            {summary.confirmed_commission.toLocaleString()}) remain zero until an approved integration exists.
          </p>
        </div>

        {/* Small multiples: separate scales, never a dual axis. */}
        <div className="grid grid-cols-1 xl:grid-cols-3 gap-4">
          <TrendChart
            title="Impressions per day"
            subtitle="Qualifying views"
            data={impressionData}
            colorVar="--series-1"
            formatValue={(value) => formatCount(value)}
          />
          <TrendChart
            title="Clicks per day"
            subtitle="Recorded click events"
            data={clickData}
            colorVar="--series-2"
            formatValue={(value) => formatCount(value)}
          />
          <TrendChart
            title="CTR per day"
            subtitle="Clicks as a share of impressions"
            data={ctrData}
            colorVar="--series-3"
            formatValue={(value) => formatPercent(value)}
          />
        </div>

        {/* Breakdown */}
        <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
          <div className="px-5 py-4 border-b border-slate-100 flex flex-col sm:flex-row sm:items-center justify-between gap-3">
            <div className="flex items-center gap-2">
              <BarChart3 className="w-4 h-4 text-slate-500" />
              <h3 className="text-sm font-semibold text-slate-900">Performance breakdown</h3>
            </div>
            <label className="flex items-center gap-2 text-xs text-slate-500">
              Dimension
              <select
                value={dimension}
                onChange={(event) => setDimension(event.target.value as BreakdownDimension)}
                className="px-2 py-1.5 text-xs border border-slate-200 rounded-md text-slate-800 bg-white"
              >
                <option value="campaign">Campaign</option>
                <option value="placement">Placement</option>
                <option value="site">Website</option>
                <option value="source">Ad source</option>
              </select>
            </label>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-5 py-3 font-semibold">
                    {dimension === 'site'
                      ? 'Website'
                      : dimension === 'placement'
                      ? 'Placement'
                      : dimension === 'source'
                      ? 'Ad source'
                      : 'Campaign'}
                  </th>
                  <th className="px-5 py-3 font-semibold text-right">Impressions</th>
                  <th className="px-5 py-3 font-semibold text-right">Clicks</th>
                  <th className="px-5 py-3 font-semibold text-right">Outbound</th>
                  <th className="px-5 py-3 font-semibold text-right">CTR</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {breakdown.map((row) => (
                  <tr key={row.key} className="hover:bg-slate-50/70">
                    <td className="px-5 py-3 text-slate-800 font-medium">{row.name}</td>
                    <td className="px-5 py-3 text-right text-slate-700 tabular-nums">{formatCount(row.impressions)}</td>
                    <td className="px-5 py-3 text-right text-slate-700 tabular-nums">{formatCount(row.clicks)}</td>
                    <td className="px-5 py-3 text-right text-slate-700 tabular-nums">
                      {formatCount(row.outbound_redirects || 0)}
                    </td>
                    <td className="px-5 py-3 text-right text-slate-700 tabular-nums">{formatPercent(row.ctr)}</td>
                  </tr>
                ))}
                {breakdown.length === 0 && (
                  <tr>
                    <td colSpan={5} className="px-5 py-10 text-center text-sm text-slate-500">
                      No events match these filters.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>

        {/* Accessible table view for the charts. */}
        <details className="bg-white border border-slate-200 rounded-xl shadow-sm">
          <summary className="px-5 py-4 cursor-pointer text-sm font-semibold text-slate-800 select-none">
            Daily totals (table view)
          </summary>
          <div className="overflow-x-auto max-h-80 overflow-y-auto border-t border-slate-100">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500 sticky top-0">
                <tr>
                  <th className="px-5 py-2.5 font-semibold">Date (UTC)</th>
                  <th className="px-5 py-2.5 font-semibold text-right">Impressions</th>
                  <th className="px-5 py-2.5 font-semibold text-right">Clicks</th>
                  <th className="px-5 py-2.5 font-semibold text-right">CTR</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {timeseries.map((point) => (
                  <tr key={point.date}>
                    <td className="px-5 py-2.5 text-slate-700 tabular-nums">{point.date}</td>
                    <td className="px-5 py-2.5 text-right text-slate-700 tabular-nums">{formatCount(point.impressions)}</td>
                    <td className="px-5 py-2.5 text-right text-slate-700 tabular-nums">{formatCount(point.clicks)}</td>
                    <td className="px-5 py-2.5 text-right text-slate-700 tabular-nums">{formatPercent(point.ctr)}</td>
                  </tr>
                ))}
                {timeseries.length === 0 && (
                  <tr>
                    <td colSpan={4} className="px-5 py-8 text-center text-sm text-slate-500">
                      No daily data for this range.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </details>
      </div>
    </div>
  );
};
