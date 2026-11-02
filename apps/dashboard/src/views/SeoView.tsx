import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Bot,
  BarChart3,
  Check,
  ExternalLink,
  FileText,
  Globe2,
  Link2,
  Loader2,
  Minus,
  Plus,
  RefreshCw,
  Search,
  ShieldCheck,
  Target,
  Trash2,
  TrendingDown,
  TrendingUp,
} from 'lucide-react';
import { api } from '../api';
import {
  GeoQuery,
  SeoAudit,
  SeoBacklink,
  SeoCompetitor,
  SeoKeyword,
  SeoProject,
  SeoSummary,
  Site,
} from '../types';
import { Modal } from '../components/Modal';

interface SeoViewProps {
  role: string;
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

type SeoTab = 'keywords' | 'competitors' | 'backlinks' | 'audits' | 'geo' | 'content';

const EMPTY_SUMMARY: SeoSummary = {
  keywords: 0,
  tracked_keywords: 0,
  top10_keywords: 0,
  improved_keywords: 0,
  declined_keywords: 0,
  average_position: 0,
  backlinks: 0,
  active_backlinks: 0,
  lost_backlinks: 0,
  competitors: 0,
  latest_audit_score: 0,
  latest_audit_at: '',
  geo_checks: 0,
  geo_mentions: 0,
  geo_visibility_percent: 0,
};

const TABS: Array<{ id: SeoTab; label: string; icon: React.ReactNode }> = [
  { id: 'keywords', label: 'Keywords', icon: <Target className="h-3.5 w-3.5" /> },
  { id: 'competitors', label: 'Competitors', icon: <BarChart3 className="h-3.5 w-3.5" /> },
  { id: 'backlinks', label: 'Backlinks', icon: <Link2 className="h-3.5 w-3.5" /> },
  { id: 'audits', label: 'Technical audits', icon: <ShieldCheck className="h-3.5 w-3.5" /> },
  { id: 'geo', label: 'GEO visibility', icon: <Bot className="h-3.5 w-3.5" /> },
  { id: 'content', label: 'Content optimizer', icon: <FileText className="h-3.5 w-3.5" /> },
];

const ENGINE_LABELS: Record<string, string> = {
  chatgpt: 'ChatGPT',
  perplexity: 'Perplexity',
  google_ai_overview: 'Google AI Overview',
  gemini: 'Gemini',
  claude: 'Claude',
  copilot: 'Copilot',
  grok: 'Grok',
};

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleDateString();
}

function formatNumber(value: number): string {
  return new Intl.NumberFormat('en-US').format(value || 0);
}

function positionDelta(keyword: SeoKeyword): React.ReactNode {
  if (!keyword.previous_position || !keyword.current_position) return <Minus className="h-3.5 w-3.5 text-slate-300" />;
  if (keyword.current_position < keyword.previous_position) {
    return <TrendingUp className="h-3.5 w-3.5 text-emerald-600" />;
  }
  if (keyword.current_position > keyword.previous_position) {
    return <TrendingDown className="h-3.5 w-3.5 text-rose-600" />;
  }
  return <Minus className="h-3.5 w-3.5 text-slate-300" />;
}

function scoreTone(score: number): string {
  if (score >= 85) return 'text-emerald-700';
  if (score >= 65) return 'text-amber-700';
  return 'text-rose-700';
}

