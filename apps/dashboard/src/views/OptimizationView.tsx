import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  FlaskConical,
  Loader2,
  Pause,
  Play,
  Plus,
  RefreshCw,
  Trash2,
  Trophy,
  Users,
} from 'lucide-react';
import { api } from '../api';
import {
  AudienceSegment,
  Campaign,
  Experiment,
  Placement,
  Site,
} from '../types';
import { Modal } from '../components/Modal';

interface OptimizationViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

type OptimizationTab = 'experiments' | 'segments';

const STATUS_STYLES: Record<Experiment['status'], string> = {
  draft: 'border-slate-200 bg-slate-100 text-slate-600',
  running: 'border-emerald-200 bg-emerald-50 text-emerald-700',
  paused: 'border-amber-200 bg-amber-50 text-amber-700',
  completed: 'border-blue-200 bg-blue-50 text-blue-700',
};

const OBJECTIVE_LABELS: Record<Experiment['objective'], string> = {
  ctr: 'CTR',
  conversion_rate: 'Conversion rate',
  revenue: 'Revenue per 1,000 impressions',
};

function formatNumber(value?: number): string {
  return new Intl.NumberFormat('en-US').format(value || 0);
}

function formatPercent(value?: number): string {
  return `${(value || 0).toFixed(2)}%`;
}

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleDateString();
}

