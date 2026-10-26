import React, { useEffect, useMemo, useState } from 'react';
import { api } from '../api';
import { AdSourceType, AffiliateOffer, Site, Placement, Campaign, AdFormat } from '../types';
import { Modal } from '../components/Modal';
import { LiveAdPreview } from '../components/LiveAdPreview';
import {
  Plus,
  Play,
  Pause,
  Edit,
  Trash2,
  ExternalLink,
  Calendar,
  Megaphone,
  Search,
  Ticket,
  Lock,
} from 'lucide-react';

interface CampaignsViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  openCreateModalDirectly?: boolean;
  role: string;
}

export const CampaignsView: React.FC<CampaignsViewProps> = ({
  onNotify,
  openCreateModalDirectly = false,
  role,
}) => {
  const canWrite = role === 'admin' || role === 'publisher';
  const isAdmin = role === 'admin';
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [sites, setSites] = useState<Site[]>([]);
  const [placements, setPlacements] = useState<Placement[]>([]);
  const [offers, setOffers] = useState<AffiliateOffer[]>([]);
  const [loading, setLoading] = useState(true);

  // Filters
  const [siteFilter, setSiteFilter] = useState<string>('all');
  const [placementFilter, setPlacementFilter] = useState<string>('all');
  const [statusFilter, setStatusFilter] = useState<string>('all');
  const [searchQuery, setSearchQuery] = useState('');

  // Modals
  const [createModalOpen, setCreateModalOpen] = useState(openCreateModalDirectly);
  const [editCampaign, setEditCampaign] = useState<Campaign | null>(null);

  // Form states
  const [formSiteId, setFormSiteId] = useState('');
  const [formPlacementId, setFormPlacementId] = useState('');
  const [name, setName] = useState('');
  const [destinationUrl, setDestinationUrl] = useState('');
  const [format, setFormat] = useState<AdFormat>('responsive_banner');
  const [sourceType, setSourceType] = useState<AdSourceType>('house');
  const [affiliateOfferId, setAffiliateOfferId] = useState('');
  const [disclosureOverride, setDisclosureOverride] = useState('');
  const [title, setTitle] = useState('');
  const [body, setBody] = useState('');
  const [imageUrl, setImageUrl] = useState('');
  const [altText, setAltText] = useState('');
  const [ctaLabel, setCtaLabel] = useState('Learn More');
  const [status, setStatus] = useState<'draft' | 'active'>('active');
  const [startsAt, setStartsAt] = useState('');
  const [endsAt, setEndsAt] = useState('');
  const [priority, setPriority] = useState(1);
  const [weight, setWeight] = useState(100);
  const [submitting, setSubmitting] = useState(false);

  const loadData = async () => {
    try {
      setLoading(true);
      const [cRes, sRes, pRes, oRes] = await Promise.all([
        api.getCampaigns({
          site_id: siteFilter !== 'all' ? siteFilter : undefined,
          placement_id: placementFilter !== 'all' ? placementFilter : undefined,
          status: statusFilter,
        }),
        api.getSites(),
        api.getPlacements(),
        api.getAffiliateOffers().catch(() => [] as AffiliateOffer[]),
      ]);
      setCampaigns(cRes);
      setSites(sRes);
      setPlacements(pRes);
      setOffers(oRes);

      if (sRes.length > 0 && !formSiteId) {
        setFormSiteId(sRes[0].id);
      }
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load campaigns');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, [siteFilter, placementFilter, statusFilter]);

  // When formSiteId changes, update placement options
  const availablePlacements = placements.filter((p) => p.site_id === formSiteId);
  useEffect(() => {
    if (availablePlacements.length > 0 && (!formPlacementId || !availablePlacements.some(p => p.id === formPlacementId))) {
      setFormPlacementId(availablePlacements[0].id);
      setFormat(availablePlacements[0].format);
    }
  }, [formSiteId, placements]);

  const formSite = sites.find((site) => site.id === formSiteId);
  const allowedSiteSources = formSite?.allowed_ad_sources?.length
    ? formSite.allowed_ad_sources
    : (['house', 'direct', 'affiliate'] as AdSourceType[]);
  const sourceOptions = useMemo(
    () =>
      (isAdmin ? (['house', 'direct', 'affiliate'] as AdSourceType[]) : (['house', 'affiliate'] as AdSourceType[])).filter(
        (source) => allowedSiteSources.includes(source),
      ),
    [isAdmin, allowedSiteSources.join(',')],
  );
  const eligibleOffers = offers.filter((offer) => {
    if (!formSite?.publisher_id || offer.publisher_id !== formSite.publisher_id) return false;
    if (offer.status === 'rejected' || offer.status === 'archived') return false;
    if (offer.validation_status === 'rejected') return false;
    return true;
  });
  const selectedOffer = offers.find((offer) => offer.id === affiliateOfferId) || null;

  useEffect(() => {
    if (sourceOptions.length > 0 && !sourceOptions.includes(sourceType)) {
      setSourceType(sourceOptions[0]);
    }
  }, [sourceOptions, sourceType]);

  const applyAffiliateOffer = (offer: AffiliateOffer) => {
    setAffiliateOfferId(offer.id);
    setSourceType('affiliate');
    setFormat('product_card');
    setDestinationUrl(offer.affiliate_url);
    setTitle(offer.product_title);
    setBody(offer.product_description);
    setImageUrl(offer.product_image_url);
    setAltText(offer.product_title);
    setCtaLabel('View offer');
  };

  const validateCampaignForm = (): string | null => {
    if (!formSiteId || !formPlacementId || !name) return 'Website, placement, and name are required.';
    if (sourceType === 'affiliate') {
      if (!affiliateOfferId) return 'Select an affiliate offer for an affiliate campaign.';
      if (!selectedOffer) return 'The selected affiliate offer is no longer available.';
      if (format !== 'product_card') return 'Affiliate campaigns must use the product card format.';
      if (!title.trim()) return 'Product cards require a title.';
      if (!imageUrl) return 'Product cards require an image URL.';
      if (selectedOffer.publisher_id !== formSite?.publisher_id) {
        return 'Affiliate offers can only be used on sites owned by the same publisher.';
      }
    } else {
      if (!destinationUrl) return 'Destination URL is required.';
      const lower = destinationUrl.toLowerCase();
      if (lower.startsWith('javascript:') || lower.startsWith('data:') || lower.startsWith('file:') || lower.startsWith('vbscript:')) {
        return 'Unsafe destination URL scheme is prohibited.';
      }
      if (format === 'product_card') {
        if (!title.trim()) return 'Product cards require a title.';
        if (!imageUrl) return 'Product cards require an image URL.';
      }
    }
    if (disclosureOverride.length > 500) return 'Disclosure override must be 500 characters or fewer.';
    return null;
  };

  const campaignPayload = () => ({
    site_id: formSiteId,
    placement_id: formPlacementId,
    name,
    destination_url: sourceType === 'affiliate' ? selectedOffer?.affiliate_url || destinationUrl : destinationUrl,
    format,
    source_type: sourceType,
    affiliate_offer_id: sourceType === 'affiliate' ? affiliateOfferId : '',
    disclosure_override: disclosureOverride,
    title,
    body,
    image_url: imageUrl,
    alt_text: altText,
    cta_label: ctaLabel,
    starts_at: startsAt || undefined,
    ends_at: endsAt || undefined,
    priority: Number(priority),
    weight: Number(weight),
  });

  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const validationError = validateCampaignForm();
    if (validationError) {
      onNotify('error', validationError);
      return;
    }

    setSubmitting(true);
    try {
      const camp = await api.createCampaign({ ...campaignPayload(), status });

      onNotify('success', `Campaign "${camp.name}" created as ${camp.status}.`);
      setCreateModalOpen(false);
      resetForm();
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to create campaign');
    } finally {
      setSubmitting(false);
    }
  };

  const handleEditSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!editCampaign) return;

    const validationError = validateCampaignForm();
    if (validationError) {
      onNotify('error', validationError);
      return;
    }

    setSubmitting(true);
    try {
      const updated = await api.updateCampaign(editCampaign.id, {
        ...campaignPayload(),
        status: editCampaign.status, // preserve or toggle via buttons
      });

      onNotify('success', `Campaign "${updated.name}" updated.`);
      setEditCampaign(null);
      resetForm();
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update campaign');
    } finally {
      setSubmitting(false);
    }
  };

  const handleTogglePublish = async (c: Campaign) => {
    try {
      if (c.status === 'active') {
        await api.pauseCampaign(c.id);
        onNotify('info', `Campaign "${c.name}" paused.`);
      } else {
        await api.publishCampaign(c.id);
        onNotify('success', `Campaign "${c.name}" is now live!`);
      }
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Action failed');
    }
  };

  const handleDelete = async (c: Campaign) => {
    if (
      !window.confirm(
        `Delete campaign "${c.name}"? Campaigns with analytics history are archived instead of deleted so reports stay consistent.`,
      )
    ) {
      return;
    }
    try {
      const result = await api.deleteCampaign(c.id);
      if (result.status === 'archived') {
        onNotify('info', result.message || `Campaign "${c.name}" was archived to preserve analytics history.`);
      } else {
        onNotify('success', `Deleted campaign "${c.name}".`);
      }
      loadData();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to delete campaign');
    }
  };

  const openEdit = (c: Campaign) => {
    setEditCampaign(c);
    setFormSiteId(c.site_id);
    setFormPlacementId(c.placement_id);
    setName(c.name);
    setDestinationUrl(c.destination_url);
    setFormat(c.format);
    setSourceType(c.source_type || 'house');
    setAffiliateOfferId(c.affiliate_offer_id || '');
    setDisclosureOverride(c.disclosure_override || '');
    setTitle(c.title || '');
    setBody(c.body || '');
    setImageUrl(c.image_url || '');
    setAltText(c.alt_text || '');
    setCtaLabel(c.cta_label || 'Learn More');
    setStartsAt(c.starts_at || '');
    setEndsAt(c.ends_at || '');
    setPriority(c.priority);
    setWeight(c.weight);
  };

  const resetForm = () => {
    setName('');
    setDestinationUrl('https://');
    setFormat('responsive_banner');
    setSourceType('house');
    setAffiliateOfferId('');
    setDisclosureOverride('');
    setTitle('');
    setBody('');
    setImageUrl('');
    setAltText('');
    setCtaLabel('Learn More');
    setStatus('active');
    setStartsAt('');
    setEndsAt('');
    setPriority(1);
    setWeight(100);
  };

  const handleSourceChange = (next: AdSourceType) => {
    setSourceType(next);
    if (next === 'affiliate') {
      setFormat('product_card');
      if (!affiliateOfferId && eligibleOffers.length > 0) {
        applyAffiliateOffer(eligibleOffers[0]);
      }
    } else if (format === 'product_card') {
      const placementFormat = placements.find((p) => p.id === formPlacementId)?.format;
      setFormat((placementFormat as AdFormat) || 'responsive_banner');
    }
  };

  const sourceBadgeClass = (source: AdSourceType) =>
    source === 'affiliate'
      ? 'bg-emerald-50 text-emerald-700 border-emerald-200'
      : source === 'direct'
      ? 'bg-violet-50 text-violet-700 border-violet-200'
      : 'bg-sky-50 text-sky-700 border-sky-200';

  const getSiteName = (id: string) => sites.find((s) => s.id === id)?.name || id;
  const getPlacementKey = (id: string) => placements.find((p) => p.id === id)?.placement_key || id;

  const filteredCampaigns = campaigns.filter((c) => {
    if (!searchQuery) return true;
    const q = searchQuery.toLowerCase();
    return (
      c.name.toLowerCase().includes(q) ||
      c.destination_url.toLowerCase().includes(q) ||
      (c.title && c.title.toLowerCase().includes(q))
    );
  });

  return (
    <div className="flex flex-col gap-6">
      {/* Header & Controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Ads & Campaigns</h2>
          <p className="text-sm text-slate-500">
            Centrally schedule advertisements, update creative assets, and redirect URLs without redeploying sites
          </p>
        </div>

        {canWrite ? (
          <button
            onClick={() => {
              resetForm();
              setCreateModalOpen(true);
            }}
            disabled={placements.length === 0}
            className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all self-start disabled:opacity-50"
          >
            <Plus className="w-4 h-4" />
            <span>Create New Ad</span>
          </button>
        ) : (
          <div className="flex items-center gap-1.5 text-xs font-medium text-slate-500 bg-slate-100 px-3 py-2 rounded-lg border border-slate-200 self-start">
            <Lock className="w-3.5 h-3.5" />
            Analyst access is read-only
          </div>
        )}
      </div>

      {/* Filter Toolbar */}
      <div className="bg-white p-4 rounded-xl border border-slate-200 shadow-sm flex flex-wrap items-center justify-between gap-3">
        <div className="flex flex-wrap items-center gap-3">
          {/* Search */}
          <div className="relative min-w-[200px]">
            <Search className="w-4 h-4 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
            <input
              type="text"
              placeholder="Search ads or URLs..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full pl-9 pr-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs text-slate-900 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Website Filter */}
          <select
            value={siteFilter}
            onChange={(e) => setSiteFilter(e.target.value)}
            className="px-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs font-medium text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
          >
            <option value="all">All Websites</option>
            {sites.map((s) => (
              <option key={s.id} value={s.id}>
                {s.name}
              </option>
            ))}
          </select>

          {/* Placement Filter */}
          <select
            value={placementFilter}
            onChange={(e) => setPlacementFilter(e.target.value)}
            className="px-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs font-medium text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
          >
            <option value="all">All Placements</option>
            {placements
              .filter((p) => siteFilter === 'all' || p.site_id === siteFilter)
              .map((p) => (
                <option key={p.id} value={p.id}>
                  {p.placement_key}
                </option>
              ))}
          </select>

          {/* Status Filter */}
          <select
            value={statusFilter}
            onChange={(e) => setStatusFilter(e.target.value)}
            className="px-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs font-medium text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
          >
            <option value="all">All Statuses</option>
            <option value="active">Active (Delivering)</option>
            <option value="paused">Paused</option>
            <option value="draft">Draft</option>
            <option value="scheduled">Scheduled</option>
            <option value="expired">Expired</option>
            <option value="archived">Archived</option>
          </select>
        </div>

        <div className="text-xs text-slate-400">
          Showing {filteredCampaigns.length} of {campaigns.length} campaigns
        </div>
      </div>

      {/* Campaigns Table */}
      <div className="bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden">
        {loading ? (
          <div className="p-12 text-center text-slate-500">Loading advertising campaigns...</div>
        ) : filteredCampaigns.length === 0 ? (
          <div className="p-12 text-center text-slate-500 flex flex-col items-center gap-3">
            <Megaphone className="w-10 h-10 text-slate-300" />
            <div>
              <div className="font-semibold text-slate-700">No campaigns found</div>
              <div className="text-xs text-slate-400 mt-1">
                {placements.length === 0
                  ? 'First create a placement before publishing an ad.'
                  : 'Create an ad to start delivering creatives to your websites.'}
              </div>
            </div>
            {canWrite && placements.length > 0 && (
              <button
                onClick={() => {
                  resetForm();
                  setCreateModalOpen(true);
                }}
                className="mt-2 text-xs font-semibold text-blue-600 bg-blue-50 px-3 py-1.5 rounded-lg border border-blue-200"
              >
                Create Ad Now
              </button>
            )}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-6 py-3.5">Ad Name & Creative</th>
                  <th className="px-6 py-3.5">Website / Placement</th>
                  <th className="px-6 py-3.5">Destination URL</th>
                  <th className="px-6 py-3.5">Schedule</th>
                  <th className="px-6 py-3.5">Status</th>
                  <th className="px-6 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {filteredCampaigns.map((c) => (
                  <tr key={c.id} className="hover:bg-slate-50/60 transition-colors">
                    <td className="px-6 py-4">
                      <div className="flex items-center gap-3">
                        {c.image_url ? (
                          <img
                            src={c.image_url}
                            alt=""
                            className="w-12 h-10 rounded object-cover bg-slate-100 border border-slate-200 shrink-0"
                            onError={(e) => {
                              (e.target as HTMLElement).style.display = 'none';
                            }}
                          />
                        ) : (
                          <div className="w-12 h-10 rounded bg-slate-100 border border-slate-200 flex items-center justify-center text-slate-400 shrink-0">
                            <Megaphone className="w-4 h-4" />
                          </div>
                        )}
                        <div>
                          <div className="font-semibold text-slate-900">{c.name}</div>
                          {c.title && <div className="text-xs text-slate-600">{c.title}</div>}
                          <div className="flex flex-wrap items-center gap-1.5 mt-1">
                            <span className="text-[10px] font-semibold text-slate-400 uppercase tracking-wider">
                              {c.format.replace(/_/g, ' ')}
                            </span>
                            <span
                              className={`text-[10px] font-semibold uppercase tracking-wider px-1.5 py-0.5 rounded border ${sourceBadgeClass(
                                c.source_type || 'house',
                              )}`}
                            >
                              {c.source_type || 'house'}
                            </span>
                          </div>
                        </div>
                      </div>
                    </td>

                    <td className="px-6 py-4">
                      <div className="font-medium text-slate-800 text-xs">{getSiteName(c.site_id)}</div>
                      <code className="text-[11px] font-mono text-blue-700 bg-blue-50 px-1.5 py-0.5 rounded border border-blue-200/50 mt-1 inline-block">
                        {getPlacementKey(c.placement_id)}
                      </code>
                    </td>

                    <td className="px-6 py-4 max-w-xs">
                      {c.source_type === 'affiliate' ? (
                        <div>
                          <div className="flex items-center gap-1 text-[11px] font-semibold text-emerald-700">
                            <Ticket className="w-3.5 h-3.5" />
                            <span>{offers.find((o) => o.id === c.affiliate_offer_id)?.product_title || 'Affiliate offer'}</span>
                          </div>
                          <div className="text-[10px] text-slate-400 mt-0.5">
                            Served via first-party redirect · exact URL stored
                          </div>
                        </div>
                      ) : (
                        <a
                          href={c.destination_url}
                          target="_blank"
                          rel="noopener noreferrer"
                          className="text-xs text-blue-600 hover:text-blue-800 flex items-center gap-1 font-mono truncate"
                          title={c.destination_url}
                        >
                          <span className="truncate">{c.destination_url}</span>
                          <ExternalLink className="w-3 h-3 shrink-0" />
                        </a>
                      )}
                    </td>

                    <td className="px-6 py-4 text-xs text-slate-500">
                      {c.starts_at || c.ends_at ? (
                        <div className="flex items-center gap-1">
                          <Calendar className="w-3.5 h-3.5 text-slate-400" />
                          <span>
                            {c.starts_at ? c.starts_at.slice(0, 10) : 'Now'} →{' '}
                            {c.ends_at ? c.ends_at.slice(0, 10) : 'Open'}
                          </span>
                        </div>
                      ) : (
                        <span className="text-slate-400">Always active</span>
                      )}
                    </td>

                    <td className="px-6 py-4">
                      <span
                        className={`inline-flex items-center gap-1.5 text-xs font-semibold px-2.5 py-1 rounded-full ${
                          c.status === 'active'
                            ? 'bg-emerald-50 text-emerald-700 border border-emerald-200'
                            : c.status === 'paused'
                            ? 'bg-amber-50 text-amber-700 border border-amber-200'
                            : 'bg-slate-100 text-slate-600'
                        }`}
                      >
                        <span
                          className={`w-1.5 h-1.5 rounded-full ${
                            c.status === 'active'
                              ? 'bg-emerald-500'
                              : c.status === 'paused'
                              ? 'bg-amber-500'
                              : 'bg-slate-400'
                          }`}
                        />
                        {c.status}
                      </span>
                    </td>

                    <td className="px-6 py-4 text-right">
                      {canWrite ? (
                        <div className="flex items-center justify-end gap-1.5">
                          {/* Publish / Pause Toggle */}
                          <button
                            onClick={() => handleTogglePublish(c)}
                            className={`px-2.5 py-1 rounded-lg text-xs font-semibold flex items-center gap-1 transition-colors ${
                              c.status === 'active'
                                ? 'bg-amber-50 text-amber-700 hover:bg-amber-100 border border-amber-200'
                                : 'bg-emerald-50 text-emerald-700 hover:bg-emerald-100 border border-emerald-200'
                            }`}
                            title={c.status === 'active' ? 'Pause Campaign' : 'Publish Campaign Live'}
                          >
                            {c.status === 'active' ? (
                              <>
                                <Pause className="w-3.5 h-3.5" />
                                <span>Pause</span>
                              </>
                            ) : (
                              <>
                                <Play className="w-3.5 h-3.5" />
                                <span>Publish</span>
                              </>
                            )}
                          </button>

                          <button
                            onClick={() => openEdit(c)}
                            className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg transition-colors"
                            title="Edit Campaign & Creative"
                          >
                            <Edit className="w-4 h-4" />
                          </button>

                          <button
                            onClick={() => handleDelete(c)}
                            className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition-colors"
                            title="Delete Campaign"
                          >
                            <Trash2 className="w-4 h-4" />
                          </button>
                        </div>
                      ) : (
                        <span className="inline-flex items-center gap-1 text-[11px] font-medium text-slate-400">
                          <Lock className="w-3 h-3" />
                          Read-only
                        </span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Create / Edit Campaign Modal with Live Preview */}
      <Modal
        isOpen={createModalOpen || !!editCampaign}
        onClose={() => {
          setCreateModalOpen(false);
          setEditCampaign(null);
        }}
        title={editCampaign ? `Edit Ad: ${editCampaign.name}` : 'Create New Ad Campaign'}
        maxWidth="max-w-4xl"
      >
        <form
          onSubmit={editCampaign ? handleEditSubmit : handleCreateSubmit}
          className="grid grid-cols-1 md:grid-cols-2 gap-6"
        >
          {/* Left Column: Form Fields */}
          <div className="flex flex-col gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Target Website
              </label>
              <select
                value={formSiteId}
                onChange={(e) => setFormSiteId(e.target.value)}
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
                Target Placement Slot
              </label>
              <select
                value={formPlacementId}
                onChange={(e) => {
                  setFormPlacementId(e.target.value);
                  const p = placements.find((x) => x.id === e.target.value);
                  if (p) setFormat(sourceType === 'affiliate' ? 'product_card' : p.format);
                }}
                required
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                {availablePlacements.map((p) => (
                  <option key={p.id} value={p.id}>
                    {p.name} ({p.placement_key}) — {p.format}
                  </option>
                ))}
              </select>
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Ad Source
              </label>
              <select
                value={sourceType}
                onChange={(e) => handleSourceChange(e.target.value as AdSourceType)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                {sourceOptions.map((source) => (
                  <option key={source} value={source}>
                    {source === 'house'
                      ? 'House ad — platform promotion'
                      : source === 'direct'
                      ? 'Direct ad — admin-managed advertiser'
                      : 'Affiliate offer — disclosed monetized link'}
                  </option>
                ))}
              </select>
              {!isAdmin && (
                <p className="text-[10px] text-slate-400 mt-1">
                  Direct advertiser campaigns are provisioned by an AdControl administrator.
                </p>
              )}
            </div>

            {sourceType === 'affiliate' && (
              <div className="rounded-lg border border-emerald-200 bg-emerald-50/60 p-3 flex flex-col gap-2">
                <label className="block text-xs font-semibold text-emerald-800 uppercase tracking-wider">
                  Affiliate Offer
                </label>
                {eligibleOffers.length === 0 ? (
                  <p className="text-xs text-amber-700">
                    No eligible offers for this site's publisher. Create and validate an offer on the Affiliate Offers
                    page first; offers must belong to the same publisher as the site.
                  </p>
                ) : (
                  <select
                    value={affiliateOfferId}
                    onChange={(e) => {
                      const offer = eligibleOffers.find((o) => o.id === e.target.value);
                      if (offer) applyAffiliateOffer(offer);
                      else setAffiliateOfferId(e.target.value);
                    }}
                    className="w-full px-3 py-2 bg-white border border-emerald-200 rounded-lg text-sm text-slate-900 focus:outline-none focus:ring-2 focus:ring-emerald-500"
                  >
                    <option value="">Select an offer...</option>
                    {eligibleOffers.map((offer) => (
                      <option key={offer.id} value={offer.id}>
                        {offer.offer_name} — {offer.provider} ({offer.validation_status})
                      </option>
                    ))}
                  </select>
                )}

                {selectedOffer && (
                  <div className="text-[11px] text-emerald-900/80 space-y-1">
                    <div>
                      Merchant: <span className="font-semibold">{selectedOffer.merchant_name || 'Not specified'}</span>
                      {selectedOffer.publisher_id !== formSite?.publisher_id && (
                        <span className="text-rose-700 font-semibold"> · Different publisher</span>
                      )}
                    </div>
                    <div className="font-mono break-all text-emerald-800/80">{selectedOffer.affiliate_url}</div>
                    <div>
                      Validation: <span className="font-semibold">{selectedOffer.validation_status}</span> · URL preserved
                      exactly; tracking parameters are never injected or reordered.
                    </div>
                    {!selectedOffer.product_image_url && (
                      <div className="text-rose-700 font-semibold">
                        This offer has no product image. Add one to the offer before publishing a product card.
                      </div>
                    )}
                  </div>
                )}
              </div>
            )}

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Internal Campaign Name
              </label>
              <input
                type="text"
                required
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="e.g. Q4 Black Friday Banner"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Destination URL (Where visitors land on click)
              </label>
              {sourceType === 'affiliate' ? (
                <>
                  <input
                    type="text"
                    readOnly
                    value={selectedOffer?.affiliate_url || ''}
                    placeholder="Select an affiliate offer above"
                    className="w-full px-3 py-2 bg-emerald-50/40 border border-emerald-200 rounded-lg text-sm text-slate-700 font-mono cursor-not-allowed"
                  />
                  <p className="text-[10px] text-slate-400 mt-1">
                    Destination comes from the offer and is served through a signed first-party redirect. It is stored
                    and served exactly as entered.
                  </p>
                </>
              ) : (
                <input
                  type="url"
                  required
                  value={destinationUrl}
                  onChange={(e) => setDestinationUrl(e.target.value)}
                  placeholder="https://example.com/landing-page"
                  className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500 font-mono"
                />
              )}
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div>
                <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                  {sourceType === 'affiliate' ? 'Product Title (required)' : 'Headline / Title'}
                </label>
                <input
                  type="text"
                  required={sourceType === 'affiliate'}
                  value={title}
                  onChange={(e) => setTitle(e.target.value)}
                  placeholder="e.g. 50% Off Everything"
                  className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>

              <div>
                <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                  CTA Button Label
                </label>
                <input
                  type="text"
                  value={ctaLabel}
                  onChange={(e) => setCtaLabel(e.target.value)}
                  placeholder="e.g. Claim Offer"
                  className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                {sourceType === 'affiliate' ? 'Product Image URL (HTTPS, required)' : 'Creative Image URL (HTTPS)'}
              </label>
              <input
                type="url"
                required={sourceType === 'affiliate'}
                value={imageUrl}
                onChange={(e) => setImageUrl(e.target.value)}
                placeholder="https://example.com/banners/728x90.jpg"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500 font-mono"
              />
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Body Copy / Description
              </label>
              <textarea
                rows={2}
                value={body}
                onChange={(e) => setBody(e.target.value)}
                placeholder="Brief copy promoting your product or service..."
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Disclosure Text Override
              </label>
              <textarea
                rows={2}
                value={disclosureOverride}
                onChange={(e) => setDisclosureOverride(e.target.value)}
                maxLength={500}
                placeholder="Leave blank to inherit the website's disclosure copy"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <p className="text-[10px] text-slate-400 mt-1">
                Affiliate product cards always show a visible disclosure. Leave blank to inherit the website default;
                the server enforces mandatory disclosure at delivery.
              </p>
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div>
                <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                  Schedule Start (UTC)
                </label>
                <input
                  type="datetime-local"
                  value={startsAt ? startsAt.slice(0, 16) : ''}
                  onChange={(e) => setStartsAt(e.target.value ? e.target.value + ':00Z' : '')}
                  className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-xs text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>

              <div>
                <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                  Schedule End (UTC)
                </label>
                <input
                  type="datetime-local"
                  value={endsAt ? endsAt.slice(0, 16) : ''}
                  onChange={(e) => setEndsAt(e.target.value ? e.target.value + ':00Z' : '')}
                  className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-xs text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
            </div>
          </div>

          {/* Right Column: Live Interactive Preview */}
          <div className="flex flex-col justify-between gap-4">
            <div>
              <div className="text-xs font-semibold text-slate-700 uppercase tracking-wider mb-2">
                Real-Time Ad Rendering
              </div>
              <LiveAdPreview
                format={format}
                title={title || 'Sample Creative Title'}
                body={body || 'Sample descriptive text preview'}
                imageUrl={imageUrl}
                destinationUrl={
                  sourceType === 'affiliate' ? selectedOffer?.affiliate_url || '' : destinationUrl || 'https://example.com'
                }
                ctaLabel={ctaLabel || 'Learn More'}
                altText={altText || name}
                merchant={sourceType === 'affiliate' ? selectedOffer?.merchant_name : undefined}
                priceText={sourceType === 'affiliate' ? selectedOffer?.price_text : undefined}
                disclosureText={
                  disclosureOverride ||
                  formSite?.default_disclosure_text ||
                  'Affiliate disclosure: we may earn a commission from purchases made through this link.'
                }
              />
            </div>

            <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
              <button
                type="button"
                onClick={() => {
                  setCreateModalOpen(false);
                  setEditCampaign(null);
                }}
                className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
              >
                Cancel
              </button>
              <button
                type="submit"
                disabled={submitting}
                className="px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg shadow-sm shadow-blue-500/20 disabled:opacity-50"
              >
                {submitting
                  ? 'Saving...'
                  : editCampaign
                  ? 'Save Ad Changes'
                  : 'Publish / Schedule Ad'}
              </button>
            </div>
          </div>
        </form>
      </Modal>
    </div>
  );
};
