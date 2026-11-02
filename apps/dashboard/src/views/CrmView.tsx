import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Building2,
  CalendarClock,
  CheckCircle2,
  Globe,
  Loader2,
  Mail,
  MessageSquare,
  Plus,
  RefreshCw,
  Save,
  Search,
  Send,
  Trash2,
  UserCircle,
  Users,
} from 'lucide-react';
import { api } from '../api';
import {
  CrmActivity,
  CrmLead,
  CrmPipelineStage,
  CrmStage,
  CrmSummary,
  CrmTask,
  CrmTaskStatus,
} from '../types';
import { Modal } from '../components/Modal';

interface CrmViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

const STAGES: Array<{ id: CrmStage; label: string; tone: string }> = [
  { id: 'new', label: 'New', tone: 'border-slate-300 bg-slate-100' },
  { id: 'contacted', label: 'Contacted', tone: 'border-blue-200 bg-blue-50' },
  { id: 'qualified', label: 'Qualified', tone: 'border-cyan-200 bg-cyan-50' },
  { id: 'proposal', label: 'Proposal', tone: 'border-violet-200 bg-violet-50' },
  { id: 'negotiation', label: 'Negotiation', tone: 'border-amber-200 bg-amber-50' },
  { id: 'won', label: 'Won', tone: 'border-emerald-200 bg-emerald-50' },
  { id: 'lost', label: 'Lost', tone: 'border-rose-200 bg-rose-50' },
];

const STAGE_LABELS = new Map(STAGES.map((stage) => [stage.id, stage.label]));

const EMPTY_SUMMARY: CrmSummary = {
  total_leads: 0,
  open_leads: 0,
  won_leads: 0,
  pipeline_value_cents: 0,
  won_value_cents: 0,
  overdue_tasks: 0,
};

function formatMoney(cents: number, currency = 'USD'): string {
  try {
    return new Intl.NumberFormat('en-US', {
      style: 'currency',
      currency: currency || 'USD',
      maximumFractionDigits: 0,
    }).format((cents || 0) / 100);
  } catch (_) {
    return `${currency || 'USD'} ${((cents || 0) / 100).toFixed(0)}`;
  }
}

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function toDateTimeInput(value?: string): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '';
  return new Date(date.getTime() - date.getTimezoneOffset() * 60_000).toISOString().slice(0, 16);
}

function localInputToIso(value: string): string {
  if (!value) return '';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? '' : date.toISOString();
}

function scoreTone(score: number): string {
  if (score >= 70) return 'bg-emerald-100 text-emerald-700 border-emerald-200';
  if (score >= 40) return 'bg-amber-100 text-amber-700 border-amber-200';
  return 'bg-slate-100 text-slate-600 border-slate-200';
}

function isOverdue(value?: string): boolean {
  if (!value) return false;
  const date = new Date(value);
  return !Number.isNaN(date.getTime()) && date.getTime() < Date.now();
}

const emptyNewLead = {
  name: '',
  email: '',
  company: '',
  website: '',
  phone: '',
  source: 'manual',
  value: '',
  tags: '',
  notes: '',
  next_follow_up: '',
};