export const OptimizationView: React.FC<OptimizationViewProps> = ({ onNotify }) => {
  const [tab, setTab] = useState<OptimizationTab>('experiments');
  const [sites, setSites] = useState<Site[]>([]);
  const [placements, setPlacements] = useState<Placement[]>([]);
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [experiments, setExperiments] = useState<Experiment[]>([]);
  const [segments, setSegments] = useState<AudienceSegment[]>([]);
  const [loading, setLoading] = useState(true);
  const [busyId, setBusyId] = useState('');

  const [experimentModal, setExperimentModal] = useState(false);
  const [siteId, setSiteId] = useState('');
  const [placementId, setPlacementId] = useState('');
  const [experimentName, setExperimentName] = useState('');
  const [objective, setObjective] = useState<Experiment['objective']>('ctr');
  const [experimentStatus, setExperimentStatus] = useState<'draft' | 'running'>('draft');
  const [minImpressions, setMinImpressions] = useState(100);
  const [minLift, setMinLift] = useState(10);
  const [autoWinner, setAutoWinner] = useState(true);
  const [variantSelection, setVariantSelection] = useState<Record<string, { selected: boolean; weight: number; control: boolean }>>({});

  const [segmentModal, setSegmentModal] = useState(false);
  const [segmentSite, setSegmentSite] = useState('');
  const [segmentName, setSegmentName] = useState('');
  const [segmentDescription, setSegmentDescription] = useState('');
  const [segmentUtmSource, setSegmentUtmSource] = useState('');
  const [segmentUtmCampaign, setSegmentUtmCampaign] = useState('');
  const [segmentDevice, setSegmentDevice] = useState('');
  const [segmentReferrer, setSegmentReferrer] = useState('');

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const [experimentRows, segmentRows, siteRows, placementRows, campaignRows] = await Promise.all([
        api.getExperiments(),
        api.getAudienceSegments(),
        api.getSites(),
        api.getPlacements(),
        api.getCampaigns(),
      ]);
      setExperiments(experimentRows);
      setSegments(segmentRows);
      setSites(siteRows);
      setPlacements(placementRows);
      setCampaigns(campaignRows);
      setSiteId((current) => current || siteRows[0]?.id || '');
      setSegmentSite((current) => current || siteRows[0]?.id || '');
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load optimization data');
    } finally {
      setLoading(false);
    }
  }, [onNotify]);

  useEffect(() => {
    load();
  }, [load]);

  const sitePlacements = useMemo(
    () => placements.filter((placement) => placement.site_id === siteId),
    [placements, siteId],
  );
  const placementCampaigns = useMemo(
    () => campaigns.filter((campaign) => campaign.site_id === siteId && campaign.placement_id === placementId),
    [campaigns, siteId, placementId],
  );

  useEffect(() => {
    if (sitePlacements.length > 0 && !sitePlacements.some((placement) => placement.id === placementId)) {
      setPlacementId(sitePlacements[0].id);
    }
  }, [placementId, sitePlacements]);

  useEffect(() => {
    setVariantSelection((current) => {
      const next: typeof current = {};
      for (const campaign of placementCampaigns) {
        next[campaign.id] = current[campaign.id] || { selected: false, weight: 100, control: false };
      }
      return next;
    });
  }, [placementCampaigns]);

  const openExperiment = () => {
    setExperimentName('');
    setObjective('ctr');
    setExperimentStatus('draft');
    setMinImpressions(100);
    setMinLift(10);
    setAutoWinner(true);
    setSiteId(sites[0]?.id || '');
    setPlacementId('');
    setVariantSelection({});
    setExperimentModal(true);
  };

  const createExperiment = async (event: React.FormEvent) => {
    event.preventDefault();
    const variants = Object.entries(variantSelection)
      .filter(([, value]) => value.selected)
      .map(([campaignId, value]) => ({
        campaign_id: campaignId,
        weight: value.weight,
        is_control: value.control,
      }));
    if (!siteId || !placementId || experimentName.trim().length < 2 || variants.length < 2) {
      onNotify('error', 'Choose a placement, name the test, and select at least two campaigns.');
      return;
    }
    setBusyId('create-experiment');
    try {
      await api.createExperiment({
        site_id: siteId,
        placement_id: placementId,
        name: experimentName.trim(),
        status: experimentStatus,
        objective,
        min_impressions: Number(minImpressions) || 100,
        min_lift_percent: Number(minLift) || 0,
        auto_winner: autoWinner,
        variants,
      });
      setExperimentModal(false);
      onNotify('success', 'Experiment created.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create experiment');
    } finally {
      setBusyId('');
    }
  };

  const changeExperimentStatus = async (experiment: Experiment, status: 'running' | 'paused' | 'completed') => {
    setBusyId(experiment.id);
    try {
      await api.setExperimentStatus(experiment.id, status);
      onNotify('success', `Experiment ${status}.`);
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update experiment');
    } finally {
      setBusyId('');
    }
  };

  const optimize = async (experiment: Experiment) => {
    setBusyId(experiment.id);
    try {
      const result = await api.optimizeExperiment(experiment.id);
      onNotify('success', `Optimizer evaluated ${result.updated_experiments} experiment(s).`);
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Optimizer failed');
    } finally {
      setBusyId('');
    }
  };

  const deleteExperiment = async (experiment: Experiment) => {
    if (!window.confirm(`Delete experiment "${experiment.name}"?`)) return;
    setBusyId(experiment.id);
    try {
      await api.deleteExperiment(experiment.id);
      onNotify('success', 'Experiment deleted.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to delete experiment');
    } finally {
      setBusyId('');
    }
  };

  const createSegment = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!segmentSite || segmentName.trim().length < 2) return;
    const rules: AudienceSegment['rules'] = {};
    if (segmentUtmSource.trim()) rules.utm_source = segmentUtmSource.trim();
    if (segmentUtmCampaign.trim()) rules.utm_campaign = segmentUtmCampaign.trim();
    if (segmentDevice) rules.device = segmentDevice;
    if (segmentReferrer.trim()) rules.referrer_contains = segmentReferrer.trim();
    setBusyId('create-segment');
    try {
      await api.createAudienceSegment({
        site_id: segmentSite,
        name: segmentName.trim(),
        description: segmentDescription.trim(),
        rules,
        status: 'active',
      });
      setSegmentModal(false);
      setSegmentName('');
      setSegmentDescription('');
      setSegmentUtmSource('');
      setSegmentUtmCampaign('');
      setSegmentDevice('');
      setSegmentReferrer('');
      onNotify('success', 'Audience segment created.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create segment');
    } finally {
      setBusyId('');
    }
  };

  const deleteSegment = async (segment: AudienceSegment) => {
    if (!window.confirm(`Delete audience segment "${segment.name}"? Campaigns using it will target everyone.`)) return;
    setBusyId(segment.id);
    try {
      await api.deleteAudienceSegment(segment.id);
      onNotify('success', 'Audience segment deleted.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to delete segment');
    } finally {
      setBusyId('');
    }
  };

  const siteName = (id: string) => sites.find((site) => site.id === id)?.name || id;
  const placementName = (id: string) => placements.find((placement) => placement.id === id)?.name || id;

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900">
            <FlaskConical className="h-6 w-6 text-slate-500" /> Optimization & experiments
          </h2>
          <p className="mt-1 max-w-3xl text-sm text-slate-500">
            Run true weighted rotation, compare campaign variants by CTR, conversion rate, or revenue,
            auto-allocate delivery weight, and declare a winner when a variant clears your sample and lift thresholds.
          </p>
        </div>
        <div className="flex items-center gap-2 self-start">
          <button
            type="button"
            onClick={load}
            disabled={loading}
            className="inline-flex items-center gap-1.5 rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50 disabled:opacity-60"
          >
            <RefreshCw className={`h-3.5 w-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
          </button>
          <button
            type="button"
            onClick={tab === 'experiments' ? openExperiment : () => setSegmentModal(true)}
            className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700"
          >
            <Plus className="h-3.5 w-3.5" /> {tab === 'experiments' ? 'New experiment' : 'New segment'}
          </button>
        </div>
      </div>

      <div className="flex gap-2">
        <button
          type="button"
          onClick={() => setTab('experiments')}
          className={`inline-flex items-center gap-1.5 rounded-lg px-3 py-2 text-xs font-semibold ${tab === 'experiments' ? 'bg-slate-900 text-white' : 'bg-white text-slate-600 border border-slate-200'}`}
        >
          <FlaskConical className="h-3.5 w-3.5" /> Experiments
        </button>
        <button
          type="button"
          onClick={() => setTab('segments')}
          className={`inline-flex items-center gap-1.5 rounded-lg px-3 py-2 text-xs font-semibold ${tab === 'segments' ? 'bg-slate-900 text-white' : 'bg-white text-slate-600 border border-slate-200'}`}
        >
          <Users className="h-3.5 w-3.5" /> Audience segments
        </button>
      </div>

      {tab === 'experiments' && (
        <div className="space-y-4">
          {experiments.map((experiment) => {
            const metrics = experiment.metrics?.variants || experiment.variants;
            const winner = experiment.variants.find((variant) => variant.campaign_id === experiment.winner_campaign_id);
            return (
              <div key={experiment.id} className="rounded-xl border border-slate-200 bg-white p-5 shadow-sm">
                <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                  <div className="min-w-0">
                    <div className="flex flex-wrap items-center gap-2">
                      <h3 className="text-base font-bold text-slate-900">{experiment.name}</h3>
                      <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold uppercase ${STATUS_STYLES[experiment.status]}`}>
                        {experiment.status}
                      </span>
                      <span className="rounded-full border border-slate-200 bg-slate-50 px-2 py-0.5 text-[10px] font-semibold text-slate-600">
                        {OBJECTIVE_LABELS[experiment.objective]}
                      </span>
                      {winner && (
                        <span className="inline-flex items-center gap-1 rounded-full border border-amber-200 bg-amber-50 px-2 py-0.5 text-[10px] font-semibold text-amber-800">
                          <Trophy className="h-3 w-3" /> {winner.name}
                        </span>
                      )}
                    </div>
                    <p className="mt-1 text-xs text-slate-500">
                      {siteName(experiment.site_id)} · {placementName(experiment.placement_id)} · min {formatNumber(experiment.min_impressions)} impressions / variant · lift ≥ {experiment.min_lift_percent}%
                    </p>
                    <p className="text-[11px] text-slate-400">
                      Started {formatDate(experiment.started_at)} · total {formatNumber(experiment.metrics?.total_impressions || 0)} impressions
                    </p>
                  </div>
                  <div className="flex shrink-0 items-center gap-1">
                    {experiment.status === 'running' ? (
                      <button type="button" onClick={() => changeExperimentStatus(experiment, 'paused')} disabled={busyId === experiment.id} className="rounded-lg border border-slate-200 p-2 text-slate-500 hover:bg-slate-50 disabled:opacity-50" title="Pause">
                        <Pause className="h-4 w-4" />
                      </button>
                    ) : (
                      <button type="button" onClick={() => changeExperimentStatus(experiment, 'running')} disabled={busyId === experiment.id} className="rounded-lg border border-slate-200 p-2 text-slate-500 hover:bg-slate-50 disabled:opacity-50" title="Start">
                        <Play className="h-4 w-4" />
                      </button>
                    )}
                    <button type="button" onClick={() => optimize(experiment)} disabled={busyId === experiment.id} className="rounded-lg border border-slate-200 px-2 py-1.5 text-[11px] font-semibold text-slate-600 hover:bg-slate-50 disabled:opacity-50" title="Run optimizer now">
                      Optimize
                    </button>
                    <button type="button" onClick={() => deleteExperiment(experiment)} disabled={busyId === experiment.id} className="rounded-lg border border-slate-200 p-2 text-slate-400 hover:bg-rose-50 hover:text-rose-600 disabled:opacity-50" title="Delete">
                      <Trash2 className="h-4 w-4" />
                    </button>
                  </div>
                </div>

                <div className="mt-4 overflow-x-auto">
                  <table className="w-full text-left text-xs">
                    <thead className="text-[10px] uppercase tracking-wider text-slate-500">
                      <tr>
                        <th className="pb-2 font-semibold">Variant</th>
                        <th className="pb-2 font-semibold">Weight</th>
                        <th className="pb-2 text-right font-semibold">Impressions</th>
                        <th className="pb-2 text-right font-semibold">Clicks</th>
                        <th className="pb-2 text-right font-semibold">CTR</th>
                        <th className="pb-2 text-right font-semibold">Conversions</th>
                        <th className="pb-2 text-right font-semibold">CVR</th>
                        <th className="pb-2 text-right font-semibold">Revenue / 1k</th>
                        <th className="pb-2 text-right font-semibold">Score</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-100">
                      {metrics.map((variant) => (
                        <tr key={variant.id} className={variant.campaign_id === experiment.winner_campaign_id ? 'bg-amber-50/60' : ''}>
                          <td className="py-2">
                            <div className="font-medium text-slate-800">{variant.name || variant.campaign_name}</div>
                            <div className="text-[10px] text-slate-400">
                              {variant.is_control ? 'Control · ' : ''}{variant.campaign_id}
                            </div>
                          </td>
                          <td className="py-2 tabular-nums text-slate-600">{variant.weight}</td>
                          <td className="py-2 text-right tabular-nums text-slate-600">{formatNumber(variant.impressions)}</td>
                          <td className="py-2 text-right tabular-nums text-slate-600">{formatNumber(variant.clicks)}</td>
                          <td className="py-2 text-right tabular-nums text-slate-700">{formatPercent(variant.ctr)}</td>
                          <td className="py-2 text-right tabular-nums text-slate-600">{formatNumber(variant.conversions)}</td>
                          <td className="py-2 text-right tabular-nums text-slate-700">{formatPercent(variant.conversion_rate)}</td>
                          <td className="py-2 text-right tabular-nums text-slate-600">
                            {((variant.revenue_per_mille_cents || 0) / 100).toFixed(2)}
                          </td>
                          <td className="py-2 text-right tabular-nums font-semibold text-slate-800">
                            {(variant.score || 0).toFixed(2)}
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            );
          })}
          {!loading && experiments.length === 0 && (
            <div className="rounded-xl border border-dashed border-slate-300 bg-white p-10 text-center">
              <FlaskConical className="mx-auto h-8 w-8 text-slate-300" />
              <h3 className="mt-3 text-sm font-semibold text-slate-800">No experiments yet</h3>
              <p className="mx-auto mt-1 max-w-md text-sm text-slate-500">
                Create an A/B test on a placement. The delivery API routes each visitor to a variant
                by weight, and the optimizer adjusts allocation or picks a winner.
              </p>
              <button type="button" onClick={openExperiment} className="mt-4 inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700">
                <Plus className="h-3.5 w-3.5" /> Create experiment
              </button>
            </div>
          )}
        </div>
      )}

      {tab === 'segments' && (
        <div className="rounded-xl border border-slate-200 bg-white shadow-sm">
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-4 py-3 font-semibold">Segment</th>
                  <th className="px-4 py-3 font-semibold">Site</th>
                  <th className="px-4 py-3 font-semibold">Rules</th>
                  <th className="px-4 py-3 font-semibold">Status</th>
                  <th className="px-4 py-3 font-semibold">Created</th>
                  <th className="px-4 py-3 text-right font-semibold">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {segments.map((segment) => (
                  <tr key={segment.id} className="hover:bg-slate-50/70">
                    <td className="px-4 py-3">
                      <div className="font-medium text-slate-900">{segment.name}</div>
                      <div className="text-[11px] text-slate-500">{segment.description || 'No description'}</div>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-600">{siteName(segment.site_id)}</td>
                    <td className="px-4 py-3">
                      <div className="flex flex-wrap gap-1">
                        {Object.entries(segment.rules || {}).map(([key, value]) => (
                          <span key={key} className="rounded-full border border-slate-200 bg-slate-50 px-2 py-0.5 text-[10px] font-medium text-slate-600">
                            {key}: {Array.isArray(value) ? value.join(' | ') : String(value)}
                          </span>
                        ))}
                        {Object.keys(segment.rules || {}).length === 0 && (
                          <span className="text-[11px] text-slate-400">Everyone</span>
                        )}
                      </div>
                    </td>
                    <td className="px-4 py-3">
                      <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold ${segment.status === 'active' ? 'border-emerald-200 bg-emerald-50 text-emerald-700' : 'border-slate-200 bg-slate-100 text-slate-500'}`}>
                        {segment.status}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-500">{formatDate(segment.created_at)}</td>
                    <td className="px-4 py-3 text-right">
                      <button type="button" onClick={() => deleteSegment(segment)} disabled={busyId === segment.id} className="rounded-lg border border-slate-200 p-1.5 text-slate-400 hover:bg-rose-50 hover:text-rose-600 disabled:opacity-50" aria-label="Delete segment">
                        <Trash2 className="h-3.5 w-3.5" />
                      </button>
                    </td>
                  </tr>
                ))}
                {segments.length === 0 && (
                  <tr><td colSpan={6} className="px-4 py-10 text-center text-sm text-slate-500">No audience segments yet.</td></tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      )}

      <Modal isOpen={experimentModal} onClose={() => setExperimentModal(false)} title="New experiment" maxWidth="max-w-3xl">
        <form onSubmit={createExperiment} className="space-y-4">
          <div className="grid gap-3 sm:grid-cols-2">
            <div className="sm:col-span-2">
              <label className="mb-1 block text-xs font-semibold text-slate-700">Experiment name</label>
              <input value={experimentName} onChange={(event) => setExperimentName(event.target.value)} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Site</label>
              <select value={siteId} onChange={(event) => { setSiteId(event.target.value); setPlacementId(''); setVariantSelection({}); }} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                {sites.map((site) => <option key={site.id} value={site.id}>{site.name}</option>)}
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Placement</label>
              <select value={placementId} onChange={(event) => { setPlacementId(event.target.value); setVariantSelection({}); }} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value="">Select placement</option>
                {sitePlacements.map((placement) => <option key={placement.id} value={placement.id}>{placement.name}</option>)}
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Objective</label>
              <select value={objective} onChange={(event) => setObjective(event.target.value as Experiment['objective'])} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value="ctr">CTR</option>
                <option value="conversion_rate">Conversion rate</option>
                <option value="revenue">Revenue / 1,000 impressions</option>
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Initial status</label>
              <select value={experimentStatus} onChange={(event) => setExperimentStatus(event.target.value as 'draft' | 'running')} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value="draft">Draft</option>
                <option value="running">Running</option>
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Min impressions per variant</label>
              <input type="number" min={1} value={minImpressions} onChange={(event) => setMinImpressions(Number(event.target.value))} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Minimum lift (%)</label>
              <input type="number" min={0} value={minLift} onChange={(event) => setMinLift(Number(event.target.value))} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <label className="flex items-center gap-2 text-xs text-slate-600 sm:col-span-2">
              <input type="checkbox" checked={autoWinner} onChange={(event) => setAutoWinner(event.target.checked)} />
              Automatically allocate weight and declare a winner when the threshold is met
            </label>
          </div>

          <div>
            <div className="text-[11px] font-bold uppercase tracking-wider text-slate-500">Variants</div>
            <p className="mt-1 text-[11px] text-slate-400">Select at least two campaigns from this placement.</p>
            <div className="mt-2 space-y-2">
              {placementCampaigns.map((campaign) => {
                const selected = variantSelection[campaign.id];
                return (
                  <div key={campaign.id} className={`rounded-lg border p-3 ${selected?.selected ? 'border-blue-200 bg-blue-50/60' : 'border-slate-200'}`}>
                    <div className="flex flex-wrap items-center justify-between gap-2">
                      <label className="flex items-center gap-2 text-sm text-slate-700">
                        <input
                          type="checkbox"
                          checked={selected?.selected || false}
                          onChange={(event) => setVariantSelection((current) => ({
                            ...current,
                            [campaign.id]: { ...(current[campaign.id] || { weight: 100, control: false }), selected: event.target.checked },
                          }))}
                        />
                        <span className="font-medium">{campaign.name}</span>
                        <span className="text-[10px] text-slate-400">{campaign.status}</span>
                      </label>
                      {selected?.selected && (
                        <div className="flex items-center gap-3">
                          <label className="flex items-center gap-1 text-[11px] text-slate-500">
                            Weight
                            <input
                              type="number"
                              min={1}
                              value={selected.weight}
                              onChange={(event) => setVariantSelection((current) => ({
                                ...current,
                                [campaign.id]: { ...current[campaign.id], weight: Number(event.target.value) },
                              }))}
                              className="w-20 rounded border border-slate-200 px-2 py-1 text-xs focus:border-blue-500 focus:outline-none"
                            />
                          </label>
                          <label className="flex items-center gap-1 text-[11px] text-slate-500">
                            <input
                              type="checkbox"
                              checked={selected.control}
                              onChange={(event) => setVariantSelection((current) => ({
                                ...current,
                                [campaign.id]: { ...current[campaign.id], control: event.target.checked },
                              }))}
                            />
                            Control
                          </label>
                        </div>
                      )}
                    </div>
                  </div>
                );
              })}
              {placementCampaigns.length === 0 && (
                <p className="rounded-lg bg-slate-50 p-3 text-xs text-slate-500">No campaigns on this placement yet.</p>
              )}
            </div>
          </div>

          <div className="flex justify-end gap-2">
            <button type="button" onClick={() => setExperimentModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button>
            <button type="submit" disabled={busyId === 'create-experiment'} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60">
              {busyId === 'create-experiment' ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <FlaskConical className="h-3.5 w-3.5" />}
              Create experiment
            </button>
          </div>
        </form>
      </Modal>

      <Modal isOpen={segmentModal} onClose={() => setSegmentModal(false)} title="New audience segment">
        <form onSubmit={createSegment} className="space-y-3">
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Site</label>
            <select value={segmentSite} onChange={(event) => setSegmentSite(event.target.value)} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
              {sites.map((site) => <option key={site.id} value={site.id}>{site.name}</option>)}
            </select>
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Name</label>
            <input value={segmentName} onChange={(event) => setSegmentName(event.target.value)} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Description</label>
            <input value={segmentDescription} onChange={(event) => setSegmentDescription(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">UTM source</label>
              <input value={segmentUtmSource} onChange={(event) => setSegmentUtmSource(event.target.value)} placeholder="newsletter" className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">UTM campaign</label>
              <input value={segmentUtmCampaign} onChange={(event) => setSegmentUtmCampaign(event.target.value)} placeholder="autumn" className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Device</label>
              <select value={segmentDevice} onChange={(event) => setSegmentDevice(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value="">Any device</option>
                <option value="desktop">Desktop</option>
                <option value="mobile">Mobile</option>
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Referrer contains</label>
              <input value={segmentReferrer} onChange={(event) => setSegmentReferrer(event.target.value)} placeholder="google.com" className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
          </div>
          <p className="rounded-lg bg-slate-50 p-3 text-[11px] leading-5 text-slate-500">
            A campaign assigned to this segment only delivers when all rules match. UTM rules compare
            the landing-page query parameters; device is inferred from the user agent.
          </p>
          <div className="flex justify-end gap-2">
            <button type="button" onClick={() => setSegmentModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button>
            <button type="submit" disabled={busyId === 'create-segment'} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60">
              {busyId === 'create-segment' ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Plus className="h-3.5 w-3.5" />}
              Create segment
            </button>
          </div>
        </form>
      </Modal>
    </div>
  );
};
