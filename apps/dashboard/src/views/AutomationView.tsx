import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  AlertTriangle,
  ArrowDown,
  ArrowUp,
  Bell,
  Clock,
  Edit,
  GitBranch,
  Loader2,
  Pause,
  Play,
  Plus,
  RefreshCw,
  Save,
  Trash2,
  Zap,
} from 'lucide-react';
import { api } from '../api';
import {
  AppNotification,
  AutomationRun,
  AutomationStep,
  AutomationStepType,
  AutomationTriggerType,
  AutomationWorkflow,
  AutomationWorkflowStatus,
  Campaign,
  Site,
} from '../types';
import { Modal } from '../components/Modal';

interface AutomationViewProps {
  role: string;
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

interface DraftStep {
  type: AutomationStepType;
  config: Record<string, any>;
}

interface WorkflowDraft {
  id?: string;
  name: string;
  description: string;
  site_id: string;
  status: AutomationWorkflowStatus;
  trigger_type: AutomationTriggerType;
  trigger_config: Record<string, any>;
  steps: DraftStep[];
}

const STATUS_STYLES: Record<AutomationWorkflowStatus, string> = {
  active: 'border-emerald-200 bg-emerald-50 text-emerald-700',
  paused: 'border-amber-200 bg-amber-50 text-amber-700',
  draft: 'border-slate-200 bg-slate-100 text-slate-600',
};

const RUN_STYLES: Record<string, string> = {
  queued: 'border-slate-200 bg-slate-100 text-slate-600',
  running: 'border-blue-200 bg-blue-50 text-blue-700',
  waiting: 'border-amber-200 bg-amber-50 text-amber-700',
  completed: 'border-emerald-200 bg-emerald-50 text-emerald-700',
  failed: 'border-rose-200 bg-rose-50 text-rose-700',
  skipped: 'border-slate-200 bg-slate-100 text-slate-500',
};

const TRIGGER_LABELS: Record<AutomationTriggerType, string> = {
  schedule: 'Schedule',
  performance: 'Performance threshold',
  conversion: 'New conversion',
  inquiry: 'New partner inquiry',
  manual: 'Manual',
};

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function toDateTimeInput(value?: string): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '';
  const offset = date.getTimezoneOffset();
  return new Date(date.getTime() - offset * 60_000).toISOString().slice(0, 16);
}

function localInputToIso(value: string): string {
  if (!value) return '';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? '' : date.toISOString();
}

function defaultTriggerConfig(type: AutomationTriggerType, siteId: string): Record<string, any> {
  if (type === 'schedule') {
    const date = new Date(Date.now() + 60 * 60 * 1000);
    return { run_at: date.toISOString(), recurrence: 'none' };
  }
  if (type === 'performance') {
    return {
      metric: 'ctr',
      comparator: 'lt',
      threshold: 1,
      lookback_hours: 24,
      cooldown_minutes: 60,
      min_impressions: 100,
      site_id: siteId,
    };
  }
  if (type === 'conversion') return {};
  if (type === 'inquiry') return { kind: '' };
  return {};
}

function defaultSteps(type: AutomationTriggerType): DraftStep[] {
  if (type === 'performance') {
    return [{ type: 'action', config: { action: 'pause_campaign', campaign_id: '' } }];
  }
  if (type === 'schedule') {
    return [{ type: 'action', config: { action: 'activate_campaign', campaign_id: '' } }];
  }
  if (type === 'conversion' || type === 'inquiry') {
    return [{
      type: 'action',
      config: {
        action: 'send_notification',
        kind: 'info',
        title: `${TRIGGER_LABELS[type]} event`,
        body: 'A configured automation trigger fired.',
      },
    }];
  }
  return [{ type: 'action', config: { action: 'send_notification', title: 'Manual automation', body: 'Manual run completed.' } }];
}

function triggerSummary(workflow: AutomationWorkflow): string {
  const config = workflow.trigger_config || {};
  if (workflow.trigger_type === 'schedule') {
    return `${formatDate(config.run_at)}${config.recurrence && config.recurrence !== 'none' ? ` · ${config.recurrence}` : ''}`;
  }
  if (workflow.trigger_type === 'performance') {
    return `${config.metric || 'ctr'} ${config.comparator || 'lt'} ${config.threshold ?? 0} over ${config.lookback_hours || 24}h`;
  }
  if (workflow.trigger_type === 'conversion') {
    return config.event_name ? `event: ${config.event_name}` : 'any conversion';
  }
  if (workflow.trigger_type === 'inquiry') {
    return config.kind ? `${config.kind} inquiries` : 'any partner inquiry';
  }
  return 'manual test run';
}

function stepLabel(step: AutomationStep | DraftStep): string {
  if (step.type === 'condition') {
    return `Condition: ${step.config.metric || 'ctr'} ${step.config.comparator || 'lt'} ${step.config.threshold ?? 0}`;
  }
  if (step.type === 'delay') {
    return `Delay ${step.config.amount || 1} ${step.config.unit || 'minutes'}`;
  }
  const action = step.config.action || 'action';
  if (action === 'pause_campaign') return 'Pause campaign';
  if (action === 'activate_campaign') return 'Activate campaign';
  if (action === 'send_notification') return 'Send in-app notification';
  if (action === 'webhook') return 'Call webhook';
  return action;
}

