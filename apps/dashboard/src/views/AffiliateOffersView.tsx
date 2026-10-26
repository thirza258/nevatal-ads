import React, { useEffect, useMemo, useState } from 'react';
import { api } from '../api';
import { AffiliateOffer, Campaign, Placement, Publisher, Site } from '../types';
import { Modal } from '../components/Modal';
import {
  AlertTriangle,
  Check,
  CheckCircle2,
  Copy,
  ExternalLink,
  Loader2,
  Pause,
  Play,
  Plus,
  Search,
  ShieldCheck,
  Ticket,
  Trash2,
  Edit,
} from 'lucide-react';

interface AffiliateOffersViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  role: string;
}

type OfferStatus = 'draft' | 'active' | 'paused';
type Provider = 'shopee_affiliate' | 'manual_affiliate';

const STATUS_STYLES: Record<string, string> = {
  active: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  draft: 'bg-slate-100 text-slate-600 border-slate-200',
  paused: 'bg-amber-50 text-amber-700 border-amber-200',
  expired: 'bg-rose-50 text-rose-700 border-rose-200',
  rejected: 'bg-rose-50 text-rose-700 border-rose-200',
  archived: 'bg-slate-100 text-slate-500 border-slate-200',
};

const VALIDATION_STYLES: Record<string, string> = {
  valid: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  unverified: 'bg-slate-100 text-slate-600 border-slate-200',
  rejected: 'bg-rose-50 text-rose-700 border-rose-200',
};

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

/**
 * Validate the affiliate URL without rewriting it. The API stores and serves
 * this value byte-for-byte, so the client must never trim or reorder query
 * parameters. The server remains authoritative.
 */
function validateAffiliateUrlInput(value: string, label: string): string | null {
  if (!value) return `${label} is required.`;
  if (value !== value.trim()) return `${label} must not contain leading or trailing whitespace.`;
  if (!value.startsWith('https://')) return `${label} must begin with https:// exactly.`;
  if (/[\u0000-\u001f\u007f]/.test(value)) return `${label} contains control characters.`;
  try {
    const parsed = new URL(value);
    if (parsed.protocol !== 'https:') return `${label} must use HTTPS.`;
    if (parsed.username || parsed.password) return `${label} must not contain user information.`;
    if (!parsed.hostname.includes('.')) return `${label} must use a public hostname.`;
  } catch (_) {
    return `${label} is not a valid URL.`;
  }
  return null;
}

function toIsoUtc(local: string): string {
  return local ? `${local}:00Z` : '';
}