export const CrmView: React.FC<CrmViewProps> = ({ onNotify }) => {
  const [leads, setLeads] = useState<CrmLead[]>([]);
  const [pipeline, setPipeline] = useState<CrmPipelineStage[]>([]);
  const [summary, setSummary] = useState<CrmSummary>(EMPTY_SUMMARY);
  const [owners, setOwners] = useState<Array<{ id: string; email: string; role: string }>>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState('');
  const [stageFilter, setStageFilter] = useState<'all' | CrmStage>('all');
  const [newOpen, setNewOpen] = useState(false);
  const [newLead, setNewLead] = useState(emptyNewLead);
  const [busy, setBusy] = useState(false);

  const [selected, setSelected] = useState<CrmLead | null>(null);
  const [activities, setActivities] = useState<CrmActivity[]>([]);
  const [tasks, setTasks] = useState<CrmTask[]>([]);
  const [detailLoading, setDetailLoading] = useState(false);
  const [editStage, setEditStage] = useState<CrmStage>('new');
  const [editOwner, setEditOwner] = useState('');
  const [editValue, setEditValue] = useState('');
  const [editTags, setEditTags] = useState('');
  const [editNotes, setEditNotes] = useState('');
  const [editFollowUp, setEditFollowUp] = useState('');
  const [activityType, setActivityType] = useState<'note' | 'call' | 'meeting'>('note');
  const [activitySubject, setActivitySubject] = useState('');
  const [activityBody, setActivityBody] = useState('');
  const [emailSubject, setEmailSubject] = useState('');
  const [emailBody, setEmailBody] = useState('');
  const [taskTitle, setTaskTitle] = useState('');
  const [taskDue, setTaskDue] = useState('');

  const ownerOptions = useMemo(
    () => owners.map((owner) => ({ id: owner.id, email: owner.email })),
    [owners],
  );

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const data = await api.getCrmLeads({ q: search.trim() || undefined, limit: 300 });
      setLeads(data.leads);
      setPipeline(data.pipeline);
      setSummary(data.summary);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load CRM data');
    } finally {
      setLoading(false);
    }
  }, [onNotify, search]);

  useEffect(() => {
    load();
  }, [load]);

  useEffect(() => {
    api.getCrmOwners()
      .then(setOwners)
      .catch(() => setOwners([]));
  }, []);

  const openLead = async (lead: CrmLead) => {
    setSelected(lead);
    setDetailLoading(true);
    setEditStage(lead.stage);
    setEditOwner(lead.owner_user_id || '');
    setEditValue((lead.value_cents / 100).toFixed(2));
    setEditTags(lead.tags.join(', '));
    setEditNotes(lead.notes);
    setEditFollowUp(toDateTimeInput(lead.next_follow_up_at));
    setActivityType('note');
    setActivitySubject('');
    setActivityBody('');
    setEmailSubject('');
    setEmailBody('');
    setTaskTitle('');
    setTaskDue('');
    try {
      const detail = await api.getCrmLead(lead.id);
      setSelected(detail.lead);
      setActivities(detail.activities);
      setTasks(detail.tasks);
      setEditStage(detail.lead.stage);
      setEditOwner(detail.lead.owner_user_id || '');
      setEditValue((detail.lead.value_cents / 100).toFixed(2));
      setEditTags(detail.lead.tags.join(', '));
      setEditNotes(detail.lead.notes);
      setEditFollowUp(toDateTimeInput(detail.lead.next_follow_up_at));
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load lead');
    } finally {
      setDetailLoading(false);
    }
  };

  const refreshLead = async (leadId: string) => {
    const [detail] = await Promise.all([api.getCrmLead(leadId), load()]);
    setSelected(detail.lead);
    setActivities(detail.activities);
    setTasks(detail.tasks);
    setEditStage(detail.lead.stage);
    setEditOwner(detail.lead.owner_user_id || '');
    setEditValue((detail.lead.value_cents / 100).toFixed(2));
    setEditTags(detail.lead.tags.join(', '));
    setEditNotes(detail.lead.notes);
    setEditFollowUp(toDateTimeInput(detail.lead.next_follow_up_at));
  };

  const handleCreate = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!newLead.name.trim() || !newLead.email.trim()) return;
    setBusy(true);
    try {
      await api.createCrmLead({
        name: newLead.name.trim(),
        email: newLead.email.trim(),
        company: newLead.company.trim(),
        website: newLead.website.trim(),
        phone: newLead.phone.trim(),
        source: newLead.source.trim() || 'manual',
        value_cents: Math.round(Number(newLead.value || 0) * 100),
        tags: newLead.tags.split(',').map((tag) => tag.trim()).filter(Boolean),
        notes: newLead.notes.trim(),
        next_follow_up_at: newLead.next_follow_up ? localInputToIso(newLead.next_follow_up) : undefined,
      });
      setNewOpen(false);
      setNewLead(emptyNewLead);
      onNotify('success', 'Lead created.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create lead');
    } finally {
      setBusy(false);
    }
  };

  const saveLead = async () => {
    if (!selected || busy) return;
    setBusy(true);
    try {
      await api.updateCrmLead(selected.id, {
        stage: editStage,
        owner_user_id: editOwner,
        value_cents: Math.max(0, Math.round(Number(editValue || 0) * 100)),
        tags: editTags.split(',').map((tag) => tag.trim()).filter(Boolean),
        notes: editNotes,
        next_follow_up_at: editFollowUp ? localInputToIso(editFollowUp) : '',
      });
      onNotify('success', 'Lead updated.');
      await refreshLead(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update lead');
    } finally {
      setBusy(false);
    }
  };

  const addActivity = async () => {
    if (!selected || (!activitySubject.trim() && !activityBody.trim())) return;
    setBusy(true);
    try {
      await api.addCrmActivity(selected.id, {
        type: activityType,
        subject: activitySubject.trim(),
        body: activityBody.trim(),
      });
      setActivitySubject('');
      setActivityBody('');
      onNotify('success', 'Interaction logged.');
      await refreshLead(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to log activity');
    } finally {
      setBusy(false);
    }
  };

  const sendEmail = async () => {
    if (!selected || !emailSubject.trim() || !emailBody.trim()) return;
    setBusy(true);
    try {
      await api.sendCrmEmail(selected.id, {
        subject: emailSubject.trim(),
        body: emailBody.trim(),
      });
      setEmailSubject('');
      setEmailBody('');
      onNotify('success', `Email sent to ${selected.email}.`);
      await refreshLead(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Email could not be sent');
    } finally {
      setBusy(false);
    }
  };

  const addTask = async () => {
    if (!selected || !taskTitle.trim() || !taskDue) return;
    setBusy(true);
    try {
      await api.createCrmTask(selected.id, {
        title: taskTitle.trim(),
        due_at: localInputToIso(taskDue),
      });
      setTaskTitle('');
      setTaskDue('');
      onNotify('success', 'Follow-up reminder created.');
      await refreshLead(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to create reminder');
    } finally {
      setBusy(false);
    }
  };

  const toggleTask = async (task: CrmTask) => {
    if (!selected) return;
    setBusy(true);
    try {
      const next: CrmTaskStatus = task.status === 'done' ? 'open' : 'done';
      await api.updateCrmTask(task.id, { status: next });
      await refreshLead(selected.id);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update reminder');
    } finally {
      setBusy(false);
    }
  };

  const deleteLead = async () => {
    if (!selected) return;
    if (!window.confirm(`Delete lead ${selected.name}? This removes its activity history.`)) return;
    setBusy(true);
    try {
      await api.deleteCrmLead(selected.id);
      setSelected(null);
      onNotify('success', 'Lead deleted.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to delete lead');
    } finally {
      setBusy(false);
    }
  };

  const visibleLeads = useMemo(() => (
    stageFilter === 'all' ? leads : leads.filter((lead) => lead.stage === stageFilter)
  ), [leads, stageFilter]);

  const topSource = useMemo(() => {
    const counts = leads.reduce<Record<string, number>>((acc, lead) => {
      acc[lead.source] = (acc[lead.source] || 0) + 1;
      return acc;
    }, {});
    return Object.entries(counts).sort((a, b) => b[1] - a[1])[0]?.[0] || '—';
  }, [leads]);

  const byStage = useMemo(() => {
    const map = new Map<CrmStage, CrmLead[]>();
    for (const stage of STAGES) map.set(stage.id, []);
    for (const lead of visibleLeads) {
      const bucket = map.get(lead.stage);
      if (bucket) bucket.push(lead);
    }
    return map;
  }, [visibleLeads]);

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900">
            <Users className="h-6 w-6 text-slate-500" /> CRM & leads
          </h2>
          <p className="mt-1 max-w-3xl text-sm text-slate-500">
            Partner applications become scored leads automatically. Move contacts through the
            pipeline, log every interaction, email from your configured SMTP transport, and keep
            follow-up reminders visible.
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
            onClick={() => setNewOpen(true)}
            className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700"
          >
            <Plus className="h-3.5 w-3.5" /> New lead
          </button>
        </div>
      </div>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-5">
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Total leads</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-slate-900">{summary.total_leads}</div>
          <div className="text-xs text-slate-500">{summary.open_leads} open</div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Open pipeline</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-blue-700">{formatMoney(summary.pipeline_value_cents)}</div>
          <div className="text-xs text-slate-500">Value across open leads</div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Won</div>
          <div className="mt-1 text-2xl font-bold tabular-nums text-emerald-700">{formatMoney(summary.won_value_cents)}</div>
          <div className="text-xs text-slate-500">{summary.won_leads} closed-won</div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Follow-ups due</div>
          <div className={`mt-1 text-2xl font-bold tabular-nums ${summary.overdue_tasks > 0 ? 'text-rose-600' : 'text-slate-900'}`}>
            {summary.overdue_tasks}
          </div>
          <div className="text-xs text-slate-500">Overdue or due now</div>
        </div>
        <div className="rounded-xl border border-slate-200 bg-white p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Sources</div>
          <div className="mt-1 truncate text-sm font-semibold capitalize text-slate-800">
            {topSource.replace(/_/g, ' ')}
          </div>
          <div className="text-xs text-slate-500">Top acquisition source</div>
        </div>
      </div>

      <div className="flex flex-col gap-3 rounded-xl border border-slate-200 bg-white p-4 sm:flex-row sm:items-center">
        <div className="relative min-w-0 flex-1">
          <Search className="pointer-events-none absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-400" />
          <input
            type="search"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            placeholder="Search name, email, company, or source"
            className="w-full rounded-lg border border-slate-200 py-2 pl-9 pr-3 text-sm text-slate-800 placeholder:text-slate-400 focus:border-blue-500 focus:outline-none"
          />
        </div>
        <select
          value={stageFilter}
          onChange={(event) => setStageFilter(event.target.value as 'all' | CrmStage)}
          className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none"
        >
          <option value="all">All stages</option>
          {STAGES.map((stage) => <option key={stage.id} value={stage.id}>{stage.label}</option>)}
        </select>
      </div>

      <div className="overflow-x-auto pb-2">
        <div className="flex min-w-max gap-3">
          {STAGES.map((stage) => {
            const stageLeads = byStage.get(stage.id) || [];
            const value = pipeline.find((row) => row.stage === stage.id)?.value_cents || 0;
            return (
              <div key={stage.id} className={`w-64 shrink-0 rounded-xl border p-3 ${stage.tone}`}>
                <div className="flex items-center justify-between">
                  <div className="text-xs font-bold uppercase tracking-wider text-slate-600">{stage.label}</div>
                  <span className="rounded-full bg-white/80 px-2 py-0.5 text-[10px] font-semibold text-slate-500">
                    {stageLeads.length}
                  </span>
                </div>
                <div className="mt-0.5 text-[11px] text-slate-500">{formatMoney(value)}</div>
                <div className="mt-3 space-y-2">
                  {stageLeads.map((lead) => (
                    <button
                      key={lead.id}
                      type="button"
                      onClick={() => openLead(lead)}
                      className="w-full rounded-lg border border-white bg-white p-3 text-left shadow-sm transition-shadow hover:shadow-md"
                    >
                      <div className="flex items-start justify-between gap-2">
                        <div className="min-w-0">
                          <div className="truncate text-sm font-semibold text-slate-900">{lead.name}</div>
                          <div className="truncate text-[11px] text-slate-500">{lead.company || lead.email}</div>
                        </div>
                        <span className={`shrink-0 rounded-full border px-1.5 py-0.5 text-[10px] font-bold ${scoreTone(lead.score)}`}>
                          {lead.score}
                        </span>
                      </div>
                      <div className="mt-2 flex items-center justify-between text-[11px] text-slate-500">
                        <span className="truncate">{lead.source.replace(/_/g, ' ')}</span>
                        <span className="font-semibold text-slate-700">{formatMoney(lead.value_cents, lead.currency)}</span>
                      </div>
                      {lead.next_follow_up_at && (
                        <div className={`mt-1 text-[10px] ${isOverdue(lead.next_follow_up_at) ? 'font-semibold text-rose-600' : 'text-slate-400'}`}>
                          Follow up {formatDate(lead.next_follow_up_at)}
                        </div>
                      )}
                    </button>
                  ))}
                  {stageLeads.length === 0 && (
                    <div className="rounded-lg border border-dashed border-slate-200 p-3 text-center text-[11px] text-slate-400">
                      No leads
                    </div>
                  )}
                </div>
              </div>
            );
          })}
        </div>
      </div>

      <Modal isOpen={newOpen} onClose={() => setNewOpen(false)} title="New lead" maxWidth="max-w-2xl">
        <form onSubmit={handleCreate} className="space-y-3">
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Name</label>
              <input
                value={newLead.name}
                onChange={(event) => setNewLead((current) => ({ ...current, name: event.target.value }))}
                required
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Email</label>
              <input
                type="email"
                value={newLead.email}
                onChange={(event) => setNewLead((current) => ({ ...current, email: event.target.value }))}
                required
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Company</label>
              <input
                value={newLead.company}
                onChange={(event) => setNewLead((current) => ({ ...current, company: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Website</label>
              <input
                type="url"
                value={newLead.website}
                onChange={(event) => setNewLead((current) => ({ ...current, website: event.target.value }))}
                placeholder="https://example.com"
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Phone</label>
              <input
                value={newLead.phone}
                onChange={(event) => setNewLead((current) => ({ ...current, phone: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Source</label>
              <input
                value={newLead.source}
                onChange={(event) => setNewLead((current) => ({ ...current, source: event.target.value }))}
                placeholder="referral, manual, event"
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Potential value (USD)</label>
              <input
                type="number"
                min={0}
                step="0.01"
                value={newLead.value}
                onChange={(event) => setNewLead((current) => ({ ...current, value: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700">Follow-up</label>
              <input
                type="datetime-local"
                value={newLead.next_follow_up}
                onChange={(event) => setNewLead((current) => ({ ...current, next_follow_up: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div className="sm:col-span-2">
              <label className="mb-1 block text-xs font-semibold text-slate-700">Tags (comma separated)</label>
              <input
                value={newLead.tags}
                onChange={(event) => setNewLead((current) => ({ ...current, tags: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div className="sm:col-span-2">
              <label className="mb-1 block text-xs font-semibold text-slate-700">Notes</label>
              <textarea
                rows={3}
                value={newLead.notes}
                onChange={(event) => setNewLead((current) => ({ ...current, notes: event.target.value }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
              />
            </div>
          </div>
          <div className="flex justify-end gap-2 pt-2">
            <button type="button" onClick={() => setNewOpen(false)} className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50">
              Cancel
            </button>
            <button type="submit" disabled={busy} className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60">
              {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Plus className="h-3.5 w-3.5" />}
              Create lead
            </button>
          </div>
        </form>
      </Modal>

      <Modal
        isOpen={!!selected}
        onClose={() => setSelected(null)}
        title={selected ? `${selected.name} · ${STAGE_LABELS.get(selected.stage) || selected.stage}` : 'Lead'}
        maxWidth="max-w-5xl"
      >
        {selected && (
          <div className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_20rem]">
            <div className="space-y-5">
              <div className="flex flex-wrap items-start justify-between gap-3">
                <div>
                  <div className="flex items-center gap-2">
                    <span className={`rounded-full border px-2 py-0.5 text-xs font-bold ${scoreTone(selected.score)}`}>
                      Score {selected.score}
                    </span>
                    <span className="text-xs text-slate-500">{selected.source.replace(/_/g, ' ')}</span>
                  </div>
                  <div className="mt-2 flex flex-wrap gap-x-4 gap-y-1 text-xs text-slate-600">
                    <a href={`mailto:${selected.email}`} className="inline-flex items-center gap-1 text-blue-700 hover:underline">
                      <Mail className="h-3 w-3" /> {selected.email}
                    </a>
                    {selected.phone && <span className="inline-flex items-center gap-1"><UserCircle className="h-3 w-3" /> {selected.phone}</span>}
                    {selected.company && <span className="inline-flex items-center gap-1"><Building2 className="h-3 w-3" /> {selected.company}</span>}
                    {selected.website && (
                      <a href={selected.website} target="_blank" rel="noreferrer noopener" className="inline-flex items-center gap-1 text-blue-700 hover:underline">
                        <Globe className="h-3 w-3" /> {selected.website.replace(/^https?:\/\//, '')}
                      </a>
                    )}
                  </div>
                  {(selected.utm_source || selected.utm_campaign || selected.referrer) && (
                    <div className="mt-2 rounded-lg bg-slate-50 p-2 text-[11px] text-slate-500">
                      Attribution: {selected.utm_source || 'direct'}/{selected.utm_medium || 'none'} ·{' '}
                      {selected.utm_campaign || 'no campaign'}
                      {selected.referrer ? ` · ${selected.referrer}` : ''}
                    </div>
                  )}
                </div>
                <button
                  type="button"
                  onClick={deleteLead}
                  disabled={busy}
                  className="rounded-lg border border-rose-200 bg-rose-50 p-2 text-rose-600 hover:bg-rose-100 disabled:opacity-50"
                  title="Delete lead"
                >
                  <Trash2 className="h-4 w-4" />
                </button>
              </div>

              <div className="rounded-xl border border-slate-200 p-4">
                <h3 className="text-xs font-bold uppercase tracking-wider text-slate-500">Pipeline & ownership</h3>
                <div className="mt-3 grid gap-3 sm:grid-cols-2">
                  <div>
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Stage</label>
                    <select
                      value={editStage}
                      onChange={(event) => setEditStage(event.target.value as CrmStage)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    >
                      {STAGES.map((stage) => <option key={stage.id} value={stage.id}>{stage.label}</option>)}
                    </select>
                  </div>
                  <div>
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Owner</label>
                    <select
                      value={editOwner}
                      onChange={(event) => setEditOwner(event.target.value)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    >
                      <option value="">Unassigned</option>
                      {ownerOptions.map((owner) => <option key={owner.id} value={owner.id}>{owner.email}</option>)}
                    </select>
                  </div>
                  <div>
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Value (USD)</label>
                    <input
                      type="number"
                      min={0}
                      step="0.01"
                      value={editValue}
                      onChange={(event) => setEditValue(event.target.value)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    />
                  </div>
                  <div>
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Next follow-up</label>
                    <input
                      type="datetime-local"
                      value={editFollowUp}
                      onChange={(event) => setEditFollowUp(event.target.value)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    />
                  </div>
                  <div className="sm:col-span-2">
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Tags</label>
                    <input
                      value={editTags}
                      onChange={(event) => setEditTags(event.target.value)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    />
                  </div>
                  <div className="sm:col-span-2">
                    <label className="mb-1 block text-[11px] font-semibold text-slate-600">Notes</label>
                    <textarea
                      rows={3}
                      value={editNotes}
                      onChange={(event) => setEditNotes(event.target.value)}
                      className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm focus:border-blue-500 focus:outline-none"
                    />
                  </div>
                </div>
                <div className="mt-3 flex justify-end">
                  <button
                    type="button"
                    onClick={saveLead}
                    disabled={busy}
                    className="inline-flex items-center gap-1.5 rounded-lg bg-slate-900 px-3 py-2 text-xs font-semibold text-white hover:bg-slate-800 disabled:opacity-60"
                  >
                    {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Save className="h-3.5 w-3.5" />}
                    Save lead
                  </button>
                </div>
              </div>

              <div className="rounded-xl border border-slate-200 p-4">
                <h3 className="flex items-center gap-2 text-xs font-bold uppercase tracking-wider text-slate-500">
                  <MessageSquare className="h-3.5 w-3.5" /> Interaction history
                </h3>
                <div className="mt-3 space-y-3">
                  {activities.map((activity) => (
                    <div key={activity.id} className="rounded-lg border border-slate-100 bg-slate-50/60 p-3">
                      <div className="flex items-center justify-between gap-2">
                        <div className="text-xs font-semibold text-slate-800">
                          {activity.subject || activity.type.replace(/_/g, ' ')}
                        </div>
                        <span className="text-[10px] text-slate-400">{formatDate(activity.occurred_at)}</span>
                      </div>
                      {activity.body && <p className="mt-1 whitespace-pre-wrap text-[11px] leading-5 text-slate-600">{activity.body}</p>}
                      <div className="mt-1 text-[10px] text-slate-400">
                        {activity.type.replace(/_/g, ' ')} · {activity.created_by || 'system'}
                      </div>
                    </div>
                  ))}
                  {activities.length === 0 && <p className="text-sm text-slate-500">No interactions logged yet.</p>}
                </div>
                <div className="mt-4 grid gap-2 border-t border-slate-100 pt-3 sm:grid-cols-[7rem_minmax(0,1fr)]">
                  <select
                    value={activityType}
                    onChange={(event) => setActivityType(event.target.value as 'note' | 'call' | 'meeting')}
                    className="rounded-lg border border-slate-200 px-2 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  >
                    <option value="note">Note</option>
                    <option value="call">Call</option>
                    <option value="meeting">Meeting</option>
                  </select>
                  <input
                    value={activitySubject}
                    onChange={(event) => setActivitySubject(event.target.value)}
                    placeholder="Subject"
                    className="rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  />
                  <textarea
                    rows={2}
                    value={activityBody}
                    onChange={(event) => setActivityBody(event.target.value)}
                    placeholder="What happened?"
                    className="rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none sm:col-span-2"
                  />
                  <div className="sm:col-span-2 flex justify-end">
                    <button
                      type="button"
                      onClick={addActivity}
                      disabled={busy || (!activitySubject.trim() && !activityBody.trim())}
                      className="inline-flex items-center gap-1.5 rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50 disabled:opacity-50"
                    >
                      <Plus className="h-3.5 w-3.5" /> Log interaction
                    </button>
                  </div>
                </div>
              </div>
            </div>

            <div className="space-y-4">
              <div className="rounded-xl border border-slate-200 p-4">
                <h3 className="flex items-center gap-2 text-xs font-bold uppercase tracking-wider text-slate-500">
                  <CalendarClock className="h-3.5 w-3.5" /> Follow-up reminders
                </h3>
                <div className="mt-3 space-y-2">
                  {tasks.map((task) => (
                    <div key={task.id} className={`rounded-lg border p-2 ${task.status === 'done' ? 'border-slate-100 bg-slate-50 opacity-70' : isOverdue(task.due_at) ? 'border-rose-200 bg-rose-50' : 'border-slate-200'}`}>
                      <div className="flex items-start gap-2">
                        <button
                          type="button"
                          onClick={() => toggleTask(task)}
                          className="mt-0.5 text-slate-400 hover:text-emerald-600"
                          aria-label={task.status === 'done' ? 'Reopen reminder' : 'Complete reminder'}
                        >
                          <CheckCircle2 className={`h-4 w-4 ${task.status === 'done' ? 'text-emerald-600' : ''}`} />
                        </button>
                        <div className="min-w-0">
                          <div className={`text-xs font-medium ${task.status === 'done' ? 'line-through text-slate-500' : 'text-slate-800'}`}>
                            {task.title}
                          </div>
                          <div className={`text-[10px] ${isOverdue(task.due_at) && task.status === 'open' ? 'font-semibold text-rose-600' : 'text-slate-400'}`}>
                            {formatDate(task.due_at)}
                          </div>
                        </div>
                      </div>
                    </div>
                  ))}
                  {tasks.length === 0 && <p className="text-xs text-slate-500">No reminders.</p>}
                </div>
                <div className="mt-3 space-y-2 border-t border-slate-100 pt-3">
                  <input
                    value={taskTitle}
                    onChange={(event) => setTaskTitle(event.target.value)}
                    placeholder="Follow-up title"
                    className="w-full rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  />
                  <input
                    type="datetime-local"
                    value={taskDue}
                    onChange={(event) => setTaskDue(event.target.value)}
                    className="w-full rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  />
                  <button
                    type="button"
                    onClick={addTask}
                    disabled={busy || !taskTitle.trim() || !taskDue}
                    className="w-full rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50 disabled:opacity-50"
                  >
                    Add reminder
                  </button>
                </div>
              </div>

              <div className="rounded-xl border border-slate-200 p-4">
                <h3 className="flex items-center gap-2 text-xs font-bold uppercase tracking-wider text-slate-500">
                  <Send className="h-3.5 w-3.5" /> Email lead
                </h3>
                <div className="mt-3 space-y-2">
                  <input
                    value={emailSubject}
                    onChange={(event) => setEmailSubject(event.target.value)}
                    placeholder="Subject"
                    className="w-full rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  />
                  <textarea
                    rows={5}
                    value={emailBody}
                    onChange={(event) => setEmailBody(event.target.value)}
                    placeholder={`Hi ${selected.name.split(' ')[0] || 'there'},`}
                    className="w-full rounded-lg border border-slate-200 px-3 py-2 text-xs focus:border-blue-500 focus:outline-none"
                  />
                  <button
                    type="button"
                    onClick={sendEmail}
                    disabled={busy || !emailSubject.trim() || !emailBody.trim()}
                    className="inline-flex w-full items-center justify-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-50"
                  >
                    {busy ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Send className="h-3.5 w-3.5" />}
                    Send via SMTP
                  </button>
                  <p className="text-[10px] leading-4 text-slate-400">
                    Uses the administrator SMTP settings. The email is logged in the interaction history.
                  </p>
                </div>
              </div>

              <div className="rounded-xl bg-slate-50 p-3 text-[11px] text-slate-500">
                <div className="flex items-center justify-between">
                  <span>Created</span>
                  <span>{formatDate(selected.created_at)}</span>
                </div>
                <div className="mt-1 flex items-center justify-between">
                  <span>Last contact</span>
                  <span>{formatDate(selected.last_contact_at)}</span>
                </div>
                <div className="mt-1 flex items-center justify-between">
                  <span>Converted</span>
                  <span>{formatDate(selected.converted_at)}</span>
                </div>
              </div>
            </div>
          </div>
        )}
        {detailLoading && (
          <div className="flex items-center gap-2 text-xs text-slate-500">
            <Loader2 className="h-3.5 w-3.5 animate-spin" /> Refreshing lead…
          </div>
        )}
      </Modal>
    </div>
  );
};
