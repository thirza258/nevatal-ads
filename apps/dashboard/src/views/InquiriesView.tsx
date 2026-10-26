import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  ExternalLink,
  Globe2,
  Inbox,
  Megaphone,
  RefreshCw,
  Search,
  X,
} from 'lucide-react';
import { api } from '../api';
import {
  PartnerInquiry,
  PartnerInquiryKind,
  PartnerInquiryStatus,
} from '../types';

interface InquiriesViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

type KindFilter = 'all' | PartnerInquiryKind;
type StatusFilter = 'all' | PartnerInquiryStatus;

const STATUS_OPTIONS: PartnerInquiryStatus[] = ['new', 'reviewing', 'closed'];

const statusStyle: Record<PartnerInquiryStatus, string> = {
  new: 'bg-blue-50 text-blue-700 border-blue-200',
  reviewing: 'bg-amber-50 text-amber-800 border-amber-200',
  closed: 'bg-slate-100 text-slate-600 border-slate-200',
};

const kindStyle: Record<PartnerInquiryKind, string> = {
  publisher: 'bg-teal-50 text-teal-800 border-teal-200',
  client: 'bg-indigo-50 text-indigo-700 border-indigo-200',
};

function safeWebsite(value: string): string {
  return /^https?:\/\//i.test(value) ? value : '';
}