export const AffiliateOffersView: React.FC<AffiliateOffersViewProps> = ({ onNotify, role }) => {
  const canWrite = role === 'admin' || role === 'publisher';
  const isAdmin = role === 'admin';

  const [offers, setOffers] = useState<AffiliateOffer[]>([]);
  const [sites, setSites] = useState<Site[]>([]);
  const [placements, setPlacements] = useState<Placement[]>([]);
  const [publishers, setPublishers] = useState<Publisher[]>([]);
  const [loading, setLoading] = useState(true);

  const [statusFilter, setStatusFilter] = useState('all');
  const [publisherFilter, setPublisherFilter] = useState('all');
  const [search, setSearch] = useState('');
  const [copiedId, setCopiedId] = useState<string | null>(null);

  const [createOpen, setCreateOpen] = useState(false);
  const [editOffer, setEditOffer] = useState<AffiliateOffer | null>(null);
  const [placeOffer, setPlaceOffer] = useState<AffiliateOffer | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [placing, setPlacing] = useState(false);

  // Offer form
  const [publisherId, setPublisherId] = useState('');
  const [provider, setProvider] = useState<Provider>('shopee_affiliate');
  const [offerName, setOfferName] = useState('');
  const [productTitle, setProductTitle] = useState('');
  const [productImageUrl, setProductImageUrl] = useState('');
  const [productDescription, setProductDescription] = useState('');
  const [merchantName, setMerchantName] = useState('');
  const [affiliateUrl, setAffiliateUrl] = useState('');
  const [canonicalProductUrl, setCanonicalProductUrl] = useState('');
  const [priceText, setPriceText] = useState('');
  const [currency, setCurrency] = useState('IDR');
  const [availabilityCheckedAt, setAvailabilityCheckedAt] = useState('');
  const [disclosureText, setDisclosureText] = useState('');
  const [offerStatus, setOfferStatus] = useState<OfferStatus>('draft');

  // Place form
  const [placePlacementId, setPlacePlacementId] = useState('');
  const [placeCampaignName, setPlaceCampaignName] = useState('');
  const [placeCampaignStatus, setPlaceCampaignStatus] = useState<'draft' | 'active' | 'scheduled'>('draft');
  const [placeStartsAt, setPlaceStartsAt] = useState('');
  const [placeEndsAt, setPlaceEndsAt] = useState('');
  const [placePriority, setPlacePriority] = useState(1);
  const [placeWeight, setPlaceWeight] = useState(100);

  const load = async () => {
    try {
      setLoading(true);
      const [offerList, siteList, placementList] = await Promise.all([
        api.getAffiliateOffers({
          status: statusFilter !== 'all' ? statusFilter : undefined,
          publisher_id: isAdmin && publisherFilter !== 'all' ? publisherFilter : undefined,
        }),
        api.getSites(),
        api.getPlacements(),
      ]);
      setOffers(offerList);
      setSites(siteList);
      setPlacements(placementList);
      if (isAdmin) {
        setPublishers(await api.getPublishers());
      }
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load affiliate offers');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [statusFilter, publisherFilter]);

  useEffect(() => {
    if (!isAdmin || publisherFilter !== 'all') return;
    if (publisherId === '' && publishers.length > 0) setPublisherId(publishers[0].id);
  }, [isAdmin, publisherFilter, publisherId, publishers]);

  const sitesById = useMemo(() => new Map(sites.map((site) => [site.id, site])), [sites]);
  const placementsById = useMemo(
    () => new Map(placements.map((placement) => [placement.id, placement])),
    [placements],
  );
  const publishersById = useMemo(
    () => new Map(publishers.map((publisher) => [publisher.id, publisher])),
    [publishers],
  );

  const filtered = useMemo(() => {
    const query = search.trim().toLowerCase();
    if (!query) return offers;
    return offers.filter(
      (offer) =>
        offer.offer_name.toLowerCase().includes(query) ||
        offer.product_title.toLowerCase().includes(query) ||
        offer.merchant_name.toLowerCase().includes(query) ||
        offer.affiliate_url.toLowerCase().includes(query),
    );
  }, [offers, search]);

  const stats = useMemo(
    () => ({
      total: offers.length,
      active: offers.filter((offer) => offer.status === 'active').length,
      unverified: offers.filter((offer) => offer.validation_status !== 'valid').length,
    }),
    [offers],
  );

  const resetForm = () => {
    setPublisherId(isAdmin && publisherFilter !== 'all' ? publisherFilter : publishers[0]?.id || '');
    setProvider('shopee_affiliate');
    setOfferName('');
    setProductTitle('');
    setProductImageUrl('');
    setProductDescription('');
    setMerchantName('');
    setAffiliateUrl('');
    setCanonicalProductUrl('');
    setPriceText('');
    setCurrency('IDR');
    setAvailabilityCheckedAt('');
    setDisclosureText('');
    setOfferStatus('draft');
  };

  const openCreate = () => {
    resetForm();
    setCreateOpen(true);
  };

  const openEdit = (offer: AffiliateOffer) => {
    setEditOffer(offer);
    setPublisherId(offer.publisher_id);
    setProvider((offer.provider === 'manual_affiliate' ? 'manual_affiliate' : 'shopee_affiliate'));
    setOfferName(offer.offer_name);
    setProductTitle(offer.product_title);
    setProductImageUrl(offer.product_image_url);
    setProductDescription(offer.product_description);
    setMerchantName(offer.merchant_name);
    setAffiliateUrl(offer.affiliate_url);
    setCanonicalProductUrl(offer.canonical_product_url);
    setPriceText(offer.price_text);
    setCurrency(offer.currency || 'IDR');
    setAvailabilityCheckedAt(offer.availability_checked_at);
    setDisclosureText(offer.disclosure_text);
    setOfferStatus(
      offer.status === 'active' || offer.status === 'paused' ? offer.status : 'draft',
    );
  };

  const validateForm = (): string | null => {
    if (isAdmin && !editOffer && !publisherId) return 'Select the publisher that owns this offer.';
    if (!offerName.trim()) return 'Offer name is required.';
    if (!productTitle.trim()) return 'Product title is required.';
    if (!productImageUrl) return 'Product image URL is required so the product card can render.';
    if (!/^https?:\/\//i.test(productImageUrl)) return 'Product image URL must use http:// or https://.';
    const affiliateError = validateAffiliateUrlInput(affiliateUrl, 'Affiliate URL');
    if (affiliateError) return affiliateError;
    if (canonicalProductUrl) {
      const canonicalError = validateAffiliateUrlInput(canonicalProductUrl, 'Canonical product URL');
      if (canonicalError) return canonicalError;
    }
    if (currency && !/^[A-Z]{3}$/.test(currency)) {
      return 'Currency must be a three-letter uppercase code, for example IDR.';
    }
    if (priceText.length > 50) return 'Price text must be 50 characters or fewer.';
    return null;
  };

  const buildPayload = () => ({
    provider,
    offer_name: offerName,
    product_title: productTitle,
    product_image_url: productImageUrl,
    product_description: productDescription,
    merchant_name: merchantName,
    affiliate_url: affiliateUrl,
    canonical_product_url: canonicalProductUrl,
    price_text: priceText,
    currency,
    availability_checked_at: availabilityCheckedAt,
    disclosure_text: disclosureText,
    status: offerStatus,
  });

  const handleCreate = async (event: React.FormEvent) => {
    event.preventDefault();
    const error = validateForm();
    if (error) {
      onNotify('error', error);
      return;
    }
    setSubmitting(true);
    try {
      const payload = buildPayload();
      const created = await api.createAffiliateOffer(
        isAdmin ? { ...payload, publisher_id: publisherId } : payload,
      );
      onNotify('success', `Affiliate offer "${created.offer_name}" created as ${created.status}.`);
      setCreateOpen(false);
      resetForm();
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to create affiliate offer');
    } finally {
      setSubmitting(false);
    }
  };

  const handleEdit = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!editOffer) return;
    const error = validateForm();
    if (error) {
      onNotify('error', error);
      return;
    }
    setSubmitting(true);
    try {
      const updated = await api.updateAffiliateOffer(editOffer.id, buildPayload());
      onNotify('success', `Affiliate offer "${updated.offer_name}" updated.`);
      setEditOffer(null);
      resetForm();
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update affiliate offer');
    } finally {
      setSubmitting(false);
    }
  };

  const handleValidate = async (offer: AffiliateOffer) => {
    try {
      const updated = await api.validateAffiliateOffer(offer.id);
      onNotify('success', `Structural validation passed for "${updated.offer_name}".`);
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Validation failed');
      load();
    }
  };

  const handleTogglePublish = async (offer: AffiliateOffer) => {
    try {
      if (offer.status === 'active') {
        await api.pauseAffiliateOffer(offer.id);
        onNotify('info', `Offer "${offer.offer_name}" paused. Campaigns using it stop serving affiliate clicks.`);
      } else {
        await api.publishAffiliateOffer(offer.id);
        onNotify('success', `Offer "${offer.offer_name}" is active.`);
      }
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Action failed');
    }
  };

  const handleDelete = async (offer: AffiliateOffer) => {
    if (
      !window.confirm(
        `Delete affiliate offer "${offer.offer_name}"? This is blocked while any campaign still references it.`,
      )
    ) {
      return;
    }
    try {
      await api.deleteAffiliateOffer(offer.id);
      onNotify('success', `Deleted affiliate offer "${offer.offer_name}".`);
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to delete affiliate offer');
    }
  };

  const openPlace = (offer: AffiliateOffer) => {
    const candidate = placements.find((placement) => {
      const site = sitesById.get(placement.site_id);
      return (
        site &&
        site.publisher_id === offer.publisher_id &&
        placement.allowed_source_types.includes('affiliate') &&
        site.allowed_ad_sources.includes('affiliate')
      );
    });
    setPlaceOffer(offer);
    setPlacePlacementId(candidate?.id || '');
    setPlaceCampaignName(offer.offer_name);
    setPlaceCampaignStatus('draft');
    setPlaceStartsAt('');
    setPlaceEndsAt('');
    setPlacePriority(1);
    setPlaceWeight(100);
  };

  const placePlacements = useMemo(() => {
    if (!placeOffer) return [];
    return placements.filter((placement) => {
      const site = sitesById.get(placement.site_id);
      return (
        site &&
        site.publisher_id === placeOffer.publisher_id &&
        placement.allowed_source_types.includes('affiliate') &&
        site.allowed_ad_sources.includes('affiliate')
      );
    });
  }, [placeOffer, placements, sitesById]);

  const handlePlace = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!placeOffer) return;
    if (!placePlacementId) {
      onNotify('error', 'Select an affiliate-enabled placement.');
      return;
    }
    setPlacing(true);
    try {
      const campaign: Campaign = await api.placeAffiliateOffer(placeOffer.id, {
        placement_id: placePlacementId,
        name: placeCampaignName,
        status: placeCampaignStatus,
        starts_at: toIsoUtc(placeStartsAt),
        ends_at: toIsoUtc(placeEndsAt),
        priority: Number(placePriority),
        weight: Number(placeWeight),
      });
      onNotify('success', `Product card "${campaign.name}" created as ${campaign.status}.`);
      setPlaceOffer(null);
      load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to place affiliate offer');
    } finally {
      setPlacing(false);
    }
  };

  const copyUrl = async (offer: AffiliateOffer) => {
    try {
      await navigator.clipboard.writeText(offer.affiliate_url);
      setCopiedId(offer.id);
      window.setTimeout(() => setCopiedId(null), 1600);
      onNotify('info', 'Affiliate URL copied exactly as stored.');
    } catch (_) {
      onNotify('error', 'Clipboard access was blocked by the browser.');
    }
  };

  const selectedPlacement = placePlacementId ? placementsById.get(placePlacementId) : undefined;
  const selectedPlacementSite = selectedPlacement ? sitesById.get(selectedPlacement.site_id) : undefined;

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Affiliate Offers</h2>
          <p className="text-sm text-slate-500 mt-1">
            Manually entered Shopee or affiliate links served through a first-party, signed redirect. AdControl
            observes clicks and outbound redirects; it does not claim externally confirmed conversions or
            commissions.
          </p>
        </div>
        {canWrite && (
          <button
            onClick={openCreate}
            disabled={isAdmin && publishers.length === 0}
            className="inline-flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all self-start"
          >
            <Plus className="w-4 h-4" /> New Affiliate Offer
          </button>
        )}
      </div>

      {!canWrite && (
        <div className="rounded-lg border border-slate-200 bg-slate-50 px-4 py-3 text-xs text-slate-600">
          Read-only analyst view. Offer destinations are visible to authorized analysts but cannot be changed.
        </div>
      )}

      <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Offers visible</div>
          <div className="mt-1 text-2xl font-bold text-slate-900">{stats.total}</div>
        </div>
        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Active offers</div>
          <div className="mt-1 text-2xl font-bold text-emerald-700">{stats.active}</div>
        </div>
        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
            Not structurally validated
          </div>
          <div className="mt-1 text-2xl font-bold text-amber-700">{stats.unverified}</div>
        </div>
      </div>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
        <div className="p-4 border-b border-slate-100 flex flex-wrap items-center gap-3">
          <div className="relative min-w-[220px] flex-1 sm:flex-none">
            <Search className="w-4 h-4 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
            <input
              type="search"
              value={search}
              onChange={(event) => setSearch(event.target.value)}
              placeholder="Search offers, merchants, URLs..."
              className="w-full pl-9 pr-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs text-slate-900 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <select
            value={statusFilter}
            onChange={(event) => setStatusFilter(event.target.value)}
            className="px-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs font-medium text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            aria-label="Filter by offer status"
          >
            <option value="all">All statuses</option>
            <option value="draft">Draft</option>
            <option value="active">Active</option>
            <option value="paused">Paused</option>
            <option value="expired">Expired</option>
            <option value="rejected">Rejected</option>
            <option value="archived">Archived</option>
          </select>
          {isAdmin && (
            <select
              value={publisherFilter}
              onChange={(event) => setPublisherFilter(event.target.value)}
              className="px-3 py-1.5 bg-slate-50 border border-slate-200 rounded-lg text-xs font-medium text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
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
          <span className="text-xs text-slate-400 ml-auto">
            Showing {filtered.length} of {offers.length}
          </span>
        </div>

        {loading ? (
          <div className="p-12 flex items-center justify-center gap-2 text-slate-500">
            <Loader2 className="w-4 h-4 animate-spin" /> Loading affiliate offers...
          </div>
        ) : filtered.length === 0 ? (
          <div className="p-12 text-center text-slate-500 flex flex-col items-center gap-2">
            <Ticket className="w-10 h-10 text-slate-300" />
            <div className="font-semibold text-slate-700">No affiliate offers found</div>
            <div className="text-xs text-slate-400">
              Add a manually entered affiliate offer, validate its HTTPS structure, and place it as a product
              card.
            </div>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-6 py-3.5">Product</th>
                  <th className="px-6 py-3.5">Affiliate destination</th>
                  <th className="px-6 py-3.5">Publisher</th>
                  <th className="px-6 py-3.5">Status</th>
                  <th className="px-6 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {filtered.map((offer) => (
                  <tr key={offer.id} className="hover:bg-slate-50/60 transition-colors align-top">
                    <td className="px-6 py-4">
                      <div className="flex items-start gap-3">
                        {offer.product_image_url ? (
                          <img
                            src={offer.product_image_url}
                            alt=""
                            className="w-14 h-14 rounded-lg object-cover bg-slate-100 border border-slate-200 shrink-0"
                            referrerPolicy="no-referrer"
                            onError={(event) => {
                              (event.target as HTMLImageElement).style.visibility = 'hidden';
                            }}
                          />
                        ) : (
                          <div className="w-14 h-14 rounded-lg bg-slate-100 border border-slate-200 flex items-center justify-center text-slate-400 shrink-0">
                            <Ticket className="w-5 h-5" />
                          </div>
                        )}
                        <div className="min-w-0">
                          <div className="font-semibold text-slate-900">{offer.offer_name}</div>
                          <div className="text-xs text-slate-600 mt-0.5">{offer.product_title}</div>
                          <div className="text-[11px] text-slate-400 mt-1 flex flex-wrap items-center gap-x-2 gap-y-0.5">
                            <span>{offer.provider.replace('_', ' ')}</span>
                            {offer.merchant_name && <span>· {offer.merchant_name}</span>}
                            {offer.price_text && <span>· {offer.price_text}</span>}
                            {offer.currency && <span>· {offer.currency}</span>}
                          </div>
                          {offer.disclosure_text && (
                            <div className="text-[11px] text-slate-500 mt-1 line-clamp-2">
                              Disclosure: {offer.disclosure_text}
                            </div>
                          )}
                        </div>
                      </div>
                    </td>

                    <td className="px-6 py-4 max-w-xs">
                      <div className="flex items-start gap-1.5">
                        <code
                          className="text-[11px] font-mono text-slate-600 break-all line-clamp-3"
                          title={offer.affiliate_url}
                        >
                          {offer.affiliate_url}
                        </code>
                        <button
                          onClick={() => copyUrl(offer)}
                          className="shrink-0 text-slate-400 hover:text-slate-600 p-1 rounded"
                          title="Copy exact affiliate URL"
                        >
                          {copiedId === offer.id ? (
                            <Check className="w-3.5 h-3.5 text-emerald-600" />
                          ) : (
                            <Copy className="w-3.5 h-3.5" />
                          )}
                        </button>
                      </div>
                      <div className="mt-1.5 flex flex-wrap items-center gap-1">
                        <span
                          className={`text-[10px] font-semibold px-1.5 py-0.5 rounded border uppercase tracking-wider ${
                            VALIDATION_STYLES[offer.validation_status] || VALIDATION_STYLES.unverified
                          }`}
                        >
                          {offer.validation_status}
                        </span>
                        {offer.last_link_validation_at && (
                          <span className="text-[10px] text-slate-400">
                            {formatDate(offer.last_link_validation_at)}
                          </span>
                        )}
                      </div>
                      {offer.validation_reason && (
                        <div className="text-[10px] text-slate-400 mt-1 line-clamp-2" title={offer.validation_reason}>
                          {offer.validation_reason}
                        </div>
                      )}
                    </td>

                    <td className="px-6 py-4 text-xs text-slate-600">
                      <div className="font-medium text-slate-800">
                        {publishersById.get(offer.publisher_id)?.name || offer.publisher_id}
                      </div>
                      <div className="text-[10px] font-mono text-slate-400 mt-0.5">{offer.publisher_id}</div>
                    </td>

                    <td className="px-6 py-4">
                      <div className="flex flex-col gap-1 items-start">
                        <span
                          className={`inline-flex items-center gap-1 text-xs font-semibold px-2.5 py-1 rounded-full border ${
                            STATUS_STYLES[offer.status] || STATUS_STYLES.draft
                          }`}
                        >
                          {offer.status}
                        </span>
                        {offer.status === 'active' && offer.validation_status !== 'valid' && (
                          <span className="text-[10px] text-amber-600 flex items-center gap-0.5">
                            <AlertTriangle className="w-3 h-3" /> Structural check pending
                          </span>
                        )}
                      </div>
                    </td>

                    <td className="px-6 py-4 text-right">
                      <div className="flex flex-wrap items-center justify-end gap-1.5">
                        {canWrite && (
                          <>
                            <button
                              onClick={() => handleValidate(offer)}
                              className="px-2 py-1 rounded-md text-[11px] font-semibold bg-slate-100 text-slate-700 hover:bg-slate-200 border border-slate-200"
                              title="Validate HTTPS structure (does not fetch the destination)"
                            >
                              Validate
                            </button>
                            <button
                              onClick={() => handleTogglePublish(offer)}
                              disabled={offer.status === 'archived' || offer.status === 'expired'}
                              className={`px-2 py-1 rounded-md text-[11px] font-semibold flex items-center gap-1 border disabled:opacity-40 ${
                                offer.status === 'active'
                                  ? 'bg-amber-50 text-amber-700 hover:bg-amber-100 border-amber-200'
                                  : 'bg-emerald-50 text-emerald-700 hover:bg-emerald-100 border-emerald-200'
                              }`}
                            >
                              {offer.status === 'active' ? (
                                <>
                                  <Pause className="w-3 h-3" /> Pause
                                </>
                              ) : (
                                <>
                                  <Play className="w-3 h-3" /> Activate
                                </>
                              )}
                            </button>
                            <button
                              onClick={() => openPlace(offer)}
                              className="px-2 py-1 rounded-md text-[11px] font-semibold bg-blue-50 text-blue-700 hover:bg-blue-100 border border-blue-200 flex items-center gap-1"
                            >
                              <ExternalLink className="w-3 h-3" /> Place
                            </button>
                            {offer.status !== 'archived' && offer.status !== 'rejected' && offer.status !== 'expired' && (
                              <button
                                onClick={() => openEdit(offer)}
                                className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg"
                                title="Edit offer"
                              >
                                <Edit className="w-3.5 h-3.5" />
                              </button>
                            )}
                            <button
                              onClick={() => handleDelete(offer)}
                              className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg"
                              title="Delete offer"
                            >
                              <Trash2 className="w-3.5 h-3.5" />
                            </button>
                          </>
                        )}
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="rounded-lg border border-slate-200 bg-white px-4 py-3 text-xs text-slate-500 leading-relaxed">
        <strong className="text-slate-700">Reporting boundary:</strong> clicks and outbound redirects are
        AdControl-observed events. External conversions and commissions are not connected in this release and are
        never inferred from redirects. Affiliate URLs are stored and redirected exactly as entered; AdControl does
        not add or reorder query parameters.
      </div>

      {/* Create / Edit offer */}
      <Modal
        isOpen={createOpen || !!editOffer}
        onClose={() => {
          setCreateOpen(false);
          setEditOffer(null);
        }}
        title={editOffer ? `Edit Affiliate Offer: ${editOffer.offer_name}` : 'New Affiliate Offer'}
        maxWidth="max-w-2xl"
      >
        <form onSubmit={editOffer ? handleEdit : handleCreate} className="flex flex-col gap-4">
          {isAdmin && !editOffer && (
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Owning publisher
              </label>
              <select
                required
                value={publisherId}
                onChange={(event) => setPublisherId(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="">Select a publisher…</option>
                {publishers.map((publisher) => (
                  <option key={publisher.id} value={publisher.id}>
                    {publisher.name} ({publisher.status})
                  </option>
                ))}
              </select>
              <p className="text-[11px] text-slate-400 mt-1">
                Offers can only be placed on sites owned by this publisher.
              </p>
            </div>
          )}
          {editOffer && (
            <div className="rounded-lg bg-slate-50 border border-slate-200 px-3 py-2 text-xs text-slate-600">
              Publisher: <strong>{publishersById.get(editOffer.publisher_id)?.name || editOffer.publisher_id}</strong>.
              An offer cannot be moved between publishers.
            </div>
          )}

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Provider
              </label>
              <select
                value={provider}
                onChange={(event) => setProvider(event.target.value as Provider)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="shopee_affiliate">Shopee affiliate</option>
                <option value="manual_affiliate">Manual affiliate</option>
              </select>
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Internal offer name
              </label>
              <input
                required
                maxLength={200}
                value={offerName}
                onChange={(event) => setOfferName(event.target.value)}
                placeholder="e.g. Wireless Earbuds — Shopee ID"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Affiliate URL (stored and redirected exactly)
            </label>
            <input
              required
              type="text"
              inputMode="url"
              spellCheck={false}
              autoComplete="off"
              value={affiliateUrl}
              onChange={(event) => setAffiliateUrl(event.target.value)}
              placeholder="https://shopee.co.id/...?af_siteid=..."
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              HTTPS only. Do not trim or rewrite the URL: query parameters and their order are preserved. The
              destination is not fetched by AdControl.
            </p>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Canonical product URL (optional)
            </label>
            <input
              type="text"
              inputMode="url"
              spellCheck={false}
              value={canonicalProductUrl}
              onChange={(event) => setCanonicalProductUrl(event.target.value)}
              placeholder="https://shopee.co.id/product/..."
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Product title
              </label>
              <input
                required
                maxLength={300}
                value={productTitle}
                onChange={(event) => setProductTitle(event.target.value)}
                placeholder="Visible card headline"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Merchant name
              </label>
              <input
                maxLength={120}
                value={merchantName}
                onChange={(event) => setMerchantName(event.target.value)}
                placeholder="e.g. Official Store"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Product image URL (required for product cards)
            </label>
            <input
              required
              type="url"
              value={productImageUrl}
              onChange={(event) => setProductImageUrl(event.target.value)}
              placeholder="https://..."
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Product description
            </label>
            <textarea
              rows={2}
              maxLength={2000}
              value={productDescription}
              onChange={(event) => setProductDescription(event.target.value)}
              placeholder="Short product copy shown on the card"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
            <div className="col-span-2">
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Price text
              </label>
              <input
                maxLength={50}
                value={priceText}
                onChange={(event) => setPriceText(event.target.value)}
                placeholder="e.g. Rp 149.000"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Currency
              </label>
              <input
                maxLength={3}
                value={currency}
                onChange={(event) => setCurrency(event.target.value)}
                placeholder="IDR"
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono uppercase focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Offer status
              </label>
              <select
                value={offerStatus}
                onChange={(event) => setOfferStatus(event.target.value as OfferStatus)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="draft">Draft</option>
                <option value="active">Active</option>
                <option value="paused">Paused</option>
              </select>
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Availability checked at (optional)
            </label>
            <input
              type="datetime-local"
              value={availabilityCheckedAt ? availabilityCheckedAt.slice(0, 16) : ''}
              onChange={(event) => setAvailabilityCheckedAt(toIsoUtc(event.target.value))}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-xs focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Affiliate disclosure (optional)
            </label>
            <textarea
              rows={2}
              maxLength={500}
              value={disclosureText}
              onChange={(event) => setDisclosureText(event.target.value)}
              placeholder="Defaults to the site disclosure or a built-in affiliate disclosure"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              Disclosure is always rendered outside the affiliate link and remains visible on every product card.
            </p>
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => {
                setCreateOpen(false);
                setEditOffer(null);
              }}
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
              {submitting ? 'Saving...' : editOffer ? 'Save Offer' : 'Create Offer'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Place offer as a product-card campaign */}
      <Modal
        isOpen={!!placeOffer}
        onClose={() => setPlaceOffer(null)}
        title={`Place Offer: ${placeOffer?.offer_name || ''}`}
        maxWidth="max-w-xl"
      >
        <form onSubmit={handlePlace} className="flex flex-col gap-4">
          <div className="rounded-lg border border-blue-200 bg-blue-50 px-3 py-2 text-xs text-blue-900 leading-relaxed">
            A product-card campaign will be created with the offer&apos;s stored destination and creative. The
            destination cannot be overridden here. Publish the campaign after confirming the site is verified and
            the offer is active.
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Placement
            </label>
            <select
              required
              value={placePlacementId}
              onChange={(event) => setPlacePlacementId(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              {placePlacements.length === 0 && <option value="">No affiliate-enabled placements</option>}
              {placePlacements.map((placement) => {
                const site = sitesById.get(placement.site_id);
                return (
                  <option key={placement.id} value={placement.id}>
                    {site?.name || placement.site_id} — {placement.name} ({placement.placement_key})
                  </option>
                );
              })}
            </select>
            {placePlacements.length === 0 && (
              <p className="text-[11px] text-rose-600 mt-1">
                Create a placement on one of this publisher&apos;s affiliate-enabled sites first. The site and
                placement must both allow the affiliate source.
              </p>
            )}
            {selectedPlacement && selectedPlacementSite && (
              <div className="mt-2 rounded-lg bg-slate-50 border border-slate-200 px-3 py-2 text-[11px] text-slate-600 flex flex-col gap-1">
                <span>
                  Site: <strong>{selectedPlacementSite.name}</strong> ({selectedPlacementSite.primary_domain})
                </span>
                <span className="flex flex-wrap gap-2">
                  <span>Verification: {selectedPlacementSite.verification_status}</span>
                  <span>Monetization: {selectedPlacementSite.monetization_status}</span>
                  <span>Placement format: {selectedPlacement.format.replace('_', ' ')}</span>
                </span>
                {(selectedPlacementSite.verification_status !== 'verified' ||
                  selectedPlacementSite.monetization_status !== 'enabled') && (
                  <span className="text-amber-700 flex items-center gap-1">
                    <AlertTriangle className="w-3 h-3" /> The campaign can be drafted now but cannot be published
                    until this site is verified and monetization-enabled.
                  </span>
                )}
              </div>
            )}
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Campaign name
            </label>
            <input
              maxLength={120}
              value={placeCampaignName}
              onChange={(event) => setPlaceCampaignName(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Initial campaign status
              </label>
              <select
                value={placeCampaignStatus}
                onChange={(event) =>
                  setPlaceCampaignStatus(event.target.value as 'draft' | 'active' | 'scheduled')
                }
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="draft">Draft (recommended)</option>
                <option value="active">Active (publish when eligible)</option>
                <option value="scheduled">Scheduled</option>
              </select>
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Priority
              </label>
              <input
                type="number"
                min={0}
                max={1000}
                value={placePriority}
                onChange={(event) => setPlacePriority(Number(event.target.value))}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Schedule start (UTC)
              </label>
              <input
                type="datetime-local"
                value={placeStartsAt}
                onChange={(event) => setPlaceStartsAt(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-xs focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Schedule end (UTC)
              </label>
              <input
                type="datetime-local"
                value={placeEndsAt}
                onChange={(event) => setPlaceEndsAt(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-xs focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Weight
            </label>
            <input
              type="number"
              min={1}
              max={10000}
              value={placeWeight}
              onChange={(event) => setPlaceWeight(Number(event.target.value))}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setPlaceOffer(null)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={placing || placePlacements.length === 0}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg disabled:opacity-50"
            >
              {placing ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <CheckCircle2 className="w-3.5 h-3.5" />}
              {placing ? 'Creating...' : 'Create Product Card'}
            </button>
          </div>
        </form>
      </Modal>

      <div className="flex items-start gap-2 rounded-lg border border-amber-200 bg-amber-50 px-4 py-3 text-xs text-amber-900">
        <ShieldCheck className="w-4 h-4 shrink-0 mt-0.5" />
        <span>
          Validation is structural only: AdControl checks the HTTPS scheme, public hostname, and URL shape without
          fetching the destination. External network inventory remains disabled until provider terms and
          credentials are approved.
        </span>
      </div>
    </div>
  );
};