export const SeoView: React.FC<SeoViewProps> = ({ onNotify }) => {
  const [sites, setSites] = useState<Site[]>([]);
  const [projects, setProjects] = useState<SeoProject[]>([]);
  const [selectedId, setSelectedId] = useState('');
  const [summary, setSummary] = useState<SeoSummary>(EMPTY_SUMMARY);
  const [keywords, setKeywords] = useState<SeoKeyword[]>([]);
  const [competitors, setCompetitors] = useState<SeoCompetitor[]>([]);
  const [backlinks, setBacklinks] = useState<SeoBacklink[]>([]);
  const [audits, setAudits] = useState<SeoAudit[]>([]);
  const [geoQueries, setGeoQueries] = useState<GeoQuery[]>([]);
  const [tab, setTab] = useState<SeoTab>('keywords');
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);

  const [projectModal, setProjectModal] = useState(false);
  const [projectSite, setProjectSite] = useState('');
  const [projectName, setProjectName] = useState('');
  const [projectDomain, setProjectDomain] = useState('');
  const [projectCountry, setProjectCountry] = useState('US');

  const [keywordModal, setKeywordModal] = useState(false);
  const [keywordText, setKeywordText] = useState('');
  const [keywordUrl, setKeywordUrl] = useState('');
  const [keywordIntent, setKeywordIntent] = useState('informational');
  const [keywordPriority, setKeywordPriority] = useState(2);
  const [researchModal, setResearchModal] = useState(false);
  const [researchSeed, setResearchSeed] = useState('');
  const [researchResults, setResearchResults] = useState<Array<{ keyword: string; intent: string; search_volume: number; difficulty: number }>>([]);
  const [rankKeyword, setRankKeyword] = useState<SeoKeyword | null>(null);
  const [rankPosition, setRankPosition] = useState('0');
  const [rankUrl, setRankUrl] = useState('');
  const [rankNote, setRankNote] = useState('');

  const [competitorModal, setCompetitorModal] = useState(false);
  const [competitorDomain, setCompetitorDomain] = useState('');
  const [competitorName, setCompetitorName] = useState('');
  const [competitorKeywordFor, setCompetitorKeywordFor] = useState<SeoCompetitor | null>(null);
  const [competitorKeyword, setCompetitorKeyword] = useState('');
  const [competitorKeywordPosition, setCompetitorKeywordPosition] = useState('0');

  const [backlinkModal, setBacklinkModal] = useState(false);
  const [backlinkSource, setBacklinkSource] = useState('');
  const [backlinkTarget, setBacklinkTarget] = useState('');
  const [backlinkAnchor, setBacklinkAnchor] = useState('');
  const [backlinkNofollow, setBacklinkNofollow] = useState(false);

  const [auditTarget, setAuditTarget] = useState('');
  const [auditDetail, setAuditDetail] = useState<SeoAudit | null>(null);
  const [runningAudit, setRunningAudit] = useState(false);

  const [geoModal, setGeoModal] = useState(false);
  const [geoQueryText, setGeoQueryText] = useState('');
  const [geoEngines, setGeoEngines] = useState<string[]>(['chatgpt', 'perplexity', 'google_ai_overview']);
  const [mentionFor, setMentionFor] = useState<GeoQuery | null>(null);
  const [mentionEngine, setMentionEngine] = useState('chatgpt');
  const [mentionPresent, setMentionPresent] = useState(true);
  const [mentionPosition, setMentionPosition] = useState('1');
  const [mentionSnippet, setMentionSnippet] = useState('');

  const [optimizeKeyword, setOptimizeKeyword] = useState('');
  const [optimizeUrl, setOptimizeUrl] = useState('');
  const [optimizeResult, setOptimizeResult] = useState<Record<string, any> | null>(null);

  const selected = useMemo(
    () => projects.find((project) => project.id === selectedId) || null,
    [projects, selectedId],
  );

  const loadProjects = useCallback(async () => {
    const rows = await api.getSeoProjects();
    setProjects(rows);
    setSelectedId((current) => (current && rows.some((row) => row.id === current) ? current : rows[0]?.id || ''));
  }, []);

  const loadProject = useCallback(async (projectId: string) => {
    if (!projectId) {
      setSummary(EMPTY_SUMMARY);
      setKeywords([]);
      setCompetitors([]);
      setBacklinks([]);
      setAudits([]);
      setGeoQueries([]);
      return;
    }
    setLoading(true);
    try {
      const [summaryData, keywordRows, competitorRows, backlinkRows, auditRows, geoRows] = await Promise.all([
        api.getSeoSummary(projectId),
        api.getSeoKeywords(projectId),
        api.getSeoCompetitors(projectId),
        api.getSeoBacklinks(projectId),
        api.getSeoAudits(projectId),
        api.getGeoQueries(projectId),
      ]);
      setSummary(summaryData.summary);
      setKeywords(keywordRows);
      setCompetitors(competitorRows);
      setBacklinks(backlinkRows);
      setAudits(auditRows);
      setGeoQueries(geoRows);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load SEO project');
    } finally {
      setLoading(false);
    }
  }, [onNotify]);

  useEffect(() => {
    Promise.all([api.getSites(), loadProjects()])
      .then(([siteRows]) => {
        setSites(siteRows);
        setProjectSite((current) => current || siteRows[0]?.id || '');
      })
      .catch((err: any) => onNotify('error', err?.message || 'Failed to load SEO projects'));
  }, [loadProjects, onNotify]);

  useEffect(() => {
    loadProject(selectedId);
  }, [loadProject, selectedId]);

  const reload = async () => {
    await loadProjects();
    await loadProject(selectedId);
  };

  const createProject = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!projectSite || !projectName.trim()) return;
    setBusy(true);
    try {
      const project = await api.createSeoProject({
        site_id: projectSite,
        name: projectName.trim(),
        domain: projectDomain.trim() || undefined,
        country: projectCountry,
      });
      setProjects((current) => [project, ...current]);
      setSelectedId(project.id);
      setProjectModal(false);
      setProjectName('');
      setProjectDomain('');
      onNotify('success', 'SEO project created.');
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create SEO project');
    } finally {
      setBusy(false);
    }
  };

  const deleteProject = async (project: SeoProject) => {
    if (!window.confirm(`Delete SEO project ${project.name}? All keywords, audits, and GEO data will be removed.`)) return;
    setBusy(true);
    try {
      await api.deleteSeoProject(project.id);
      onNotify('success', 'SEO project deleted.');
      await loadProjects();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to delete project');
    } finally {
      setBusy(false);
    }
  };

  const addKeyword = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selected || !keywordText.trim()) return;
    setBusy(true);
    try {
      const keyword = await api.createSeoKeyword(selected.id, {
        keyword: keywordText.trim(),
        target_url: keywordUrl.trim() || undefined,
        intent: keywordIntent,
        priority: keywordPriority,
      });
      setKeywords((current) => [...current, keyword]);
      setKeywordModal(false);
      setKeywordText('');
      setKeywordUrl('');
      onNotify('success', 'Keyword added.');
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add keyword');
    } finally {
      setBusy(false);
    }
  };

  const runResearch = async () => {
    if (!selected || researchSeed.trim().length < 2) return;
    setBusy(true);
    try {
      setResearchResults(await api.researchSeoKeywords(selected.id, researchSeed.trim(), 30));
    } catch (err: any) {
      onNotify('error', err?.message || 'Keyword research failed');
    } finally {
      setBusy(false);
    }
  };

  const addResearchResults = async () => {
    if (!selected || researchResults.length === 0) return;
    setBusy(true);
    try {
      await api.createSeoKeywordsBulk(selected.id, researchResults);
      setResearchModal(false);
      setResearchResults([]);
      setResearchSeed('');
      onNotify('success', 'Research keywords added.');
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add research keywords');
    } finally {
      setBusy(false);
    }
  };

  const openRank = (keyword: SeoKeyword) => {
    setRankKeyword(keyword);
    setRankPosition(String(keyword.current_position || 0));
    setRankUrl(keyword.target_url);
    setRankNote('');
  };

  const saveRank = async () => {
    if (!rankKeyword) return;
    setBusy(true);
    try {
      await api.recordSeoRanking(rankKeyword.id, {
        position: Number(rankPosition),
        url: rankUrl.trim() || undefined,
        note: rankNote.trim() || undefined,
      });
      setRankKeyword(null);
      onNotify('success', 'SERP position recorded.');
      if (selected) await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to record rank');
    } finally {
      setBusy(false);
    }
  };

  const addCompetitor = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selected || !competitorDomain.trim()) return;
    setBusy(true);
    try {
      await api.createSeoCompetitor(selected.id, {
        domain: competitorDomain.trim(),
        name: competitorName.trim(),
      });
      setCompetitorModal(false);
      setCompetitorDomain('');
      setCompetitorName('');
      onNotify('success', 'Competitor added.');
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add competitor');
    } finally {
      setBusy(false);
    }
  };

  const addCompetitorKeyword = async () => {
    if (!competitorKeywordFor || !competitorKeyword.trim()) return;
    setBusy(true);
    try {
      await api.addSeoCompetitorKeyword(competitorKeywordFor.id, {
        keyword: competitorKeyword.trim(),
        position: Number(competitorKeywordPosition || 0),
      });
      setCompetitorKeywordFor(null);
      setCompetitorKeyword('');
      if (selected) await loadProject(selected.id);
      onNotify('success', 'Competitor keyword added.');
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add competitor keyword');
    } finally {
      setBusy(false);
    }
  };

  const addBacklink = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selected || !backlinkSource.trim()) return;
    setBusy(true);
    try {
      await api.createSeoBacklink(selected.id, {
        source_url: backlinkSource.trim(),
        target_url: backlinkTarget.trim() || undefined,
        anchor_text: backlinkAnchor.trim() || undefined,
        nofollow: backlinkNofollow,
        status: 'active',
      });
      setBacklinkModal(false);
      setBacklinkSource('');
      setBacklinkTarget('');
      setBacklinkAnchor('');
      setBacklinkNofollow(false);
      onNotify('success', 'Backlink recorded.');
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add backlink');
    } finally {
      setBusy(false);
    }
  };

  const toggleBacklink = async (backlink: SeoBacklink) => {
    setBusy(true);
    try {
      await api.updateSeoBacklink(backlink.id, {
        status: backlink.status === 'active' ? 'lost' : 'active',
      });
      if (selected) await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update backlink');
    } finally {
      setBusy(false);
    }
  };

  const runAudit = async () => {
    if (!selected) return;
    setRunningAudit(true);
    try {
      const audit = await api.runSeoAudit(selected.id, auditTarget.trim() || undefined);
      setAuditDetail(audit);
      onNotify('success', `Technical audit finished with a score of ${audit.score}.`);
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Technical audit failed');
    } finally {
      setRunningAudit(false);
    }
  };

  const openAudit = async (audit: SeoAudit) => {
    try {
      setAuditDetail(await api.getSeoAudit(audit.id));
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load audit');
    }
  };

  const addGeoQuery = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selected || !geoQueryText.trim() || geoEngines.length === 0) return;
    setBusy(true);
    try {
      await api.createGeoQuery(selected.id, {
        query_text: geoQueryText.trim(),
        engines: geoEngines,
      });
      setGeoModal(false);
      setGeoQueryText('');
      onNotify('success', 'GEO query added.');
      await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to add GEO query');
    } finally {
      setBusy(false);
    }
  };

  const recordMention = async () => {
    if (!mentionFor) return;
    setBusy(true);
    try {
      await api.addGeoMention(mentionFor.id, {
        engine: mentionEngine,
        mentioned: mentionPresent,
        position: Number(mentionPosition || 0),
        snippet: mentionSnippet.trim(),
      });
      setMentionFor(null);
      setMentionSnippet('');
      onNotify('success', 'AI visibility check recorded.');
      if (selected) await loadProject(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to record AI visibility');
    } finally {
      setBusy(false);
    }
  };

  const runOptimize = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selected || !optimizeKeyword.trim()) return;
    setBusy(true);
    try {
      setOptimizeResult(await api.optimizeSeoContent(selected.id, {
        keyword: optimizeKeyword.trim(),
        target_url: optimizeUrl.trim() || undefined,
      }));
    } catch (err: any) {
      onNotify('error', err?.message || 'Content analysis failed');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900">
            <Search className="h-6 w-6 text-slate-500" /> SEO & GEO intelligence
          </h2>
          <p className="mt-1 max-w-3xl text-sm text-slate-500">
            Track keyword positions, analyze competitor keywords, monitor backlinks, run technical
            audits and content optimization, and record AI-search visibility across generative engines.
          </p>
        </div>
        <div className="flex items-center gap-2 self-start">
          <button
            type="button"
            onClick={reload}
            disabled={loading}
            className="inline-flex items-center gap-1.5 rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50 disabled:opacity-60"
          >
            <RefreshCw className={`h-3.5 w-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
          </button>
          <button
            type="button"
            onClick={() => setProjectModal(true)}
            className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700"
          >
            <Plus className="h-3.5 w-3.5" /> New SEO project
          </button>
        </div>
      </div>

      <div className="grid gap-5 xl:grid-cols-[17rem_minmax(0,1fr)]">
        <div className="rounded-xl border border-slate-200 bg-white p-3 h-fit">
          <div className="px-2 py-1 text-[11px] font-bold uppercase tracking-wider text-slate-500">Projects</div>
          <div className="mt-2 space-y-1">
            {projects.map((project) => (
              <div
                key={project.id}
                className={`group flex items-center justify-between gap-2 rounded-lg px-2 py-2 ${
                  selectedId === project.id ? 'bg-blue-50 text-blue-900' : 'text-slate-700 hover:bg-slate-50'
                }`}
              >
                <button type="button" onClick={() => setSelectedId(project.id)} className="min-w-0 flex-1 text-left">
                  <div className="truncate text-sm font-semibold">{project.name}</div>
                  <div className="truncate text-[11px] text-slate-500">{project.domain}</div>
                </button>
                <button
                  type="button"
                  onClick={() => deleteProject(project)}
                  className="rounded p-1 text-slate-300 opacity-0 hover:bg-rose-50 hover:text-rose-600 group-hover:opacity-100"
                  aria-label="Delete project"
                >
                  <Trash2 className="h-3.5 w-3.5" />
                </button>
              </div>
            ))}
            {projects.length === 0 && (
              <p className="px-2 py-3 text-xs text-slate-500">No SEO projects yet.</p>
            )}
          </div>
        </div>

        <div className="min-w-0 flex flex-col gap-4">
          {selected ? (
            <>
              <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
                <div className="rounded-xl border border-slate-200 bg-white p-4">
                  <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Tracked keywords</div>
                  <div className="mt-1 text-2xl font-bold tabular-nums text-slate-900">{summary.tracked_keywords}</div>
                  <div className="text-xs text-slate-500">{summary.top10_keywords} in top 10</div>
                </div>
                <div className="rounded-xl border border-slate-200 bg-white p-4">
                  <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Average position</div>
                  <div className="mt-1 text-2xl font-bold tabular-nums text-blue-700">
                    {summary.average_position > 0 ? summary.average_position.toFixed(1) : '—'}
                  </div>
                  <div className="text-xs text-slate-500">{summary.improved_keywords} improved · {summary.declined_keywords} declined</div>
                </div>
                <div className="rounded-xl border border-slate-200 bg-white p-4">
                  <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Technical score</div>
                  <div className={`mt-1 text-2xl font-bold tabular-nums ${scoreTone(summary.latest_audit_score)}`}>
                    {summary.latest_audit_score || '—'}
                  </div>
                  <div className="text-xs text-slate-500">{summary.latest_audit_at ? formatDate(summary.latest_audit_at) : 'No audit yet'}</div>
                </div>
                <div className="rounded-xl border border-slate-200 bg-white p-4">
                  <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">AI visibility</div>
                  <div className="mt-1 text-2xl font-bold tabular-nums text-violet-700">
                    {summary.geo_visibility_percent.toFixed(0)}%
                  </div>
                  <div className="text-xs text-slate-500">{summary.geo_mentions} of {summary.geo_checks} checks</div>
                </div>
              </div>

              <div className="flex flex-wrap gap-2 border-b border-slate-200 pb-2">
                {TABS.map((item) => (
                  <button
                    key={item.id}
                    type="button"
                    onClick={() => setTab(item.id)}
                    className={`inline-flex items-center gap-1.5 rounded-lg px-3 py-2 text-xs font-semibold ${
                      tab === item.id ? 'bg-slate-900 text-white' : 'text-slate-600 hover:bg-slate-100'
                    }`}
                  >
                    {item.icon}{item.label}
                  </button>
                ))}
              </div>

              {tab === 'keywords' && (
                <div className="rounded-xl border border-slate-200 bg-white shadow-sm">
                  <div className="flex flex-wrap items-center justify-between gap-2 border-b border-slate-100 p-4">
                    <div>
                      <h3 className="text-sm font-bold text-slate-800">Keyword positions</h3>
                      <p className="text-[11px] text-slate-500">Record SERP ranks manually or import them from your provider.</p>
                    </div>
                    <div className="flex gap-2">
                      <button type="button" onClick={() => setResearchModal(true)} className="inline-flex items-center gap-1.5 rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50">
                        <Search className="h-3.5 w-3.5" /> Research
                      </button>
                      <button type="button" onClick={() => setKeywordModal(true)} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700">
                        <Plus className="h-3.5 w-3.5" /> Add keyword
                      </button>
                    </div>
                  </div>
                  <div className="overflow-x-auto">
                    <table className="w-full text-left text-sm">
                      <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
                        <tr>
                          <th className="px-4 py-3 font-semibold">Keyword</th>
                          <th className="px-4 py-3 font-semibold">Position</th>
                          <th className="px-4 py-3 font-semibold">Intent</th>
                          <th className="px-4 py-3 font-semibold">Priority</th>
                          <th className="px-4 py-3 font-semibold">Volume</th>
                          <th className="px-4 py-3 font-semibold">Difficulty</th>
                          <th className="px-4 py-3 font-semibold">Checked</th>
                          <th className="px-4 py-3 text-right font-semibold">Actions</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-slate-100">
                        {keywords.map((keyword) => (
                          <tr key={keyword.id} className="hover:bg-slate-50/70">
                            <td className="px-4 py-3">
                              <div className="flex items-center gap-1.5 font-medium text-slate-900">
                                {keyword.keyword}
                                {keyword.target_url && (
                                  <a href={keyword.target_url} target="_blank" rel="noreferrer noopener" className="text-slate-400 hover:text-blue-600" aria-label="Open target URL">
                                    <ExternalLink className="h-3 w-3" />
                                  </a>
                                )}
                              </div>
                              <div className="text-[10px] text-slate-400">{keyword.source}</div>
                            </td>
                            <td className="px-4 py-3">
                              <div className="flex items-center gap-1.5 tabular-nums">
                                <span className="font-semibold text-slate-800">{keyword.current_position || '—'}</span>
                                {positionDelta(keyword)}
                                {keyword.previous_position > 0 && <span className="text-[10px] text-slate-400">prev {keyword.previous_position}</span>}
                              </div>
                            </td>
                            <td className="px-4 py-3 text-xs capitalize text-slate-600">{keyword.intent}</td>
                            <td className="px-4 py-3 text-xs text-slate-600">P{keyword.priority}</td>
                            <td className="px-4 py-3 text-xs tabular-nums text-slate-600">{formatNumber(keyword.search_volume)}</td>
                            <td className="px-4 py-3 text-xs tabular-nums text-slate-600">{keyword.difficulty}</td>
                            <td className="px-4 py-3 text-xs text-slate-500">{formatDate(keyword.last_checked_at)}</td>
                            <td className="px-4 py-3 text-right">
                              <div className="inline-flex gap-1">
                                <button type="button" onClick={() => openRank(keyword)} className="rounded-md border border-slate-200 px-2 py-1 text-[11px] font-semibold text-slate-700 hover:bg-slate-50">
                                  Rank
                                </button>
                                <button type="button" onClick={() => api.deleteSeoKeyword(keyword.id).then(() => { if (selected) loadProject(selected.id); })} className="rounded-md border border-slate-200 p-1.5 text-slate-400 hover:bg-rose-50 hover:text-rose-600" aria-label="Delete keyword">
                                  <Trash2 className="h-3 w-3" />
                                </button>
                              </div>
                            </td>
                          </tr>
                        ))}
                        {keywords.length === 0 && (
                          <tr><td colSpan={8} className="px-4 py-10 text-center text-sm text-slate-500">No keywords yet.</td></tr>
                        )}
                      </tbody>
                    </table>
                  </div>
                </div>
              )}

              {tab === 'competitors' && (
                <div className="space-y-4">
                  <div className="flex justify-end">
                    <button type="button" onClick={() => setCompetitorModal(true)} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700">
                      <Plus className="h-3.5 w-3.5" /> Add competitor
                    </button>
                  </div>
                  {competitors.map((competitor) => (
                    <div key={competitor.id} className="rounded-xl border border-slate-200 bg-white p-4">
                      <div className="flex flex-wrap items-center justify-between gap-2">
                        <div>
                          <div className="flex items-center gap-2 text-sm font-bold text-slate-900">
                            <Globe2 className="h-4 w-4 text-slate-400" /> {competitor.name || competitor.domain}
                          </div>
                          <div className="text-[11px] text-slate-500">{competitor.domain}</div>
                        </div>
                        <div className="flex gap-2">
                          <button type="button" onClick={() => { setCompetitorKeywordFor(competitor); setCompetitorKeyword(''); setCompetitorKeywordPosition('0'); }} className="rounded-lg border border-slate-200 px-2 py-1 text-[11px] font-semibold text-slate-700 hover:bg-slate-50">
                            Add keyword
                          </button>
                          <button type="button" onClick={() => api.deleteSeoCompetitor(competitor.id).then(() => { if (selected) loadProject(selected.id); })} className="rounded-lg border border-slate-200 p-1.5 text-slate-400 hover:bg-rose-50 hover:text-rose-600" aria-label="Delete competitor">
                            <Trash2 className="h-3.5 w-3.5" />
                          </button>
                        </div>
                      </div>
                      <div className="mt-3 overflow-x-auto">
                        <table className="w-full text-left text-xs">
                          <thead className="text-[10px] uppercase tracking-wider text-slate-500">
                            <tr>
                              <th className="pb-2 font-semibold">Competitor keyword</th>
                              <th className="pb-2 font-semibold">Position</th>
                              <th className="pb-2 font-semibold">Our position</th>
                              <th className="pb-2 text-right font-semibold">Actions</th>
                            </tr>
                          </thead>
                          <tbody className="divide-y divide-slate-100">
                            {competitor.keywords.map((keyword) => {
                              const ours = keywords.find((row) => row.keyword.toLowerCase() === keyword.keyword.toLowerCase());
                              return (
                                <tr key={keyword.id}>
                                  <td className="py-2 text-slate-700">{keyword.keyword}</td>
                                  <td className="py-2 tabular-nums text-slate-600">{keyword.position || '—'}</td>
                                  <td className="py-2 tabular-nums text-slate-600">{ours?.current_position || 'Not tracked'}</td>
                                  <td className="py-2 text-right">
                                    <button type="button" onClick={() => api.deleteSeoCompetitorKeyword(competitor.id, keyword.id).then(() => { if (selected) loadProject(selected.id); })} className="rounded p-1 text-slate-300 hover:text-rose-600" aria-label="Delete competitor keyword">
                                      <Trash2 className="h-3 w-3" />
                                    </button>
                                  </td>
                                </tr>
                              );
                            })}
                            {competitor.keywords.length === 0 && (
                              <tr><td colSpan={4} className="py-4 text-center text-slate-400">No competitor keywords yet.</td></tr>
                            )}
                          </tbody>
                        </table>
                      </div>
                    </div>
                  ))}
                  {competitors.length === 0 && (
                    <div className="rounded-xl border border-dashed border-slate-300 bg-white p-10 text-center text-sm text-slate-500">
                      Add a competitor to compare keyword coverage.
                    </div>
                  )}
                </div>
              )}

              {tab === 'backlinks' && (
                <div className="rounded-xl border border-slate-200 bg-white p-4">
                  <div className="flex flex-wrap items-center justify-between gap-2">
                    <div>
                      <h3 className="text-sm font-bold text-slate-800">Backlink monitor</h3>
                      <p className="text-[11px] text-slate-500">Track active and lost links. {summary.active_backlinks} active · {summary.lost_backlinks} lost.</p>
                    </div>
                    <button type="button" onClick={() => setBacklinkModal(true)} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700">
                      <Plus className="h-3.5 w-3.5" /> Add backlink
                    </button>
                  </div>
                  <div className="mt-4 overflow-x-auto">
                    <table className="w-full text-left text-xs">
                      <thead className="text-[10px] uppercase tracking-wider text-slate-500">
                        <tr>
                          <th className="pb-2 font-semibold">Source</th>
                          <th className="pb-2 font-semibold">Anchor</th>
                          <th className="pb-2 font-semibold">Status</th>
                          <th className="pb-2 font-semibold">Type</th>
                          <th className="pb-2 font-semibold">First seen</th>
                          <th className="pb-2 text-right font-semibold">Actions</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-slate-100">
                        {backlinks.map((backlink) => (
                          <tr key={backlink.id}>
                            <td className="max-w-[18rem] py-3">
                              <a href={backlink.source_url} target="_blank" rel="noreferrer noopener" className="truncate text-blue-700 hover:underline">
                                {backlink.domain || backlink.source_url}
                              </a>
                              <div className="truncate text-[10px] text-slate-400">{backlink.source_url}</div>
                            </td>
                            <td className="py-3 text-slate-600">{backlink.anchor_text || '—'}</td>
                            <td className="py-3">
                              <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold ${
                                backlink.status === 'active'
                                  ? 'border-emerald-200 bg-emerald-50 text-emerald-700'
                                  : backlink.status === 'lost'
                                    ? 'border-rose-200 bg-rose-50 text-rose-700'
                                    : 'border-amber-200 bg-amber-50 text-amber-700'
                              }`}>
                                {backlink.status}
                              </span>
                            </td>
                            <td className="py-3 text-slate-600">{backlink.nofollow ? 'nofollow' : 'follow'}</td>
                            <td className="py-3 text-slate-500">{formatDate(backlink.first_seen_at)}</td>
                            <td className="py-3 text-right">
                              <div className="inline-flex gap-1">
                                <button type="button" onClick={() => toggleBacklink(backlink)} className="rounded border border-slate-200 px-2 py-1 text-[10px] font-semibold text-slate-600 hover:bg-slate-50">
                                  Mark {backlink.status === 'active' ? 'lost' : 'active'}
                                </button>
                                <button type="button" onClick={() => api.deleteSeoBacklink(backlink.id).then(() => { if (selected) loadProject(selected.id); })} className="rounded p-1 text-slate-300 hover:text-rose-600" aria-label="Delete backlink">
                                  <Trash2 className="h-3 w-3" />
                                </button>
                              </div>
                            </td>
                          </tr>
                        ))}
                        {backlinks.length === 0 && (
                          <tr><td colSpan={6} className="py-8 text-center text-slate-500">No backlinks recorded.</td></tr>
                        )}
                      </tbody>
                    </table>
                  </div>
                </div>
              )}

              {tab === 'audits' && (
                <div className="space-y-4">
                  <div className="rounded-xl border border-slate-200 bg-white p-4">
                    <h3 className="text-sm font-bold text-slate-800">Run a technical audit</h3>
                    <p className="mt-1 text-xs text-slate-500">
                      Fetches a page from this verified site and checks title, meta description, headings,
                      canonicals, structured data, images, robots.txt, and sitemap.xml.
                    </p>
                    <div className="mt-3 flex flex-col gap-2 sm:flex-row">
                      <input
                        value={auditTarget}
                        onChange={(event) => setAuditTarget(event.target.value)}
                        placeholder={`https://${selected.domain}/`}
                        className="min-w-0 flex-1 rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                      />
                      <button type="button" onClick={runAudit} disabled={runningAudit} className="inline-flex items-center justify-center gap-1.5 rounded-lg bg-slate-900 px-3 py-2 text-xs font-semibold text-white hover:bg-slate-800 disabled:opacity-60">
                        {runningAudit ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <ShieldCheck className="h-3.5 w-3.5" />}
                        Run audit
                      </button>
                    </div>
                  </div>
                  <div className="space-y-2">
                    {audits.map((audit) => (
                      <button key={audit.id} type="button" onClick={() => openAudit(audit)} className="flex w-full items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white p-4 text-left hover:bg-slate-50">
                        <div className="min-w-0">
                          <div className="truncate text-sm font-medium text-slate-800">{audit.target_url}</div>
                          <div className="text-[11px] text-slate-400">{formatDate(audit.checked_at)} · {audit.status}</div>
                        </div>
                        <div className={`text-xl font-bold tabular-nums ${scoreTone(audit.score)}`}>{audit.score}</div>
                      </button>
                    ))}
                    {audits.length === 0 && <p className="text-sm text-slate-500">No audits yet.</p>}
                  </div>
                </div>
              )}

              {tab === 'geo' && (
                <div className="space-y-4">
                  <div className="flex justify-end">
                    <button type="button" onClick={() => setGeoModal(true)} className="inline-flex items-center gap-1.5 rounded-lg bg-violet-600 px-3 py-2 text-xs font-semibold text-white hover:bg-violet-700">
                      <Plus className="h-3.5 w-3.5" /> Add AI query
                    </button>
                  </div>
                  {geoQueries.map((query) => (
                    <div key={query.id} className="rounded-xl border border-slate-200 bg-white p-4">
                      <div className="flex flex-wrap items-start justify-between gap-2">
                        <div>
                          <div className="text-sm font-bold text-slate-900">{query.query_text}</div>
                          <div className="mt-1 flex flex-wrap gap-1">
                            {query.engines.map((engine) => (
                              <span key={engine} className="rounded-full border border-violet-200 bg-violet-50 px-2 py-0.5 text-[10px] font-semibold text-violet-700">
                                {ENGINE_LABELS[engine] || engine}
                              </span>
                            ))}
                          </div>
                          <div className="mt-1 text-[11px] text-slate-500">
                            {query.checks || 0} checks · {query.mention_rate_percent?.toFixed(0) || 0}% mention rate · last {formatDate(query.last_checked_at)}
                          </div>
                        </div>
                        <div className="flex gap-2">
                          <button type="button" onClick={() => { setMentionFor(query); setMentionEngine(query.engines[0] || 'chatgpt'); setMentionPresent(true); setMentionPosition('1'); setMentionSnippet(''); }} className="rounded-lg border border-violet-200 bg-violet-50 px-2 py-1 text-[11px] font-semibold text-violet-700 hover:bg-violet-100">
                            Record check
                          </button>
                          <button type="button" onClick={() => api.deleteGeoQuery(query.id).then(() => { if (selected) loadProject(selected.id); })} className="rounded-lg border border-slate-200 p-1.5 text-slate-400 hover:bg-rose-50 hover:text-rose-600" aria-label="Delete GEO query">
                            <Trash2 className="h-3.5 w-3.5" />
                          </button>
                        </div>
                      </div>
                      {query.mentions && query.mentions.length > 0 && (
                        <div className="mt-3 overflow-x-auto">
                          <table className="w-full text-left text-xs">
                            <thead className="text-[10px] uppercase tracking-wider text-slate-500">
                              <tr>
                                <th className="pb-2 font-semibold">Engine</th>
                                <th className="pb-2 font-semibold">Mentioned</th>
                                <th className="pb-2 font-semibold">Position</th>
                                <th className="pb-2 font-semibold">Snippet</th>
                                <th className="pb-2 font-semibold">Checked</th>
                              </tr>
                            </thead>
                            <tbody className="divide-y divide-slate-100">
                              {query.mentions.slice(0, 10).map((mention) => (
                                <tr key={mention.id}>
                                  <td className="py-2 text-slate-700">{ENGINE_LABELS[mention.engine] || mention.engine}</td>
                                  <td className="py-2">
                                    <span className={`inline-flex items-center gap-1 rounded-full border px-2 py-0.5 text-[10px] font-semibold ${
                                      mention.mentioned
                                        ? 'border-emerald-200 bg-emerald-50 text-emerald-700'
                                        : 'border-slate-200 bg-slate-50 text-slate-500'
                                    }`}>
                                      {mention.mentioned ? <Check className="h-3 w-3" /> : <Minus className="h-3 w-3" />}
                                      {mention.mentioned ? 'Yes' : 'No'}
                                    </span>
                                  </td>
                                  <td className="py-2 tabular-nums text-slate-600">{mention.position || '—'}</td>
                                  <td className="max-w-[22rem] truncate py-2 text-slate-500">{mention.snippet || '—'}</td>
                                  <td className="py-2 text-slate-500">{formatDate(mention.checked_at)}</td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      )}
                    </div>
                  ))}
                  {geoQueries.length === 0 && (
                    <div className="rounded-xl border border-dashed border-slate-300 bg-white p-10 text-center text-sm text-slate-500">
                      Add a question your buyers ask AI assistants, then record whether your site appears.
                    </div>
                  )}
                </div>
              )}

              {tab === 'content' && (
                <div className="space-y-4">
                  <form onSubmit={runOptimize} className="rounded-xl border border-slate-200 bg-white p-4">
                    <h3 className="text-sm font-bold text-slate-800">Content optimization</h3>
                    <p className="mt-1 text-xs text-slate-500">
                      Analyze a page against one target keyword. The page must belong to this site.
                    </p>
                    <div className="mt-3 grid gap-2 sm:grid-cols-2">
                      <input
                        value={optimizeKeyword}
                        onChange={(event) => setOptimizeKeyword(event.target.value)}
                        placeholder="Target keyword"
                        className="rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                      />
                      <input
                        value={optimizeUrl}
                        onChange={(event) => setOptimizeUrl(event.target.value)}
                        placeholder={`https://${selected.domain}/`}
                        className="rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                      />
                    </div>
                    <div className="mt-3 flex justify-end">
                      <button type="submit" disabled={busy || !optimizeKeyword.trim()} className="inline-flex items-center gap-1.5 rounded-lg bg-slate-900 px-3 py-2 text-xs font-semibold text-white hover:bg-slate-800 disabled:opacity-60">
                        {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <FileText className="h-3.5 w-3.5" />}
                        Analyze content
                      </button>
                    </div>
                  </form>
                  {optimizeResult && (
                    <div className="rounded-xl border border-slate-200 bg-white p-5">
                      <div className="flex flex-wrap items-center justify-between gap-3">
                        <div>
                          <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Optimization score</div>
                          <div className={`text-3xl font-bold ${scoreTone(Number(optimizeResult.score) || 0)}`}>
                            {optimizeResult.score}
                          </div>
                        </div>
                        <div className="grid grid-cols-3 gap-4 text-center text-xs">
                          <div>
                            <div className="font-bold text-slate-800">{optimizeResult.word_count}</div>
                            <div className="text-slate-500">words</div>
                          </div>
                          <div>
                            <div className="font-bold text-slate-800">{optimizeResult.keyword_occurrences}</div>
                            <div className="text-slate-500">mentions</div>
                          </div>
                          <div>
                            <div className="font-bold text-slate-800">{Number(optimizeResult.keyword_density_percent || 0).toFixed(2)}%</div>
                            <div className="text-slate-500">density</div>
                          </div>
                        </div>
                      </div>
                      <div className="mt-4 space-y-2">
                        {(optimizeResult.suggestions || []).map((suggestion: string, index: number) => (
                          <div key={index} className="rounded-lg border border-slate-100 bg-slate-50 p-3 text-sm text-slate-700">
                            {suggestion}
                          </div>
                        ))}
                      </div>
                    </div>
                  )}
                </div>
              )}
            </>
          ) : (
            <div className="rounded-xl border border-dashed border-slate-300 bg-white p-12 text-center">
              <Search className="mx-auto h-8 w-8 text-slate-300" />
              <h3 className="mt-3 text-sm font-semibold text-slate-800">No SEO project selected</h3>
              <p className="mt-1 text-sm text-slate-500">Create a project for a verified site to start tracking keywords.</p>
            </div>
          )}
        </div>
      </div>

      <Modal isOpen={projectModal} onClose={() => setProjectModal(false)} title="New SEO project">
        <form onSubmit={createProject} className="space-y-3">
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Site</label>
            <select
              value={projectSite}
              onChange={(event) => {
                setProjectSite(event.target.value);
                const site = sites.find((row) => row.id === event.target.value);
                setProjectDomain(site?.primary_domain || '');
              }}
              required
              className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
            >
              <option value="">Select a site</option>
              {sites.map((site) => <option key={site.id} value={site.id}>{site.name} ({site.primary_domain})</option>)}
            </select>
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Project name</label>
            <input value={projectName} onChange={(event) => setProjectName(event.target.value)} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Domain</label>
              <input value={projectDomain} onChange={(event) => setProjectDomain(event.target.value)} placeholder="example.com" className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Country</label>
              <input value={projectCountry} onChange={(event) => setProjectCountry(event.target.value)} maxLength={2} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm uppercase focus:border-blue-500 focus:outline-none" />
            </div>
          </div>
          <div className="flex justify-end gap-2 pt-2">
            <button type="button" onClick={() => setProjectModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50">Cancel</button>
            <button type="submit" disabled={busy} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60">
              {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Plus className="h-3.5 w-3.5" />} Create project
            </button>
          </div>
        </form>
      </Modal>

      <Modal isOpen={keywordModal} onClose={() => setKeywordModal(false)} title="Add keyword">
        <form onSubmit={addKeyword} className="space-y-3">
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Keyword</label>
            <input value={keywordText} onChange={(event) => setKeywordText(event.target.value)} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Target URL</label>
            <input value={keywordUrl} onChange={(event) => setKeywordUrl(event.target.value)} placeholder={`https://${selected?.domain || 'example.com'}/`} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Intent</label>
              <select value={keywordIntent} onChange={(event) => setKeywordIntent(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value="informational">Informational</option>
                <option value="commercial">Commercial</option>
                <option value="transactional">Transactional</option>
                <option value="navigational">Navigational</option>
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Priority</label>
              <select value={keywordPriority} onChange={(event) => setKeywordPriority(Number(event.target.value))} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                <option value={1}>P1 — Core</option>
                <option value={2}>P2 — Growth</option>
                <option value={3}>P3 — Long tail</option>
              </select>
            </div>
          </div>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setKeywordModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="submit" disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add keyword</button></div>
        </form>
      </Modal>

      <Modal isOpen={researchModal} onClose={() => setResearchModal(false)} title="Keyword research" maxWidth="max-w-2xl">
        <div className="space-y-3">
          <p className="rounded-lg bg-slate-50 p-3 text-xs leading-5 text-slate-500">
            Generate local seed-based ideas. Volumes and difficulty are not fetched from a third-party
            provider; add verified values after import.
          </p>
          <div className="flex gap-2">
            <input value={researchSeed} onChange={(event) => setResearchSeed(event.target.value)} placeholder="e.g. affiliate marketing" className="min-w-0 flex-1 rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
            <button type="button" onClick={runResearch} disabled={busy || researchSeed.trim().length < 2} className="rounded-lg bg-slate-900 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Generate</button>
          </div>
          {researchResults.length > 0 && (
            <>
              <div className="max-h-64 space-y-1 overflow-auto rounded-lg border border-slate-200 p-2">
                {researchResults.map((result) => (
                  <div key={result.keyword} className="flex items-center justify-between rounded-md px-2 py-1.5 text-xs hover:bg-slate-50">
                    <span className="text-slate-700">{result.keyword}</span>
                    <span className="text-[10px] capitalize text-slate-400">{result.intent}</span>
                  </div>
                ))}
              </div>
              <div className="flex justify-end"><button type="button" onClick={addResearchResults} disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add {researchResults.length} keywords</button></div>
            </>
          )}
        </div>
      </Modal>

      <Modal isOpen={!!rankKeyword} onClose={() => setRankKeyword(null)} title={`Record SERP rank · ${rankKeyword?.keyword || ''}`}>
        <div className="space-y-3">
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Position (0 = not ranking)</label>
            <input type="number" min={0} max={1000} value={rankPosition} onChange={(event) => setRankPosition(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Ranking URL</label>
            <input value={rankUrl} onChange={(event) => setRankUrl(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Note</label>
            <input value={rankNote} onChange={(event) => setRankNote(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" />
          </div>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setRankKeyword(null)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="button" onClick={saveRank} disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Save rank</button></div>
        </div>
      </Modal>

      <Modal isOpen={competitorModal} onClose={() => setCompetitorModal(false)} title="Add competitor">
        <form onSubmit={addCompetitor} className="space-y-3">
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Domain</label><input value={competitorDomain} onChange={(event) => setCompetitorDomain(event.target.value)} placeholder="competitor.com" required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Name</label><input value={competitorName} onChange={(event) => setCompetitorName(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setCompetitorModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="submit" disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add competitor</button></div>
        </form>
      </Modal>

      <Modal isOpen={!!competitorKeywordFor} onClose={() => setCompetitorKeywordFor(null)} title={`Add competitor keyword · ${competitorKeywordFor?.domain || ''}`}>
        <div className="space-y-3">
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Keyword</label><input value={competitorKeyword} onChange={(event) => setCompetitorKeyword(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Their position</label><input type="number" min={0} value={competitorKeywordPosition} onChange={(event) => setCompetitorKeywordPosition(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setCompetitorKeywordFor(null)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="button" onClick={addCompetitorKeyword} disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add keyword</button></div>
        </div>
      </Modal>

      <Modal isOpen={backlinkModal} onClose={() => setBacklinkModal(false)} title="Add backlink">
        <form onSubmit={addBacklink} className="space-y-3">
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Source URL</label><input value={backlinkSource} onChange={(event) => setBacklinkSource(event.target.value)} placeholder="https://blog.example.com/post" required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Target URL</label><input value={backlinkTarget} onChange={(event) => setBacklinkTarget(event.target.value)} placeholder={`https://${selected?.domain || 'example.com'}/`} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Anchor text</label><input value={backlinkAnchor} onChange={(event) => setBacklinkAnchor(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <label className="flex items-center gap-2 text-xs text-slate-600"><input type="checkbox" checked={backlinkNofollow} onChange={(event) => setBacklinkNofollow(event.target.checked)} /> Nofollow</label>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setBacklinkModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="submit" disabled={busy} className="rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add backlink</button></div>
        </form>
      </Modal>

      <Modal isOpen={!!auditDetail} onClose={() => setAuditDetail(null)} title="Technical audit" maxWidth="max-w-3xl">
        {auditDetail && (
          <div className="space-y-4">
            <div className="flex flex-wrap items-center justify-between gap-3">
              <div>
                <div className="text-sm font-semibold text-slate-800">{auditDetail.target_url}</div>
                <div className="text-[11px] text-slate-500">{formatDate(auditDetail.checked_at)}</div>
              </div>
              <div className={`text-3xl font-bold ${scoreTone(auditDetail.score)}`}>{auditDetail.score}</div>
            </div>
            <div className="grid grid-cols-2 gap-2 text-xs sm:grid-cols-4">
              {Object.entries(auditDetail.summary || {}).filter(([key]) => ['http_status', 'word_count', 'h1_count', 'images_missing_alt', 'internal_links', 'external_links'].includes(key)).map(([key, value]) => (
                <div key={key} className="rounded-lg bg-slate-50 p-2">
                  <div className="text-[10px] uppercase tracking-wider text-slate-400">{key.replace(/_/g, ' ')}</div>
                  <div className="font-semibold text-slate-700">{String(value)}</div>
                </div>
              ))}
            </div>
            <div className="space-y-2">
              {(auditDetail.issues || []).map((issue) => (
                <div key={issue.id} className="rounded-lg border border-slate-200 p-3">
                  <div className="flex items-center justify-between gap-2">
                    <div className="text-sm font-semibold text-slate-800">{issue.message}</div>
                    <span className={`rounded-full px-2 py-0.5 text-[10px] font-semibold ${
                      issue.severity === 'critical' || issue.severity === 'high'
                        ? 'bg-rose-100 text-rose-700'
                        : issue.severity === 'medium'
                          ? 'bg-amber-100 text-amber-700'
                          : 'bg-slate-100 text-slate-600'
                    }`}>{issue.severity}</span>
                  </div>
                  <p className="mt-1 text-xs text-slate-500">{issue.recommendation}</p>
                  <div className="mt-1 text-[10px] text-slate-400">{issue.category} · {issue.code}</div>
                </div>
              ))}
              {(auditDetail.issues || []).length === 0 && <p className="text-sm text-slate-500">No issues found.</p>}
            </div>
          </div>
        )}
      </Modal>

      <Modal isOpen={geoModal} onClose={() => setGeoModal(false)} title="Add AI visibility query">
        <form onSubmit={addGeoQuery} className="space-y-3">
          <div><label className="mb-1 block text-xs font-semibold text-slate-700">Question buyers ask AI</label><textarea value={geoQueryText} onChange={(event) => setGeoQueryText(event.target.value)} rows={3} required className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
          <div>
            <label className="mb-1 block text-xs font-semibold text-slate-700">Engines</label>
            <div className="grid grid-cols-2 gap-2">
              {Object.entries(ENGINE_LABELS).map(([engine, label]) => (
                <label key={engine} className="flex items-center gap-2 text-xs text-slate-600">
                  <input
                    type="checkbox"
                    checked={geoEngines.includes(engine)}
                    onChange={(event) => setGeoEngines((current) => (
                      event.target.checked ? [...current, engine] : current.filter((value) => value !== engine)
                    ))}
                  />
                  {label}
                </label>
              ))}
            </div>
          </div>
          <div className="flex justify-end gap-2"><button type="button" onClick={() => setGeoModal(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="submit" disabled={busy || !geoQueryText.trim() || geoEngines.length === 0} className="rounded-lg bg-violet-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Add query</button></div>
        </form>
      </Modal>

      <Modal isOpen={!!mentionFor} onClose={() => setMentionFor(null)} title="Record AI visibility check">
        {mentionFor && (
          <div className="space-y-3">
            <div className="rounded-lg bg-slate-50 p-3 text-xs text-slate-600">{mentionFor.query_text}</div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Engine</label>
              <select value={mentionEngine} onChange={(event) => setMentionEngine(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none">
                {mentionFor.engines.map((engine) => <option key={engine} value={engine}>{ENGINE_LABELS[engine] || engine}</option>)}
              </select>
            </div>
            <label className="flex items-center gap-2 text-xs text-slate-600"><input type="checkbox" checked={mentionPresent} onChange={(event) => setMentionPresent(event.target.checked)} /> Our site is mentioned</label>
            <div className="grid gap-3 sm:grid-cols-2">
              <div><label className="mb-1 block text-xs font-semibold text-slate-700">Position</label><input type="number" min={0} max={100} value={mentionPosition} onChange={(event) => setMentionPosition(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
              <div><label className="mb-1 block text-xs font-semibold text-slate-700">Snippet</label><input value={mentionSnippet} onChange={(event) => setMentionSnippet(event.target.value)} className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none" /></div>
            </div>
            <div className="flex justify-end gap-2"><button type="button" onClick={() => setMentionFor(null)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700">Cancel</button><button type="button" onClick={recordMention} disabled={busy} className="rounded-lg bg-violet-600 px-3 py-2 text-xs font-semibold text-white disabled:opacity-60">Save check</button></div>
          </div>
        )}
      </Modal>
    </div>
  );
};