function emptyDraft(siteId: string): WorkflowDraft {
  const trigger_type: AutomationTriggerType = 'performance';
  return {
    name: '',
    description: '',
    site_id: siteId,
    status: 'draft',
    trigger_type,
    trigger_config: defaultTriggerConfig(trigger_type, siteId),
    steps: defaultSteps(trigger_type),
  };
}

export const AutomationView: React.FC<AutomationViewProps> = ({ role, onNotify }) => {
  const isAdmin = role === 'admin';
  const [sites, setSites] = useState<Site[]>([]);
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [workflows, setWorkflows] = useState<AutomationWorkflow[]>([]);
  const [runs, setRuns] = useState<AutomationRun[]>([]);
  const [notifications, setNotifications] = useState<AppNotification[]>([]);
  const [unread, setUnread] = useState(0);
  const [loading, setLoading] = useState(true);
  const [busyId, setBusyId] = useState('');
  const [expandedRun, setExpandedRun] = useState('');
  const [builderOpen, setBuilderOpen] = useState(false);
  const [draft, setDraft] = useState<WorkflowDraft>(emptyDraft(''));
  const [saving, setSaving] = useState(false);

  const siteNames = useMemo(
    () => new Map(sites.map((site) => [site.id, site.name])),
    [sites],
  );
  const visibleCampaigns = useMemo(
    () => campaigns.filter((campaign) => !draft.site_id || campaign.site_id === draft.site_id),
    [campaigns, draft.site_id],
  );

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const [workflowRows, runRows, notificationData] = await Promise.all([
        api.getAutomationWorkflows(),
        api.getAutomationRuns({ limit: 50 }),
        api.getAutomationNotifications(),
      ]);
      setWorkflows(workflowRows);
      setRuns(runRows);
      setNotifications(notificationData.notifications);
      setUnread(notificationData.unread);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to load automation data');
    } finally {
      setLoading(false);
    }
  }, [onNotify]);

  useEffect(() => {
    let cancelled = false;
    Promise.all([api.getSites(), api.getCampaigns()])
      .then(([siteRows, campaignRows]) => {
        if (cancelled) return;
        setSites(siteRows);
        setCampaigns(campaignRows);
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
    const timer = window.setInterval(load, 30_000);
    return () => window.clearInterval(timer);
  }, [load]);

  const openCreate = () => {
    const siteId = sites[0]?.id || '';
    setDraft(emptyDraft(siteId));
    setBuilderOpen(true);
  };

  const openEdit = (workflow: AutomationWorkflow) => {
    const triggerConfig = { ...(workflow.trigger_config || {}) };
    if (workflow.trigger_type === 'schedule' && triggerConfig.run_at) {
      triggerConfig.run_at_input = toDateTimeInput(String(triggerConfig.run_at));
    }
    setDraft({
      id: workflow.id,
      name: workflow.name,
      description: workflow.description,
      site_id: workflow.site_id,
      status: workflow.status,
      trigger_type: workflow.trigger_type,
      trigger_config: triggerConfig,
      steps: workflow.steps.map((step) => ({ type: step.type, config: { ...(step.config || {}) } })),
    });
    setBuilderOpen(true);
  };

  const setTriggerType = (type: AutomationTriggerType) => {
    setDraft((current) => ({
      ...current,
      trigger_type: type,
      trigger_config: defaultTriggerConfig(type, current.site_id),
      steps: defaultSteps(type),
    }));
  };

  const setStep = (index: number, patch: Partial<DraftStep>) => {
    setDraft((current) => ({
      ...current,
      steps: current.steps.map((step, row) => (row === index ? { ...step, ...patch } : step)),
    }));
  };

  const setStepConfig = (index: number, patch: Record<string, any>) => {
    setDraft((current) => ({
      ...current,
      steps: current.steps.map((step, row) => (
        row === index ? { ...step, config: { ...step.config, ...patch } } : step
      )),
    }));
  };

  const addStep = (type: AutomationStepType) => {
    const config = type === 'condition'
      ? { metric: 'ctr', comparator: 'lt', threshold: 1, lookback_hours: 24, min_impressions: 100 }
      : type === 'delay'
        ? { amount: 1, unit: 'hours' }
        : { action: 'send_notification', kind: 'info', title: 'Automation alert', body: '' };
    setDraft((current) => ({ ...current, steps: [...current.steps, { type, config }] }));
  };

  const moveStep = (index: number, direction: -1 | 1) => {
    setDraft((current) => {
      const next = [...current.steps];
      const target = index + direction;
      if (target < 0 || target >= next.length) return current;
      [next[index], next[target]] = [next[target], next[index]];
      return { ...current, steps: next };
    });
  };

  const removeStep = (index: number) => {
    setDraft((current) => ({ ...current, steps: current.steps.filter((_, row) => row !== index) }));
  };

  const saveWorkflow = async () => {
    if (saving) return;
    if (draft.name.trim().length < 2) {
      onNotify('error', 'Give the workflow a name of at least 2 characters.');
      return;
    }
    if (!isAdmin && !draft.site_id) {
      onNotify('error', 'Choose a site for this workflow.');
      return;
    }
    if (draft.trigger_type === 'performance' && !draft.site_id &&
        !draft.trigger_config.campaign_id) {
      onNotify('error', 'Performance automations need a site or campaign scope.');
      return;
    }
    const trigger_config = { ...draft.trigger_config };
    if (draft.trigger_type === 'schedule') {
      const iso = draft.trigger_config.run_at_input
        ? localInputToIso(String(draft.trigger_config.run_at_input))
        : String(draft.trigger_config.run_at || '');
      if (!iso) {
        onNotify('error', 'Choose a valid run time for the schedule.');
        return;
      }
      trigger_config.run_at = iso;
      delete trigger_config.run_at_input;
    }
    if (draft.trigger_type === 'performance' && !trigger_config.site_id && draft.site_id) {
      trigger_config.site_id = draft.site_id;
    }
    const payload = {
      name: draft.name.trim(),
      description: draft.description.trim(),
      site_id: draft.site_id,
      status: draft.status,
      trigger_type: draft.trigger_type,
      trigger_config,
      steps: draft.steps.map((step) => ({ type: step.type, config: step.config })),
    };
    setSaving(true);
    try {
      if (draft.id) {
        await api.updateAutomationWorkflow(draft.id, payload);
        onNotify('success', 'Workflow updated.');
      } else {
        await api.createAutomationWorkflow(payload);
        onNotify('success', 'Workflow created.');
      }
      setBuilderOpen(false);
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to save workflow');
    } finally {
      setSaving(false);
    }
  };

  const toggleStatus = async (workflow: AutomationWorkflow) => {
    const next: AutomationWorkflowStatus = workflow.status === 'active' ? 'paused' : 'active';
    setBusyId(workflow.id);
    try {
      await api.setAutomationWorkflowStatus(workflow.id, next);
      setWorkflows((current) => current.map((row) => (
        row.id === workflow.id ? { ...row, status: next } : row
      )));
      onNotify('success', `Workflow ${next === 'active' ? 'activated' : 'paused'}.`);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update workflow');
    } finally {
      setBusyId('');
    }
  };

  const runNow = async (workflow: AutomationWorkflow) => {
    setBusyId(workflow.id);
    try {
      await api.runAutomationWorkflow(workflow.id, { source: 'manual-test' });
      onNotify('success', 'Workflow queued. Runs are processed within about 30 seconds.');
      await load();
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to run workflow');
    } finally {
      setBusyId('');
    }
  };

  const removeWorkflow = async (workflow: AutomationWorkflow) => {
    if (!window.confirm(`Delete workflow "${workflow.name}"? Run history will also be removed.`)) return;
    setBusyId(workflow.id);
    try {
      await api.deleteAutomationWorkflow(workflow.id);
      setWorkflows((current) => current.filter((row) => row.id !== workflow.id));
      onNotify('success', 'Workflow deleted.');
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to delete workflow');
    } finally {
      setBusyId('');
    }
  };

  const markRead = async (notification: AppNotification) => {
    try {
      await api.markAutomationNotificationRead(notification.id);
      setNotifications((current) => current.map((row) => (
        row.id === notification.id ? { ...row, read_at: new Date().toISOString() } : row
      )));
      setUnread((count) => Math.max(0, count - (notification.read_at ? 0 : 1)));
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to mark notification read');
    }
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900">
            <Zap className="h-6 w-6 text-slate-500" /> Marketing automation
          </h2>
          <p className="mt-1 max-w-3xl text-sm text-slate-500">
            Build ordered workflows from triggers, conditions, delays, and actions. Scheduled
            activation, performance-based pausing, notifications, and signed-safe outbound webhooks
            run on the API worker about every 30 seconds.
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
            onClick={openCreate}
            className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700"
          >
            <Plus className="h-3.5 w-3.5" /> New workflow
          </button>
        </div>
      </div>

      <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_22rem]">
        <div className="flex flex-col gap-4">
          {workflows.map((workflow) => (
            <div key={workflow.id} className="rounded-xl border border-slate-200 bg-white p-5 shadow-sm">
              <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                <div className="min-w-0">
                  <div className="flex flex-wrap items-center gap-2">
                    <h3 className="text-base font-bold text-slate-900">{workflow.name}</h3>
                    <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold uppercase ${STATUS_STYLES[workflow.status]}`}>
                      {workflow.status}
                    </span>
                    <span className="rounded-full border border-slate-200 bg-slate-50 px-2 py-0.5 text-[10px] font-semibold text-slate-600">
                      {TRIGGER_LABELS[workflow.trigger_type]}
                    </span>
                  </div>
                  <p className="mt-1 text-xs text-slate-500">
                    {siteNames.get(workflow.site_id) || (workflow.site_id ? workflow.site_id : 'All sites')}
                    {' · '}{triggerSummary(workflow)}
                  </p>
                  {workflow.description && (
                    <p className="mt-2 max-w-3xl text-sm text-slate-600">{workflow.description}</p>
                  )}
                  <div className="mt-3 flex flex-wrap items-center gap-2">
                    {workflow.steps.map((step, index) => (
                      <React.Fragment key={step.id || index}>
                        <span className={`inline-flex items-center gap-1 rounded-lg border px-2 py-1 text-[11px] font-medium ${
                          step.type === 'condition'
                            ? 'border-amber-200 bg-amber-50 text-amber-800'
                            : step.type === 'delay'
                              ? 'border-blue-200 bg-blue-50 text-blue-700'
                              : 'border-emerald-200 bg-emerald-50 text-emerald-700'
                        }`}>
                          {step.type === 'condition' ? <GitBranch className="h-3 w-3" /> : step.type === 'delay' ? <Clock className="h-3 w-3" /> : <Zap className="h-3 w-3" />}
                          {stepLabel(step)}
                        </span>
                        {index < workflow.steps.length - 1 && <span className="text-slate-300">→</span>}
                      </React.Fragment>
                    ))}
                    {workflow.steps.length === 0 && <span className="text-xs text-slate-400">No steps yet</span>}
                  </div>
                  <div className="mt-3 text-[11px] text-slate-400">
                    Last triggered: {formatDate(workflow.last_triggered_at)}
                    {workflow.next_run_at ? ` · Next: ${formatDate(workflow.next_run_at)}` : ''}
                  </div>
                </div>
                <div className="flex shrink-0 items-center gap-1">
                  <button
                    type="button"
                    onClick={() => toggleStatus(workflow)}
                    disabled={busyId === workflow.id}
                    className="rounded-lg border border-slate-200 bg-white p-2 text-slate-500 hover:bg-slate-50 disabled:opacity-50"
                    title={workflow.status === 'active' ? 'Pause workflow' : 'Activate workflow'}
                  >
                    {workflow.status === 'active' ? <Pause className="h-4 w-4" /> : <Play className="h-4 w-4" />}
                  </button>
                  <button
                    type="button"
                    onClick={() => runNow(workflow)}
                    disabled={busyId === workflow.id || workflow.status !== 'active'}
                    className="rounded-lg border border-slate-200 bg-white p-2 text-slate-500 hover:bg-slate-50 disabled:opacity-50"
                    title="Run now"
                  >
                    <Zap className="h-4 w-4" />
                  </button>
                  <button
                    type="button"
                    onClick={() => openEdit(workflow)}
                    className="rounded-lg border border-slate-200 bg-white p-2 text-slate-500 hover:bg-slate-50"
                    title="Edit workflow"
                  >
                    <Edit className="h-4 w-4" />
                  </button>
                  <button
                    type="button"
                    onClick={() => removeWorkflow(workflow)}
                    disabled={busyId === workflow.id}
                    className="rounded-lg border border-slate-200 bg-white p-2 text-slate-400 hover:bg-rose-50 hover:text-rose-600 disabled:opacity-50"
                    title="Delete workflow"
                  >
                    <Trash2 className="h-4 w-4" />
                  </button>
                </div>
              </div>
            </div>
          ))}
          {!loading && workflows.length === 0 && (
            <div className="rounded-xl border border-dashed border-slate-300 bg-white p-10 text-center">
              <Zap className="mx-auto h-8 w-8 text-slate-300" />
              <h3 className="mt-3 text-sm font-semibold text-slate-800">No automations yet</h3>
              <p className="mx-auto mt-1 max-w-md text-sm text-slate-500">
                Start with a performance threshold that pauses a weak campaign, or schedule a
                campaign activation and notify your team.
              </p>
              <button
                type="button"
                onClick={openCreate}
                className="mt-4 inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700"
              >
                <Plus className="h-3.5 w-3.5" /> Create workflow
              </button>
            </div>
          )}

          <div className="rounded-xl border border-slate-200 bg-white p-5">
            <h3 className="flex items-center gap-2 text-sm font-bold text-slate-800">
              <Play className="h-4 w-4 text-slate-400" /> Recent runs
            </h3>
            <div className="mt-3 divide-y divide-slate-100">
              {runs.slice(0, 12).map((run) => {
                const workflow = workflows.find((row) => row.id === run.workflow_id);
                const open = expandedRun === run.id;
                return (
                  <div key={run.id} className="py-3">
                    <button
                      type="button"
                      onClick={() => setExpandedRun(open ? '' : run.id)}
                      className="flex w-full items-start justify-between gap-3 text-left"
                    >
                      <div className="min-w-0">
                        <div className="flex flex-wrap items-center gap-2">
                          <span className="text-sm font-medium text-slate-800">
                            {workflow?.name || run.workflow_id}
                          </span>
                          <span className={`rounded-full border px-2 py-0.5 text-[10px] font-semibold ${RUN_STYLES[run.status] || RUN_STYLES.queued}`}>
                            {run.status}
                          </span>
                        </div>
                        <div className="mt-0.5 truncate text-[11px] text-slate-400">
                          {formatDate(run.created_at)} · step {run.current_step}
                          {run.error ? ` · ${run.error}` : ''}
                        </div>
                      </div>
                      <span className="shrink-0 text-[11px] text-slate-400">{open ? 'Hide log' : 'View log'}</span>
                    </button>
                    {open && (
                      <pre className="mt-2 max-h-56 overflow-auto rounded-lg bg-slate-900 p-3 text-[11px] leading-5 text-slate-100">
                        {run.log || 'No log entries yet.'}
                      </pre>
                    )}
                  </div>
                );
              })}
              {runs.length === 0 && <p className="py-4 text-sm text-slate-500">No runs yet.</p>}
            </div>
          </div>
        </div>

        <div className="flex flex-col gap-4">
          <div className="rounded-xl border border-slate-200 bg-white p-5">
            <div className="flex items-center justify-between">
              <h3 className="flex items-center gap-2 text-sm font-bold text-slate-800">
                <Bell className="h-4 w-4 text-slate-400" /> Notifications
              </h3>
              {unread > 0 && (
                <span className="rounded-full bg-rose-100 px-2 py-0.5 text-[10px] font-bold text-rose-700">
                  {unread} unread
                </span>
              )}
            </div>
            <div className="mt-3 space-y-2">
              {notifications.slice(0, 10).map((notification) => (
                <div
                  key={notification.id}
                  className={`rounded-lg border p-3 ${
                    notification.read_at
                      ? 'border-slate-100 bg-white'
                      : 'border-blue-100 bg-blue-50/50'
                  }`}
                >
                  <div className="flex items-start justify-between gap-2">
                    <div className="text-xs font-semibold text-slate-800">{notification.title}</div>
                    {!notification.read_at && (
                      <button
                        type="button"
                        onClick={() => markRead(notification)}
                        className="shrink-0 text-[10px] font-semibold text-blue-700 hover:text-blue-900"
                      >
                        Mark read
                      </button>
                    )}
                  </div>
                  {notification.body && (
                    <p className="mt-1 text-[11px] leading-5 text-slate-500">{notification.body}</p>
                  )}
                  <div className="mt-1 text-[10px] text-slate-400">{formatDate(notification.created_at)}</div>
                </div>
              ))}
              {notifications.length === 0 && (
                <p className="text-sm text-slate-500">No notifications yet. Automation actions and threshold alerts appear here.</p>
              )}
            </div>
          </div>

          <div className="rounded-xl border border-amber-200 bg-amber-50 p-4">
            <div className="flex items-start gap-2">
              <AlertTriangle className="mt-0.5 h-4 w-4 shrink-0 text-amber-700" aria-hidden="true" />
              <div className="text-xs leading-5 text-amber-900">
                Webhook actions only call public HTTPS endpoints. Private, loopback, link-local,
                and internal addresses are rejected by the worker, and redirects are not followed.
              </div>
            </div>
          </div>
        </div>
      </div>

      <Modal
        isOpen={builderOpen}
        onClose={() => setBuilderOpen(false)}
        title={draft.id ? 'Edit workflow' : 'New workflow'}
        maxWidth="max-w-4xl"
      >
        <div className="space-y-5">
          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-name">Name</label>
              <input
                id="automation-name"
                value={draft.name}
                onChange={(event) => setDraft((current) => ({ ...current, name: event.target.value }))}
                maxLength={120}
                placeholder="Pause underperforming campaigns"
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-site">Site</label>
              <select
                id="automation-site"
                value={draft.site_id}
                onChange={(event) => {
                  const site_id = event.target.value;
                  setDraft((current) => ({
                    ...current,
                    site_id,
                    trigger_config: current.trigger_type === 'performance'
                      ? { ...current.trigger_config, site_id }
                      : current.trigger_config,
                    steps: current.steps.map((step) => (
                      step.type === 'action' && (step.config.action === 'pause_campaign' || step.config.action === 'activate_campaign')
                        ? { ...step, config: { ...step.config, campaign_id: '' } }
                        : step
                    )),
                  }));
                }}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              >
                {isAdmin && <option value="">All sites</option>}
                {sites.map((site) => (
                  <option key={site.id} value={site.id}>{site.name}</option>
                ))}
              </select>
            </div>
            <div className="sm:col-span-2">
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-description">Description</label>
              <textarea
                id="automation-description"
                value={draft.description}
                onChange={(event) => setDraft((current) => ({ ...current, description: event.target.value }))}
                rows={2}
                maxLength={1000}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              />
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-trigger">Trigger</label>
              <select
                id="automation-trigger"
                value={draft.trigger_type}
                onChange={(event) => setTriggerType(event.target.value as AutomationTriggerType)}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              >
                <option value="performance">Performance threshold</option>
                <option value="schedule">Schedule</option>
                <option value="conversion">New conversion</option>
                <option value="inquiry">New partner inquiry</option>
                <option value="manual">Manual only</option>
              </select>
            </div>
            <div>
              <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-status">Status</label>
              <select
                id="automation-status"
                value={draft.status}
                onChange={(event) => setDraft((current) => ({ ...current, status: event.target.value as AutomationWorkflowStatus }))}
                className="w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
              >
                <option value="draft">Draft</option>
                <option value="active">Active</option>
                <option value="paused">Paused</option>
              </select>
            </div>
          </div>

          <div className="rounded-xl border border-slate-200 bg-slate-50/70 p-4">
            <div className="text-[11px] font-bold uppercase tracking-wider text-slate-500">Trigger configuration</div>
            {draft.trigger_type === 'schedule' && (
              <div className="mt-3 grid gap-3 sm:grid-cols-2">
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-run-at">Run at</label>
                  <input
                    id="automation-run-at"
                    type="datetime-local"
                    value={draft.trigger_config.run_at_input || toDateTimeInput(String(draft.trigger_config.run_at || ''))}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, run_at_input: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700" htmlFor="automation-recurrence">Repeat</label>
                  <select
                    id="automation-recurrence"
                    value={draft.trigger_config.recurrence || 'none'}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, recurrence: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  >
                    <option value="none">Once</option>
                    <option value="hourly">Hourly</option>
                    <option value="daily">Daily</option>
                    <option value="weekly">Weekly</option>
                  </select>
                </div>
              </div>
            )}
            {draft.trigger_type === 'performance' && (
              <div className="mt-3 grid gap-3 sm:grid-cols-3">
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Metric</label>
                  <select
                    value={draft.trigger_config.metric || 'ctr'}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, metric: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  >
                    <option value="ctr">CTR %</option>
                    <option value="conversion_rate">Conversion rate %</option>
                    <option value="impressions">Impressions</option>
                    <option value="clicks">Clicks</option>
                    <option value="conversions">Conversions</option>
                    <option value="commission_cents">Commission (cents)</option>
                  </select>
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Comparator</label>
                  <select
                    value={draft.trigger_config.comparator || 'lt'}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, comparator: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  >
                    <option value="lt">Below</option>
                    <option value="lte">At or below</option>
                    <option value="gt">Above</option>
                    <option value="gte">At or above</option>
                  </select>
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Threshold</label>
                  <input
                    type="number"
                    step="0.01"
                    value={draft.trigger_config.threshold ?? 1}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, threshold: Number(event.target.value) },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Lookback (hours)</label>
                  <input
                    type="number"
                    min={1}
                    max={720}
                    value={draft.trigger_config.lookback_hours ?? 24}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, lookback_hours: Number(event.target.value) },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Min impressions</label>
                  <input
                    type="number"
                    min={0}
                    value={draft.trigger_config.min_impressions ?? 100}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, min_impressions: Number(event.target.value) },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Cooldown (minutes)</label>
                  <input
                    type="number"
                    min={1}
                    value={draft.trigger_config.cooldown_minutes ?? 60}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, cooldown_minutes: Number(event.target.value) },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div className="sm:col-span-3">
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Campaign scope (optional)</label>
                  <select
                    value={draft.trigger_config.campaign_id || ''}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, campaign_id: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  >
                    <option value="">Whole site</option>
                    {visibleCampaigns.map((campaign) => (
                      <option key={campaign.id} value={campaign.id}>{campaign.name}</option>
                    ))}
                  </select>
                </div>
              </div>
            )}
            {draft.trigger_type === 'conversion' && (
              <div className="mt-3 grid gap-3 sm:grid-cols-2">
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Event name (optional)</label>
                  <input
                    value={draft.trigger_config.event_name || ''}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, event_name: event.target.value },
                    }))}
                    placeholder="purchase"
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Campaign filter (optional)</label>
                  <select
                    value={draft.trigger_config.campaign_id || ''}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, campaign_id: event.target.value },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  >
                    <option value="">Any campaign</option>
                    {visibleCampaigns.map((campaign) => (
                      <option key={campaign.id} value={campaign.id}>{campaign.name}</option>
                    ))}
                  </select>
                </div>
                <div>
                  <label className="mb-1 block text-xs font-semibold text-slate-700">Minimum value (cents)</label>
                  <input
                    type="number"
                    min={0}
                    value={draft.trigger_config.min_value_cents ?? 0}
                    onChange={(event) => setDraft((current) => ({
                      ...current,
                      trigger_config: { ...current.trigger_config, min_value_cents: Number(event.target.value) },
                    }))}
                    className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                  />
                </div>
              </div>
            )}
            {draft.trigger_type === 'inquiry' && (
              <div className="mt-3">
                <label className="mb-1 block text-xs font-semibold text-slate-700">Inquiry type</label>
                <select
                  value={draft.trigger_config.kind || ''}
                  onChange={(event) => setDraft((current) => ({
                    ...current,
                    trigger_config: { ...current.trigger_config, kind: event.target.value },
                  }))}
                  className="w-full rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm text-slate-800 focus:border-blue-500 focus:outline-none"
                >
                  <option value="">Any inquiry</option>
                  <option value="publisher">Publisher application</option>
                  <option value="client">Client application</option>
                </select>
              </div>
            )}
            {draft.trigger_type === 'manual' && (
              <p className="mt-2 text-xs text-slate-500">This workflow only runs when you press Run now.</p>
            )}
          </div>

          <div>
            <div className="flex flex-wrap items-center justify-between gap-2">
              <div className="text-[11px] font-bold uppercase tracking-wider text-slate-500">Workflow steps</div>
              <div className="flex flex-wrap gap-1">
                <button type="button" onClick={() => addStep('condition')} className="inline-flex items-center gap-1 rounded-md border border-slate-200 bg-white px-2 py-1 text-[11px] font-semibold text-slate-600 hover:bg-slate-50">
                  <GitBranch className="h-3 w-3" /> Condition
                </button>
                <button type="button" onClick={() => addStep('delay')} className="inline-flex items-center gap-1 rounded-md border border-slate-200 bg-white px-2 py-1 text-[11px] font-semibold text-slate-600 hover:bg-slate-50">
                  <Clock className="h-3 w-3" /> Delay
                </button>
                <button type="button" onClick={() => addStep('action')} className="inline-flex items-center gap-1 rounded-md border border-slate-200 bg-white px-2 py-1 text-[11px] font-semibold text-slate-600 hover:bg-slate-50">
                  <Zap className="h-3 w-3" /> Action
                </button>
              </div>
            </div>
            <div className="relative mt-3 space-y-3 pl-8">
              {draft.steps.length > 0 && (
                <div className="absolute bottom-4 left-3 top-4 w-px bg-slate-200" aria-hidden="true" />
              )}
              {draft.steps.map((step, index) => (
                <div key={index} className="relative rounded-xl border border-slate-200 bg-white p-4 shadow-sm">
                  <div className={`absolute -left-8 top-4 flex h-6 w-6 items-center justify-center rounded-full border ${
                    step.type === 'condition'
                      ? 'border-amber-200 bg-amber-50 text-amber-700'
                      : step.type === 'delay'
                        ? 'border-blue-200 bg-blue-50 text-blue-700'
                        : 'border-emerald-200 bg-emerald-50 text-emerald-700'
                  }`}>
                    {step.type === 'condition' ? <GitBranch className="h-3 w-3" /> : step.type === 'delay' ? <Clock className="h-3 w-3" /> : <Zap className="h-3 w-3" />}
                  </div>
                  <div className="flex items-center justify-between gap-2">
                    <select
                      value={step.type}
                      onChange={(event) => {
                        const type = event.target.value as AutomationStepType;
                        setStep(index, {
                          type,
                          config: type === 'condition'
                            ? { metric: 'ctr', comparator: 'lt', threshold: 1, lookback_hours: 24, min_impressions: 100 }
                            : type === 'delay'
                              ? { amount: 1, unit: 'hours' }
                              : { action: 'send_notification', kind: 'info', title: 'Automation alert', body: '' },
                        });
                      }}
                      className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none"
                    >
                      <option value="condition">Condition</option>
                      <option value="delay">Delay</option>
                      <option value="action">Action</option>
                    </select>
                    <div className="flex items-center gap-1">
                      <button type="button" onClick={() => moveStep(index, -1)} disabled={index === 0} className="rounded-md p-1.5 text-slate-400 hover:bg-slate-50 disabled:opacity-30" aria-label="Move step up">
                        <ArrowUp className="h-3.5 w-3.5" />
                      </button>
                      <button type="button" onClick={() => moveStep(index, 1)} disabled={index === draft.steps.length - 1} className="rounded-md p-1.5 text-slate-400 hover:bg-slate-50 disabled:opacity-30" aria-label="Move step down">
                        <ArrowDown className="h-3.5 w-3.5" />
                      </button>
                      <button type="button" onClick={() => removeStep(index)} className="rounded-md p-1.5 text-slate-400 hover:bg-rose-50 hover:text-rose-600" aria-label="Remove step">
                        <Trash2 className="h-3.5 w-3.5" />
                      </button>
                    </div>
                  </div>

                  {step.type === 'condition' && (
                    <div className="mt-3 grid gap-2 sm:grid-cols-4">
                      <select
                        value={step.config.metric || 'ctr'}
                        onChange={(event) => setStepConfig(index, { metric: event.target.value })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                      >
                        <option value="ctr">CTR %</option>
                        <option value="conversion_rate">Conversion rate %</option>
                        <option value="impressions">Impressions</option>
                        <option value="clicks">Clicks</option>
                        <option value="conversions">Conversions</option>
                        <option value="commission_cents">Commission (cents)</option>
                      </select>
                      <select
                        value={step.config.comparator || 'lt'}
                        onChange={(event) => setStepConfig(index, { comparator: event.target.value })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                      >
                        <option value="lt">Below</option>
                        <option value="lte">At or below</option>
                        <option value="gt">Above</option>
                        <option value="gte">At or above</option>
                      </select>
                      <input
                        type="number"
                        step="0.01"
                        value={step.config.threshold ?? 1}
                        onChange={(event) => setStepConfig(index, { threshold: Number(event.target.value) })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                        placeholder="Threshold"
                      />
                      <input
                        type="number"
                        min={1}
                        value={step.config.lookback_hours ?? 24}
                        onChange={(event) => setStepConfig(index, { lookback_hours: Number(event.target.value) })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                        placeholder="Lookback h"
                      />
                    </div>
                  )}

                  {step.type === 'delay' && (
                    <div className="mt-3 grid gap-2 sm:grid-cols-2">
                      <input
                        type="number"
                        min={1}
                        value={step.config.amount ?? 1}
                        onChange={(event) => setStepConfig(index, { amount: Number(event.target.value) })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                        placeholder="Amount"
                      />
                      <select
                        value={step.config.unit || 'minutes'}
                        onChange={(event) => setStepConfig(index, { unit: event.target.value })}
                        className="rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                      >
                        <option value="seconds">Seconds</option>
                        <option value="minutes">Minutes</option>
                        <option value="hours">Hours</option>
                        <option value="days">Days</option>
                      </select>
                    </div>
                  )}

                  {step.type === 'action' && (
                    <div className="mt-3 space-y-2">
                      <select
                        value={step.config.action || 'send_notification'}
                        onChange={(event) => {
                          const action = event.target.value;
                          setStep(index, {
                            config: action === 'pause_campaign' || action === 'activate_campaign'
                              ? { action, campaign_id: '' }
                              : action === 'webhook'
                                ? { action, url: '' }
                                : { action, kind: 'info', title: 'Automation alert', body: '' },
                          });
                        }}
                        className="w-full rounded-lg border border-slate-200 px-2 py-1.5 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none"
                      >
                        <option value="pause_campaign">Pause campaign</option>
                        <option value="activate_campaign">Activate / publish campaign</option>
                        <option value="send_notification">Send in-app notification</option>
                        <option value="webhook">Call webhook</option>
                      </select>

                      {(step.config.action === 'pause_campaign' || step.config.action === 'activate_campaign') && (
                        <select
                          value={step.config.campaign_id || ''}
                          onChange={(event) => setStepConfig(index, { campaign_id: event.target.value })}
                          className="w-full rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                        >
                          <option value="">Select campaign</option>
                          {visibleCampaigns.map((campaign) => (
                            <option key={campaign.id} value={campaign.id}>{campaign.name}</option>
                          ))}
                        </select>
                      )}

                      {step.config.action === 'send_notification' && (
                        <div className="grid gap-2">
                          <input
                            value={step.config.title || ''}
                            onChange={(event) => setStepConfig(index, { title: event.target.value })}
                            placeholder="Notification title"
                            className="w-full rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                          />
                          <textarea
                            value={step.config.body || ''}
                            onChange={(event) => setStepConfig(index, { body: event.target.value })}
                            placeholder="Notification body. Supports {{workflow.name}} and {{trigger.actual}}."
                            rows={2}
                            className="w-full rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                          />
                        </div>
                      )}

                      {step.config.action === 'webhook' && (
                        <input
                          type="url"
                          value={step.config.url || ''}
                          onChange={(event) => setStepConfig(index, { url: event.target.value })}
                          placeholder="https://hooks.example.com/adcontrol"
                          className="w-full rounded-lg border border-slate-200 px-2 py-1.5 text-xs text-slate-700 focus:border-blue-500 focus:outline-none"
                        />
                      )}
                    </div>
                  )}
                </div>
              ))}
              {draft.steps.length === 0 && (
                <div className="rounded-xl border border-dashed border-slate-300 p-6 text-center text-xs text-slate-500">
                  Add a condition, delay, or action. Active workflows require at least one step.
                </div>
              )}
            </div>
          </div>

          <div className="flex items-center justify-between gap-3 border-t border-slate-100 pt-4">
            <div className="text-[11px] text-slate-400">
              {draft.steps.length} step{draft.steps.length === 1 ? '' : 's'} · worker interval about 30s
            </div>
            <div className="flex gap-2">
              <button
                type="button"
                onClick={() => setBuilderOpen(false)}
                className="rounded-lg border border-slate-200 px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={saveWorkflow}
                disabled={saving}
                className="inline-flex items-center gap-1.5 rounded-lg bg-blue-600 px-3 py-2 text-xs font-semibold text-white hover:bg-blue-700 disabled:opacity-60"
              >
                {saving ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Save className="h-3.5 w-3.5" />}
                Save workflow
              </button>
            </div>
          </div>
        </div>
      </Modal>
    </div>
  );
};
