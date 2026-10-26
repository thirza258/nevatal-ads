import React, { useEffect, useMemo, useState } from 'react';
import { api } from '../api';
import { Publisher } from '../types';
import { Modal } from '../components/Modal';
import {
  Check,
  Copy,
  Edit,
  Loader2,
  Mail,
  Plus,
  Search,
  ShieldCheck,
  Users,
} from 'lucide-react';

interface PublishersViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

const STATUS_STYLES: Record<string, string> = {
  active: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  pending: 'bg-amber-50 text-amber-700 border-amber-200',
  suspended: 'bg-rose-50 text-rose-700 border-rose-200',
  rejected: 'bg-slate-100 text-slate-600 border-slate-200',
};

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

export const PublishersView: React.FC<PublishersViewProps> = ({ onNotify }) => {
  const [publishers, setPublishers] = useState<Publisher[]>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState('');
  const [createOpen, setCreateOpen] = useState(false);
  const [editPublisher, setEditPublisher] = useState<Publisher | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [copiedId, setCopiedId] = useState<string | null>(null);

  const [name, setName] = useState('');
  const [ownerEmail, setOwnerEmail] = useState('');
  const [password, setPassword] = useState('');
  const [contactEmail, setContactEmail] = useState('');
  const [timezone, setTimezone] = useState('UTC');
  const [status, setStatus] = useState<'pending' | 'active' | 'suspended' | 'rejected'>('pending');

  const load = async () => {
    try {
      setLoading(true);
      setPublishers(await api.getPublishers());
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load publishers');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const counts = useMemo(
    () => ({
      active: publishers.filter((p) => p.status === 'active').length,
      pending: publishers.filter((p) => p.status === 'pending').length,
      suspended: publishers.filter((p) => p.status === 'suspended').length,
    }),
    [publishers],
  );

  const filtered = useMemo(() => {
    const query = search.trim().toLowerCase();
    if (!query) return publishers;
    return publishers.filter(
      (p) =>
        p.name.toLowerCase().includes(query) ||
        p.owner_email.toLowerCase().includes(query) ||
        p.contact_email.toLowerCase().includes(query),
    );
  }, [publishers, search]);

  const resetForm = () => {
    setName('');
    setOwnerEmail('');
    setPassword('');
    setContactEmail('');
    setTimezone('UTC');
    setStatus('pending');
  };

  const handleCreate = async (event: React.FormEvent) => {
    event.preventDefault();
    if (password.length < 12) {
      onNotify('error', 'Owner password must be at least 12 characters.');
      return;
    }
    setSubmitting(true);
    try {
      const created = await api.createPublisher({
        name,
        owner_email: ownerEmail,
        password,
        contact_email: contactEmail || ownerEmail,
        timezone: timezone || 'UTC',
        status,
      });
      onNotify('success', `Publisher "${created.name}" provisioned. Share the owner sign-in separately.`);
      setCreateOpen(false);
      resetForm();
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to create publisher');
    } finally {
      setSubmitting(false);
    }
  };

  const handleEdit = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!editPublisher) return;
    setSubmitting(true);
    try {
      const updated = await api.updatePublisher(editPublisher.id, {
        name,
        contact_email: contactEmail,
        timezone: timezone || 'UTC',
        status,
      });
      onNotify('success', `Publisher "${updated.name}" updated.`);
      setEditPublisher(null);
      resetForm();
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update publisher');
    } finally {
      setSubmitting(false);
    }
  };

  const openEdit = (publisher: Publisher) => {
    setEditPublisher(publisher);
    setName(publisher.name);
    setOwnerEmail(publisher.owner_email);
    setContactEmail(publisher.contact_email);
    setTimezone(publisher.timezone || 'UTC');
    setStatus(publisher.status as typeof status);
    setPassword('');
  };

  const copyOwnerEmail = async (publisher: Publisher) => {
    try {
      await navigator.clipboard.writeText(publisher.owner_email);
      setCopiedId(publisher.id);
      window.setTimeout(() => setCopiedId(null), 1600);
    } catch (_) {
      onNotify('error', 'Clipboard access was blocked by the browser.');
    }
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Publishers</h2>
          <p className="text-sm text-slate-500 mt-1">
            Invite-only accounts that own verified sites. Publishers can manage house and affiliate campaigns for
            their own inventory; direct advertisers remain platform-admin managed.
          </p>
        </div>
        <button
          onClick={() => {
            resetForm();
            setCreateOpen(true);
          }}
          className="inline-flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all self-start"
        >
          <Plus className="w-4 h-4" /> Provision Publisher
        </button>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
        {[
          { label: 'Active publishers', value: counts.active, icon: ShieldCheck, tone: 'text-emerald-600 bg-emerald-50' },
          { label: 'Awaiting activation', value: counts.pending, icon: Users, tone: 'text-amber-600 bg-amber-50' },
          { label: 'Suspended', value: counts.suspended, icon: Users, tone: 'text-rose-600 bg-rose-50' },
        ].map(({ label, value, icon: Icon, tone }) => (
          <div key={label} className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm flex items-center justify-between">
            <div>
              <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">{label}</div>
              <div className="mt-1 text-2xl font-bold text-slate-900">{value}</div>
            </div>
            <div className={`w-10 h-10 rounded-lg flex items-center justify-center ${tone}`}>
              <Icon className="w-5 h-5" />
            </div>
          </div>
        ))}
      </div>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
        <div className="p-4 border-b border-slate-100 flex flex-wrap items-center justify-between gap-3">
          <div className="relative min-w-[220px]">
            <Search className="w-4 h-4 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
            <input
              type="search"
              value={search}
              onChange={(event) => setSearch(event.target.value)}
              placeholder="Search publishers or email..."
              className="w-full pl-9 pr-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs text-slate-900 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="text-xs text-slate-400">
            Showing {filtered.length} of {publishers.length}
          </div>
        </div>

        {loading ? (
          <div className="p-12 text-center text-slate-500 flex items-center justify-center gap-2">
            <Loader2 className="w-4 h-4 animate-spin" /> Loading publishers...
          </div>
        ) : filtered.length === 0 ? (
          <div className="p-12 text-center text-slate-500 flex flex-col items-center gap-2">
            <Users className="w-10 h-10 text-slate-300" />
            <div className="font-semibold text-slate-700">No publishers found</div>
            <div className="text-xs text-slate-400">
              Provision an invite-only publisher account, then verify the domains it owns.
            </div>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-6 py-3.5">Publisher</th>
                  <th className="px-6 py-3.5">Owner sign-in</th>
                  <th className="px-6 py-3.5">Contact / timezone</th>
                  <th className="px-6 py-3.5">Status</th>
                  <th className="px-6 py-3.5">Created</th>
                  <th className="px-6 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {filtered.map((publisher) => (
                  <tr key={publisher.id} className="hover:bg-slate-50/60 transition-colors">
                    <td className="px-6 py-4">
                      <div className="font-semibold text-slate-900">{publisher.name}</div>
                      <div className="text-[11px] text-slate-400 font-mono mt-0.5">{publisher.id}</div>
                    </td>
                    <td className="px-6 py-4">
                      <div className="flex items-center gap-2">
                        <span className="text-xs text-slate-700">{publisher.owner_email}</span>
                        <button
                          onClick={() => copyOwnerEmail(publisher)}
                          className="text-slate-400 hover:text-slate-600 p-1 rounded"
                          title="Copy owner email"
                        >
                          {copiedId === publisher.id ? (
                            <Check className="w-3.5 h-3.5 text-emerald-600" />
                          ) : (
                            <Copy className="w-3.5 h-3.5" />
                          )}
                        </button>
                      </div>
                      <div className="text-[10px] text-slate-400 mt-0.5">Password is never displayed</div>
                    </td>
                    <td className="px-6 py-4 text-xs text-slate-600">
                      <div className="flex items-center gap-1">
                        <Mail className="w-3.5 h-3.5 text-slate-400" />
                        <span>{publisher.contact_email || publisher.owner_email || '—'}</span>
                      </div>
                      <div className="text-[10px] text-slate-400 mt-0.5">{publisher.timezone || 'UTC'}</div>
                    </td>
                    <td className="px-6 py-4">
                      <span
                        className={`inline-flex items-center gap-1 text-xs font-semibold px-2.5 py-1 rounded-full border ${
                          STATUS_STYLES[publisher.status] || STATUS_STYLES.rejected
                        }`}
                      >
                        {publisher.status}
                      </span>
                    </td>
                    <td className="px-6 py-4 text-xs text-slate-500">{formatDate(publisher.created_at)}</td>
                    <td className="px-6 py-4 text-right">
                      <button
                        onClick={() => openEdit(publisher)}
                        className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg transition-colors"
                        title="Edit publisher"
                      >
                        <Edit className="w-4 h-4" />
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="rounded-lg border border-blue-200 bg-blue-50 px-4 py-3 text-xs text-blue-900 leading-relaxed">
        Publisher accounts are invite-only. Provisioning creates the owner login; deliver the temporary password
        through a secure channel and have the owner change it after signing in. Domain verification and site
        monetization approval are required before publisher inventory can deliver ads.
      </div>

      <Modal isOpen={createOpen} onClose={() => setCreateOpen(false)} title="Provision Publisher" maxWidth="max-w-xl">
        <form onSubmit={handleCreate} className="flex flex-col gap-4">
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Publisher name
              </label>
              <input
                required
                maxLength={120}
                value={name}
                onChange={(event) => setName(event.target.value)}
                placeholder="e.g. Northwind Media"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Initial status
              </label>
              <select
                value={status}
                onChange={(event) => setStatus(event.target.value as typeof status)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="pending">Pending (recommended)</option>
                <option value="active">Active</option>
                <option value="suspended">Suspended</option>
                <option value="rejected">Rejected</option>
              </select>
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Owner email (sign-in)
            </label>
            <input
              required
              type="email"
              autoComplete="off"
              value={ownerEmail}
              onChange={(event) => setOwnerEmail(event.target.value)}
              placeholder="owner@example.com"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Temporary password
            </label>
            <input
              required
              type="password"
              minLength={12}
              maxLength={128}
              autoComplete="new-password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              placeholder="At least 12 characters"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              Sent to the API over TLS and hashed server-side with PBKDF2. It is never stored in this browser or
              shown again.
            </p>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Contact email (optional)
              </label>
              <input
                type="email"
                value={contactEmail}
                onChange={(event) => setContactEmail(event.target.value)}
                placeholder="Defaults to owner email"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Timezone
              </label>
              <input
                value={timezone}
                onChange={(event) => setTimezone(event.target.value)}
                placeholder="UTC"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setCreateOpen(false)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg disabled:opacity-50"
            >
              {submitting && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
              {submitting ? 'Provisioning...' : 'Provision Publisher'}
            </button>
          </div>
        </form>
      </Modal>

      <Modal
        isOpen={!!editPublisher}
        onClose={() => setEditPublisher(null)}
        title={`Edit Publisher: ${editPublisher?.name || ''}`}
      >
        <form onSubmit={handleEdit} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Publisher name
            </label>
            <input
              required
              maxLength={120}
              value={name}
              onChange={(event) => setName(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Owner email (read-only)
            </label>
            <input
              readOnly
              value={ownerEmail}
              className="w-full px-3 py-2 bg-slate-100 border border-slate-200 rounded-lg text-sm text-slate-500"
            />
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Contact email
              </label>
              <input
                type="email"
                value={contactEmail}
                onChange={(event) => setContactEmail(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Timezone
              </label>
              <input
                value={timezone}
                onChange={(event) => setTimezone(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Status
            </label>
            <select
              value={status}
              onChange={(event) => setStatus(event.target.value as typeof status)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="pending">Pending</option>
              <option value="active">Active (delivery allowed once sites are verified)</option>
              <option value="suspended">Suspended (stops delivery immediately)</option>
              <option value="rejected">Rejected</option>
            </select>
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setEditPublisher(null)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg disabled:opacity-50"
            >
              {submitting && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
              {submitting ? 'Saving...' : 'Save Changes'}
            </button>
          </div>
        </form>
      </Modal>
    </div>
  );
};