function formatReceived(value: string): string {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

export const InquiriesView: React.FC<InquiriesViewProps> = ({ onNotify }) => {
  const [inquiries, setInquiries] = useState<PartnerInquiry[]>([]);
  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState('');
  const [expanded, setExpanded] = useState<string | null>(null);
  const [updatingId, setUpdatingId] = useState<string | null>(null);
  const [kindFilter, setKindFilter] = useState<KindFilter>('all');
  const [statusFilter, setStatusFilter] = useState<StatusFilter>('all');
  const [search, setSearch] = useState('');

  const load = useCallback(async () => {
    setLoading(true);
    setLoadError('');
    try {
      const data = await api.getPartnerInquiries({ limit: 200 });
      setInquiries(data);
    } catch (err: any) {
      const message = err?.message || 'Failed to load inquiries';
      setLoadError(message);
      onNotify('error', message);
    } finally {
      setLoading(false);
    }
  }, [onNotify]);

  useEffect(() => {
    load();
  }, [load]);

  const counts = useMemo(() => ({
    total: inquiries.length,
    new: inquiries.filter((inquiry) => inquiry.status === 'new').length,
    publishers: inquiries.filter((inquiry) => inquiry.kind === 'publisher').length,
    clients: inquiries.filter((inquiry) => inquiry.kind === 'client').length,
  }), [inquiries]);

  const visible = useMemo(() => {
    const needle = search.trim().toLowerCase();
    return inquiries.filter((inquiry) => {
      if (kindFilter !== 'all' && inquiry.kind !== kindFilter) return false;
      if (statusFilter !== 'all' && inquiry.status !== statusFilter) return false;
      if (!needle) return true;
      return [inquiry.name, inquiry.email, inquiry.company, inquiry.website, inquiry.message]
        .some((value) => value.toLowerCase().includes(needle));
    });
  }, [inquiries, kindFilter, statusFilter, search]);

  const updateStatus = async (inquiry: PartnerInquiry, status: PartnerInquiryStatus) => {
    if (status === inquiry.status || updatingId) return;
    setUpdatingId(inquiry.id);
    try {
      const updated = await api.updatePartnerInquiry(inquiry.id, status);
      setInquiries((current) => current.map((item) => (item.id === updated.id ? updated : item)));
      onNotify('success', `Inquiry from ${inquiry.name} marked ${status}`);
    } catch (err: any) {
      onNotify('error', err?.message || 'Failed to update inquiry');
    } finally {
      setUpdatingId(null);
    }
  };

  const filtersActive = kindFilter !== 'all' || statusFilter !== 'all' || search.trim() !== '';

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900 flex items-center gap-2">
            <Inbox className="w-6 h-6 text-slate-500" /> Partner inquiries
          </h2>
          <p className="text-sm text-slate-500 mt-1">
            Publisher and client applications submitted from the public site. Nothing is provisioned
            automatically; follow up by email, then mark the inquiry as reviewing or closed.
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

      <div className="grid grid-cols-2 gap-3 lg:grid-cols-4">
        <div className="bg-white border border-slate-200 rounded-xl p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Total</div>
          <div className="mt-1 text-2xl font-bold text-slate-900 tabular-nums">{counts.total}</div>
        </div>
        <div className="bg-white border border-slate-200 rounded-xl p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">New</div>
          <div className="mt-1 text-2xl font-bold text-blue-700 tabular-nums">{counts.new}</div>
        </div>
        <div className="bg-white border border-slate-200 rounded-xl p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Publishers</div>
          <div className="mt-1 text-2xl font-bold text-teal-700 tabular-nums">{counts.publishers}</div>
        </div>
        <div className="bg-white border border-slate-200 rounded-xl p-4">
          <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">Clients</div>
          <div className="mt-1 text-2xl font-bold text-indigo-700 tabular-nums">{counts.clients}</div>
        </div>
      </div>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
        <div className="flex flex-col gap-3 border-b border-slate-100 p-4 lg:flex-row lg:items-center">
          <div className="relative min-w-0 flex-1">
            <Search className="pointer-events-none absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-400" aria-hidden="true" />
            <input
              type="search"
              value={search}
              onChange={(event) => setSearch(event.target.value)}
              placeholder="Search name, email, company, or text"
              aria-label="Search partner inquiries"
              className="w-full rounded-lg border border-slate-200 bg-white py-2 pl-9 pr-3 text-sm text-slate-800 placeholder:text-slate-400 focus:border-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500/20"
            />
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <label className="sr-only" htmlFor="inquiry-kind-filter">Filter by type</label>
            <select
              id="inquiry-kind-filter"
              value={kindFilter}
              onChange={(event) => setKindFilter(event.target.value as KindFilter)}
              className="rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500/20"
            >
              <option value="all">All types</option>
              <option value="publisher">Publishers</option>
              <option value="client">Clients</option>
            </select>
            <label className="sr-only" htmlFor="inquiry-status-filter">Filter by status</label>
            <select
              id="inquiry-status-filter"
              value={statusFilter}
              onChange={(event) => setStatusFilter(event.target.value as StatusFilter)}
              className="rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500/20"
            >
              <option value="all">All statuses</option>
              <option value="new">New</option>
              <option value="reviewing">Reviewing</option>
              <option value="closed">Closed</option>
            </select>
            {filtersActive && (
              <button
                type="button"
                onClick={() => {
                  setKindFilter('all');
                  setStatusFilter('all');
                  setSearch('');
                }}
                className="inline-flex items-center gap-1 rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-50"
              >
                <X className="h-3.5 w-3.5" aria-hidden="true" /> Clear
              </button>
            )}
          </div>
        </div>

        {loadError && inquiries.length === 0 ? (
          <div className="p-8 text-center" role="alert">
            <p className="text-sm text-rose-700">{loadError}</p>
            <button
              type="button"
              onClick={load}
              className="mt-3 inline-flex items-center gap-1.5 rounded-lg border border-slate-200 bg-white px-3 py-2 text-xs font-semibold text-slate-700 hover:bg-slate-50"
            >
              <RefreshCw className="h-3.5 w-3.5" aria-hidden="true" /> Try again
            </button>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 text-[11px] uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-5 py-3 font-semibold">Received</th>
                  <th className="px-5 py-3 font-semibold">Type</th>
                  <th className="px-5 py-3 font-semibold">Contact</th>
                  <th className="px-5 py-3 font-semibold">Company / site</th>
                  <th className="px-5 py-3 font-semibold">Message</th>
                  <th className="px-5 py-3 font-semibold">Status</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {visible.map((inquiry) => {
                  const isOpen = expanded === inquiry.id;
                  const website = safeWebsite(inquiry.website);
                  return (
                    <React.Fragment key={inquiry.id}>
                      <tr className="align-top hover:bg-slate-50/70">
                        <td className="px-5 py-4 text-xs text-slate-600 whitespace-nowrap tabular-nums">
                          {formatReceived(inquiry.created_at)}
                        </td>
                        <td className="px-5 py-4">
                          <span className={`inline-flex items-center gap-1 whitespace-nowrap rounded-full border px-2 py-0.5 text-[11px] font-semibold ${kindStyle[inquiry.kind]}`}>
                            {inquiry.kind === 'publisher'
                              ? <Globe2 className="h-3 w-3" aria-hidden="true" />
                              : <Megaphone className="h-3 w-3" aria-hidden="true" />}
                            {inquiry.kind === 'publisher' ? 'Publisher' : 'Client'}
                          </span>
                        </td>
                        <td className="px-5 py-4">
                          <div className="font-medium text-slate-900">{inquiry.name}</div>
                          <a
                            href={`mailto:${inquiry.email}`}
                            className="text-xs text-blue-700 hover:text-blue-900 hover:underline"
                          >
                            {inquiry.email}
                          </a>
                        </td>
                        <td className="px-5 py-4 text-slate-700">
                          <div>{inquiry.company || '—'}</div>
                          {website ? (
                            <a
                              href={website}
                              target="_blank"
                              rel="noreferrer noopener"
                              className="mt-1 inline-flex max-w-[16rem] items-center gap-1 truncate text-xs text-blue-700 hover:text-blue-900 hover:underline"
                            >
                              <span className="truncate">{website}</span>
                              <ExternalLink className="h-3 w-3 shrink-0" aria-hidden="true" />
                            </a>
                          ) : (
                            <span className="text-xs text-slate-400">{inquiry.website || 'No site provided'}</span>
                          )}
                        </td>
                        <td className="max-w-[22rem] px-5 py-4">
                          <p className={`text-sm leading-6 text-slate-600 ${isOpen ? '' : 'line-clamp-2'}`}>
                            {inquiry.message}
                          </p>
                          {inquiry.message.length > 120 && (
                            <button
                              type="button"
                              onClick={() => setExpanded(isOpen ? null : inquiry.id)}
                              aria-expanded={isOpen}
                              className="mt-1 text-xs font-semibold text-blue-600 hover:text-blue-800"
                            >
                              {isOpen ? 'Show less' : 'Read full message'}
                            </button>
                          )}
                        </td>
                        <td className="px-5 py-4">
                          <span className={`mb-2 inline-flex whitespace-nowrap rounded-full border px-2 py-0.5 text-[11px] font-semibold capitalize ${statusStyle[inquiry.status]}`}>
                            {inquiry.status}
                          </span>
                          <label className="sr-only" htmlFor={`status-${inquiry.id}`}>
                            Update status for {inquiry.name}
                          </label>
                          <select
                            id={`status-${inquiry.id}`}
                            value={inquiry.status}
                            disabled={updatingId === inquiry.id}
                            onChange={(event) => updateStatus(inquiry, event.target.value as PartnerInquiryStatus)}
                            className="block w-full min-w-[8.5rem] rounded-lg border border-slate-200 bg-white px-2.5 py-1.5 text-xs font-semibold text-slate-700 focus:border-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500/20 disabled:opacity-60"
                          >
                            {STATUS_OPTIONS.map((status) => (
                              <option key={status} value={status}>
                                {status.charAt(0).toUpperCase() + status.slice(1)}
                              </option>
                            ))}
                          </select>
                        </td>
                      </tr>
                      {isOpen && (
                        <tr className="bg-slate-50/60">
                          <td colSpan={6} className="px-5 py-4">
                            <div className="rounded-lg border border-slate-200 bg-white p-4">
                              <div className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">
                                Full message from {inquiry.name}
                              </div>
                              <p className="mt-2 whitespace-pre-wrap text-sm leading-7 text-slate-700">
                                {inquiry.message}
                              </p>
                              <div className="mt-3 flex flex-wrap gap-4 text-xs text-slate-500">
                                <a href={`mailto:${inquiry.email}`} className="font-semibold text-blue-700 hover:underline">
                                  Reply by email
                                </a>
                                {website && (
                                  <a
                                    href={website}
                                    target="_blank"
                                    rel="noreferrer noopener"
                                    className="inline-flex items-center gap-1 font-semibold text-blue-700 hover:underline"
                                  >
                                    Open website <ExternalLink className="h-3 w-3" aria-hidden="true" />
                                  </a>
                                )}
                                <span>Last updated {formatReceived(inquiry.updated_at)}</span>
                              </div>
                            </div>
                          </td>
                        </tr>
                      )}
                    </React.Fragment>
                  );
                })}
                {visible.length === 0 && !loading && (
                  <tr>
                    <td colSpan={6} className="px-5 py-10 text-center text-sm text-slate-500">
                      {filtersActive
                        ? 'No inquiries match these filters.'
                        : 'No partner inquiries yet. New applications from the public site will appear here.'}
                    </td>
                  </tr>
                )}
                {loading && inquiries.length === 0 && (
                  <tr>
                    <td colSpan={6} className="px-5 py-10 text-center text-sm text-slate-500">
                      Loading partner inquiries…
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        )}

        <div className="flex flex-wrap items-center justify-between gap-3 border-t border-slate-100 px-5 py-3 text-xs text-slate-500">
          <span>
            Showing {visible.length} of {inquiries.length} loaded inquiries
          </span>
          <span>Status changes are recorded in the audit log.</span>
        </div>
      </div>
    </div>
  );
};
