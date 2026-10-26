import React, { useCallback, useEffect, useState } from 'react';
import { api } from '../api';
import { AuditLogItem } from '../types';
import { ChevronDown, ChevronLeft, ChevronRight, History, RefreshCw, Shield } from 'lucide-react';

interface AuditLogViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

const PAGE_SIZE = 50;

function actionStyle(action: string): string {
  if (action.startsWith('DELETE') || action.includes('ARCHIVE')) {
    return 'bg-rose-50 text-rose-700 border-rose-200';
  }
  if (action.includes('PAUSE') || action.includes('PASSWORD')) {
    return 'bg-amber-50 text-amber-800 border-amber-200';
  }
  if (action.includes('CREATE') || action.includes('PUBLISH')) {
    return 'bg-emerald-50 text-emerald-700 border-emerald-200';
  }
  if (action.includes('LOGIN') || action.includes('ROTATE')) {
    return 'bg-blue-50 text-blue-700 border-blue-200';
  }
  return 'bg-slate-50 text-slate-700 border-slate-200';
}

function formatDetails(details: unknown): string {
  if (details === null || details === undefined || details === '') return '—';
  if (typeof details === 'string') {
    try {
      return JSON.stringify(JSON.parse(details), null, 2);
    } catch (_) {
      return details;
    }
  }
  try {
    return JSON.stringify(details, null, 2);
  } catch (_) {
    return String(details);
  }
}

export const AuditLogView: React.FC<AuditLogViewProps> = ({ onNotify }) => {
  const [logs, setLogs] = useState<AuditLogItem[]>([]);
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(true);
  const [expanded, setExpanded] = useState<string | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const data = await api.getAuditLogs(PAGE_SIZE, offset);
      setLogs(data);
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load audit log');
    } finally {
      setLoading(false);
    }
  }, [offset, onNotify]);

  useEffect(() => {
    load();
  }, [load]);

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900 flex items-center gap-2">
            <History className="w-6 h-6 text-slate-500" /> Audit log
          </h2>
          <p className="text-sm text-slate-500 mt-1">
            Sensitive changes and authentication events. Records are append-only and retained with the database.
          </p>
        </div>
        <button
          onClick={load}
          disabled={loading}
          className="inline-flex items-center gap-1.5 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-60 px-3 py-2 rounded-lg transition-colors self-start sm:self-auto"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
        </button>
      </div>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
              <tr>
                <th className="px-5 py-3 font-semibold">Time (local)</th>
                <th className="px-5 py-3 font-semibold">Actor</th>
                <th className="px-5 py-3 font-semibold">Action</th>
                <th className="px-5 py-3 font-semibold">Entity</th>
                <th className="px-5 py-3 font-semibold text-right">Details</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {logs.map((log) => {
                const isOpen = expanded === log.id;
                return (
                  <React.Fragment key={log.id}>
                    <tr className="hover:bg-slate-50/70">
                      <td className="px-5 py-3 text-slate-600 whitespace-nowrap tabular-nums">
                        {new Date(log.created_at).toLocaleString()}
                      </td>
                      <td className="px-5 py-3 text-slate-800 font-medium">{log.actor}</td>
                      <td className="px-5 py-3">
                        <span className={`inline-flex items-center gap-1 text-[11px] font-semibold border px-2 py-0.5 rounded-full ${actionStyle(log.action)}`}>
                          <Shield className="w-3 h-3" /> {log.action}
                        </span>
                      </td>
                      <td className="px-5 py-3 text-slate-600">
                        <span className="text-slate-500">{log.entity_type}</span>
                        <span className="mx-1 text-slate-300">/</span>
                        <code className="text-[11px] bg-slate-100 px-1.5 py-0.5 rounded text-slate-700">
                          {log.entity_id}
                        </code>
                      </td>
                      <td className="px-5 py-3 text-right">
                        <button
                          onClick={() => setExpanded(isOpen ? null : log.id)}
                          className="inline-flex items-center gap-1 text-xs font-semibold text-blue-600 hover:text-blue-800"
                          aria-expanded={isOpen}
                        >
                          {isOpen ? 'Hide' : 'View'}
                          <ChevronDown className={`w-3.5 h-3.5 transition-transform ${isOpen ? 'rotate-180' : ''}`} />
                        </button>
                      </td>
                    </tr>
                    {isOpen && (
                      <tr className="bg-slate-50/60">
                        <td colSpan={5} className="px-5 py-3">
                          <pre className="text-[11px] leading-relaxed text-slate-700 bg-white border border-slate-200 rounded-lg p-3 overflow-x-auto max-h-72">
                            {formatDetails(log.details)}
                          </pre>
                        </td>
                      </tr>
                    )}
                  </React.Fragment>
                );
              })}
              {logs.length === 0 && !loading && (
                <tr>
                  <td colSpan={5} className="px-5 py-10 text-center text-sm text-slate-500">
                    No audit events yet.
                  </td>
                </tr>
              )}
              {loading && logs.length === 0 && (
                <tr>
                  <td colSpan={5} className="px-5 py-10 text-center text-sm text-slate-500">
                    Loading audit events…
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>

        <div className="px-5 py-3 border-t border-slate-100 flex items-center justify-between gap-3">
          <span className="text-xs text-slate-500">
            Showing events {offset + 1}–{offset + logs.length}
          </span>
          <div className="flex items-center gap-2">
            <button
              onClick={() => setOffset((value) => Math.max(0, value - PAGE_SIZE))}
              disabled={offset === 0 || loading}
              className="inline-flex items-center gap-1 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-40 px-3 py-1.5 rounded-lg transition-colors"
            >
              <ChevronLeft className="w-3.5 h-3.5" /> Previous
            </button>
            <button
              onClick={() => setOffset((value) => value + PAGE_SIZE)}
              disabled={logs.length < PAGE_SIZE || loading}
              className="inline-flex items-center gap-1 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-40 px-3 py-1.5 rounded-lg transition-colors"
            >
              Next <ChevronRight className="w-3.5 h-3.5" />
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
