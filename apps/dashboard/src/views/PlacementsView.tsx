import React, { useEffect, useState } from 'react';
import { api } from '../api';
import { AdSourceType, Site, Placement } from '../types';
import { Modal } from '../components/Modal';
import {
  Plus,
  Copy,
  Check,
  Trash2,
  Edit,
  LayoutGrid,
} from 'lucide-react';

interface PlacementsViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  openCreateModalDirectly?: boolean;
  canWrite?: boolean;
}

const PLACEMENT_SOURCE_OPTIONS: { value: AdSourceType; label: string }[] = [
  { value: 'house', label: 'House' },
  { value: 'direct', label: 'Direct' },
  { value: 'affiliate', label: 'Affiliate' },
];

type PlacementFormat = 'responsive_banner' | 'image' | 'text_link' | 'native_card' | 'product_card';
type DisclosureMode = 'required' | 'inherited' | 'disabled_only_if_not_applicable';

export const PlacementsView: React.FC<PlacementsViewProps> = ({
  onNotify,
  openCreateModalDirectly = false,
  canWrite = true,
}) => {
  const [placements, setPlacements] = useState<Placement[]>([]);
  const [sites, setSites] = useState<Site[]>([]);
  const [selectedSiteFilter, setSelectedSiteFilter] = useState<string>('all');
  const [loading, setLoading] = useState(true);

  const [createModalOpen, setCreateModalOpen] = useState(openCreateModalDirectly);
  const [editPlacement, setEditPlacement] = useState<Placement | null>(null);
  const [copiedKey, setCopiedKey] = useState<string | null>(null);

  // Form states
  const [siteId, setSiteId] = useState('');
  const [placementKey, setPlacementKey] = useState('');
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [format, setFormat] = useState<PlacementFormat>('responsive_banner');
  const [fallbackBehavior, setFallbackBehavior] = useState<'hide' | 'blank' | 'custom'>('hide');
  const [status, setStatus] = useState<'active' | 'disabled'>('active');
  const [allowedSourceTypes, setAllowedSourceTypes] = useState<AdSourceType[]>([
    'house',
    'direct',
    'affiliate',
  ]);
  const [maxItems, setMaxItems] = useState(1);
  const [disclosureMode, setDisclosureMode] = useState<DisclosureMode>('inherited');
  const [submitting, setSubmitting] = useState(false);

  const loadData = async () => {
    try {
      setLoading(true);
      const [pRes, sRes] = await Promise.all([
        api.getPlacements(selectedSiteFilter !== 'all' ? selectedSiteFilter : undefined),
        api.getSites(),
      ]);
      setPlacements(pRes);
      setSites(sRes);
      if (sRes.length > 0 && !siteId) {
        setSiteId(sRes[0].id);
      }
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load placements');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, [selectedSiteFilter]);

  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!siteId || !placementKey || !name) return;
    if (allowedSourceTypes.length === 0) {
      onNotify('error', 'Select at least one allowed source type.');
      return;
    }
    setSubmitting(true);
    try {
      const p = await api.createPlacement({
        site_id: siteId,
        placement_key: placementKey.toLowerCase().trim().replace(/[^a-z0-9_-]/g, '-'),
        name,
        description,
        format,
        fallback_behavior: fallbackBehavior,
        status,
        allowed_source_types: allowedSourceTypes,
        max_items: Number(maxItems),
        disclosure_mode: disclosureMode,
        layout_config: {},
      });

      onNotify('success', `Placement "${p.name}" created.`);
      setCreateModalOpen(false);
      resetForm();
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to create placement');
    } finally {
      setSubmitting(false);
    }
  };

  const handleEditSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!editPlacement) return;
    if (allowedSourceTypes.length === 0) {
      onNotify('error', 'Select at least one allowed source type.');
      return;
    }
    setSubmitting(true);
    try {
      const updated = await api.updatePlacement(editPlacement.id, {
        name,
        description,
        format,
        fallback_behavior: fallbackBehavior,
        status,
        allowed_source_types: allowedSourceTypes,
        max_items: Number(maxItems),
        disclosure_mode: disclosureMode,
      });

      onNotify('success', `Placement "${updated.name}" updated.`);
      setEditPlacement(null);
      resetForm();
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update placement');
    } finally {
      setSubmitting(false);
    }
  };

  const handleDelete = async (p: Placement) => {
    if (
      !window.confirm(
        `Delete placement "${p.name}"? Active campaigns assigned to it will stop serving. Placements with analytics history are disabled instead of deleted.`,
      )
    ) {
      return;
    }
    try {
      const result = await api.deletePlacement(p.id);
      if (result.status === 'archived') {
        onNotify('info', result.message || `Placement "${p.name}" was disabled to preserve analytics history.`);
      } else {
        onNotify('success', `Deleted placement "${p.name}".`);
      }
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to delete placement');
    }
  };

  const openEditModal = (p: Placement) => {
    setEditPlacement(p);
    setName(p.name);
    setDescription(p.description || '');
    setFormat(p.format);
    setFallbackBehavior(p.fallback_behavior);
    setStatus(p.status);
    setAllowedSourceTypes(
      p.allowed_source_types?.length ? p.allowed_source_types : ['house', 'direct', 'affiliate'],
    );
    setMaxItems(p.max_items || 1);
    setDisclosureMode(p.disclosure_mode || 'inherited');
  };

  const resetForm = () => {
    setName('');
    setDescription('');
    setPlacementKey('');
    setFormat('responsive_banner');
    setFallbackBehavior('hide');
    setStatus('active');
    setAllowedSourceTypes(['house', 'direct', 'affiliate']);
    setMaxItems(1);
    setDisclosureMode('inherited');
  };

  const toggleSource = (source: AdSourceType) => {
    setAllowedSourceTypes((current) =>
      current.includes(source) ? current.filter((value) => value !== source) : [...current, source],
    );
  };

  const copyMarkup = (key: string) => {
    const markup = `<div data-adcontrol-placement="${key}"></div>`;
    navigator.clipboard.writeText(markup);
    setCopiedKey(key);
    setTimeout(() => setCopiedKey(null), 2000);
    onNotify('info', `Copied embed markup for ${key}`);
  };

  const getSiteName = (id: string) => {
    return sites.find((s) => s.id === id)?.name || id;
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Placement Slots</h2>
          <p className="text-sm text-slate-500">
            Define stable markup tags embedded on your websites where ads are rendered centrally
          </p>
        </div>

        <div className="flex items-center gap-3">
          {/* Site Filter */}
          <select
            value={selectedSiteFilter}
            onChange={(e) => setSelectedSiteFilter(e.target.value)}
            className="px-3 py-2 bg-white border border-slate-200 rounded-lg text-xs font-medium text-slate-700 shadow-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
          >
            <option value="all">All Registered Websites</option>
            {sites.map((s) => (
              <option key={s.id} value={s.id}>
                {s.name}
              </option>
            ))}
          </select>

          {canWrite && (
            <button
              onClick={() => {
                resetForm();
                setCreateModalOpen(true);
              }}
              disabled={sites.length === 0}
              className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all disabled:opacity-50"
            >
              <Plus className="w-4 h-4" />
              <span>Create Placement</span>
            </button>
          )}
        </div>
      </div>

      {/* Placements Grid / Table */}
      <div className="bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden">
        {loading ? (
          <div className="p-12 text-center text-slate-500">Loading placement slots...</div>
        ) : placements.length === 0 ? (
          <div className="p-12 text-center text-slate-500 flex flex-col items-center gap-3">
            <LayoutGrid className="w-10 h-10 text-slate-300" />
            <div>
              <div className="font-semibold text-slate-700">No placements configured</div>
              <div className="text-xs text-slate-400 mt-1">
                Create a placement like "header-banner" or "article-inline" to start serving ads.
              </div>
            </div>
            {canWrite && sites.length > 0 && (
              <button
                onClick={() => setCreateModalOpen(true)}
                className="mt-2 text-xs font-semibold text-blue-600 bg-blue-50 px-3 py-1.5 rounded-lg border border-blue-200"
              >
                Create Placement Now
              </button>
            )}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-6 py-3.5">Placement Name & Key</th>
                  <th className="px-6 py-3.5">Website</th>
                  <th className="px-6 py-3.5">Format</th>
                  <th className="px-6 py-3.5">Embed Markup</th>
                  <th className="px-6 py-3.5">Status</th>
                  <th className="px-6 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {placements.map((p) => (
                  <tr key={p.id} className="hover:bg-slate-50/60 transition-colors">
                    <td className="px-6 py-4">
                      <div className="font-semibold text-slate-900">{p.name}</div>
                      <code className="text-xs font-mono text-blue-700 bg-blue-50 px-1.5 py-0.5 rounded border border-blue-200/60 mt-1 inline-block">
                        {p.placement_key}
                      </code>
                      {p.description && (
                        <div className="text-xs text-slate-400 mt-0.5">{p.description}</div>
                      )}
                    </td>

                    <td className="px-6 py-4 text-slate-700 font-medium">
                      {getSiteName(p.site_id)}
                    </td>

                    <td className="px-6 py-4">
                      <span className="text-xs font-semibold bg-slate-100 text-slate-700 px-2.5 py-1 rounded-full uppercase tracking-wider">
                        {p.format.replace('_', ' ')}
                      </span>
                      <div className="flex flex-wrap gap-1 mt-1.5">
                        {(p.allowed_source_types || []).map((source) => (
                          <span
                            key={source}
                            className="text-[10px] font-semibold uppercase tracking-wider bg-blue-50 text-blue-700 border border-blue-200 px-1.5 py-0.5 rounded"
                          >
                            {source}
                          </span>
                        ))}
                      </div>
                      <div className="text-[10px] text-slate-400 mt-1">
                        Max {p.max_items || 1} item{(p.max_items || 1) === 1 ? '' : 's'} · Disclosure:{' '}
                        {(p.disclosure_mode || 'inherited').replace(/_/g, ' ')}
                      </div>
                    </td>

                    <td className="px-6 py-4">
                      <div className="flex items-center gap-2">
                        <code className="text-[11px] font-mono bg-slate-900 text-emerald-400 px-2 py-1 rounded">
                          &lt;div data-adcontrol-placement="{p.placement_key}"&gt;&lt;/div&gt;
                        </code>
                        <button
                          onClick={() => copyMarkup(p.placement_key)}
                          className="text-slate-400 hover:text-slate-600 p-1 rounded"
                          title="Copy HTML container element"
                        >
                          {copiedKey === p.placement_key ? (
                            <Check className="w-4 h-4 text-emerald-600" />
                          ) : (
                            <Copy className="w-4 h-4" />
                          )}
                        </button>
                      </div>
                    </td>

                    <td className="px-6 py-4">
                      <span
                        className={`inline-flex items-center gap-1 text-xs font-semibold px-2.5 py-1 rounded-full ${
                          p.status === 'active'
                            ? 'bg-emerald-50 text-emerald-700 border border-emerald-200'
                            : 'bg-slate-100 text-slate-600'
                        }`}
                      >
                        {p.status}
                      </span>
                    </td>

                    <td className="px-6 py-4 text-right">
                      {canWrite && (
                        <div className="flex items-center justify-end gap-1.5">
                          <button
                            onClick={() => openEditModal(p)}
                            className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg transition-colors"
                            title="Edit Placement"
                          >
                            <Edit className="w-4 h-4" />
                          </button>
                          <button
                            onClick={() => handleDelete(p)}
                            className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition-colors"
                            title="Delete Placement"
                          >
                            <Trash2 className="w-4 h-4" />
                          </button>
                        </div>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Create Placement Modal */}
      <Modal
        isOpen={createModalOpen}
        onClose={() => setCreateModalOpen(false)}
        title="Create Placement Slot"
      >
        <form onSubmit={handleCreateSubmit} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Website
            </label>
            <select
              value={siteId}
              onChange={(e) => setSiteId(e.target.value)}
              required
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              {sites.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.name} ({s.primary_domain})
                </option>
              ))}
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Placement Name
            </label>
            <input
              type="text"
              required
              value={name}
              onChange={(e) => {
                setName(e.target.value);
                if (!placementKey) {
                  setPlacementKey(
                    e.target.value.toLowerCase().replace(/[^a-z0-9]/g, '-').replace(/-+/g, '-')
                  );
                }
              }}
              placeholder="e.g. Header Banner"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Placement Key (Permanent identifier in HTML)
            </label>
            <input
              type="text"
              required
              value={placementKey}
              onChange={(e) => setPlacementKey(e.target.value)}
              placeholder="e.g. header-banner"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              Used as <code className="text-blue-600">data-adcontrol-placement="{placementKey || 'key'}"</code> on your site.
            </p>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Ad Format
            </label>
            <select
              value={format}
              onChange={(e) => setFormat(e.target.value as PlacementFormat)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="responsive_banner">Responsive Banner (Flexible Image & Link)</option>
              <option value="native_card">Native Card (Title, Image, Text, CTA)</option>
              <option value="product_card">Affiliate Product Card (Disclosed)</option>
              <option value="text_link">Sponsored Text Link</option>
              <option value="image">Fixed Image Banner</option>
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed source types
            </label>
            <div className="flex flex-wrap gap-2">
              {PLACEMENT_SOURCE_OPTIONS.map((source) => (
                <label
                  key={source.value}
                  className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium cursor-pointer ${
                    allowedSourceTypes.includes(source.value)
                      ? 'border-blue-300 bg-blue-50 text-blue-800'
                      : 'border-slate-200 bg-slate-50 text-slate-600'
                  }`}
                >
                  <input
                    type="checkbox"
                    checked={allowedSourceTypes.includes(source.value)}
                    onChange={() => toggleSource(source.value)}
                  />
                  {source.label}
                </label>
              ))}
            </div>
            <p className="text-[11px] text-slate-400 mt-1">
              Affiliate product cards require both the site and placement to allow the affiliate source.
            </p>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Max items
              </label>
              <input
                type="number"
                min={1}
                max={20}
                value={maxItems}
                onChange={(e) => setMaxItems(Number(e.target.value))}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Disclosure mode
              </label>
              <select
                value={disclosureMode}
                onChange={(e) => setDisclosureMode(e.target.value as DisclosureMode)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="inherited">Inherited from site</option>
                <option value="required">Required</option>
                <option value="disabled_only_if_not_applicable">Only non-affiliate may omit</option>
              </select>
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Fallback Behavior (When no active ad is assigned)
            </label>
            <select
              value={fallbackBehavior}
              onChange={(e) => setFallbackBehavior(e.target.value as any)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="hide">Hide (Leave container empty without layout shift)</option>
              <option value="blank">Blank reservation</option>
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Notes / Description (Optional)
            </label>
            <input
              type="text"
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              placeholder="e.g. Appears above the fold across all article pages"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setCreateModalOpen(false)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg shadow-sm shadow-blue-500/20 disabled:opacity-50"
            >
              {submitting ? 'Creating...' : 'Create Placement'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Edit Placement Modal */}
      <Modal
        isOpen={!!editPlacement}
        onClose={() => setEditPlacement(null)}
        title={`Edit Placement: ${editPlacement?.name}`}
      >
        <form onSubmit={handleEditSubmit} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Placement Name
            </label>
            <input
              type="text"
              required
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Ad Format
            </label>
            <select
              value={format}
              onChange={(e) => setFormat(e.target.value as PlacementFormat)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="responsive_banner">Responsive Banner</option>
              <option value="native_card">Native Card</option>
              <option value="product_card">Affiliate Product Card</option>
              <option value="text_link">Sponsored Text Link</option>
              <option value="image">Fixed Image</option>
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed source types
            </label>
            <div className="flex flex-wrap gap-2">
              {PLACEMENT_SOURCE_OPTIONS.map((source) => (
                <label
                  key={source.value}
                  className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium cursor-pointer ${
                    allowedSourceTypes.includes(source.value)
                      ? 'border-blue-300 bg-blue-50 text-blue-800'
                      : 'border-slate-200 bg-slate-50 text-slate-600'
                  }`}
                >
                  <input
                    type="checkbox"
                    checked={allowedSourceTypes.includes(source.value)}
                    onChange={() => toggleSource(source.value)}
                  />
                  {source.label}
                </label>
              ))}
            </div>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Max items
              </label>
              <input
                type="number"
                min={1}
                max={20}
                value={maxItems}
                onChange={(e) => setMaxItems(Number(e.target.value))}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Disclosure mode
              </label>
              <select
                value={disclosureMode}
                onChange={(e) => setDisclosureMode(e.target.value as DisclosureMode)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="inherited">Inherited from site</option>
                <option value="required">Required</option>
                <option value="disabled_only_if_not_applicable">Only non-affiliate may omit</option>
              </select>
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Fallback Behavior
            </label>
            <select
              value={fallbackBehavior}
              onChange={(e) => setFallbackBehavior(e.target.value as any)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="hide">Hide</option>
              <option value="blank">Blank</option>
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Status
            </label>
            <select
              value={status}
              onChange={(e) => setStatus(e.target.value as any)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="active">Active</option>
              <option value="disabled">Disabled</option>
            </select>
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setEditPlacement(null)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg shadow-sm shadow-blue-500/20 disabled:opacity-50"
            >
              {submitting ? 'Saving...' : 'Save Changes'}
            </button>
          </div>
        </form>
      </Modal>
    </div>
  );
};
