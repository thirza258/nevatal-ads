import React, { useEffect, useState } from 'react';
import { api } from '../api';
import { AdSourceType, Publisher, Site, SiteVerification } from '../types';
import { Modal } from '../components/Modal';
import {
  Plus,
  Copy,
  Check,
  RefreshCw,
  PowerOff,
  Trash2,
  Code2,
  Globe,
  Shield,
  ShieldAlert,
  ShieldCheck,
  Edit,
  Loader2,
} from 'lucide-react';

interface WebsitesViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  openCreateModalDirectly?: boolean;
  canWrite?: boolean;
  isAdmin?: boolean;
}

const SOURCE_OPTIONS: { value: AdSourceType; label: string; description: string }[] = [
  { value: 'house', label: 'House', description: 'Ads for the publisher’s own properties' },
  { value: 'direct', label: 'Direct', description: 'Admin-managed direct advertiser campaigns' },
  { value: 'affiliate', label: 'Affiliate', description: 'Disclosed affiliate product cards' },
];

function verificationBadge(site: Site): { label: string; className: string } {
  if (site.verification_status === 'verified') {
    return { label: 'Domain verified', className: 'bg-emerald-50 text-emerald-700 border-emerald-200' };
  }
  if (site.verification_status === 'rejected') {
    return { label: 'Verification rejected', className: 'bg-rose-50 text-rose-700 border-rose-200' };
  }
  return { label: 'Verification required', className: 'bg-amber-50 text-amber-700 border-amber-200' };
}

export const WebsitesView: React.FC<WebsitesViewProps> = ({
  onNotify,
  openCreateModalDirectly = false,
  canWrite = true,
  isAdmin = false,
}) => {
  const [sites, setSites] = useState<Site[]>([]);
  const [publishers, setPublishers] = useState<Publisher[]>([]);
  const [loading, setLoading] = useState(true);
  const [createModalOpen, setCreateModalOpen] = useState(openCreateModalDirectly);
  const [editSite, setEditSite] = useState<Site | null>(null);
  const [snippetSite, setSnippetSite] = useState<Site | null>(null);
  const [snippetFramework, setSnippetFramework] = useState<'html' | 'react' | 'nextjs' | 'wordpress'>('html');
  const [verificationSite, setVerificationSite] = useState<Site | null>(null);
  const [verification, setVerification] = useState<SiteVerification | null>(null);
  const [verificationLoading, setVerificationLoading] = useState(false);
  const [verifying, setVerifying] = useState(false);

  // Form states
  const [name, setName] = useState('');
  const [primaryDomain, setPrimaryDomain] = useState('');
  const [allowedDomainsStr, setAllowedDomainsStr] = useState('');
  const [status, setStatus] = useState<'active' | 'disabled'>('active');
  const [publisherId, setPublisherId] = useState('');
  const [defaultDisclosure, setDefaultDisclosure] = useState('');
  const [allowedSources, setAllowedSources] = useState<AdSourceType[]>(['house', 'direct', 'affiliate']);
  const [monetizationStatus, setMonetizationStatus] = useState<'pending' | 'enabled' | 'disabled'>('pending');
  const [submitting, setSubmitting] = useState(false);
  const [copiedKey, setCopiedKey] = useState<string | null>(null);

  const fetchSites = async () => {
    try {
      setLoading(true);
      const [res, publisherList] = await Promise.all([
        api.getSites(),
        isAdmin ? api.getPublishers().catch(() => [] as Publisher[]) : Promise.resolve([] as Publisher[]),
      ]);
      setSites(res);
      setPublishers(publisherList);
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to fetch websites');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchSites();
  }, []);

  const openVerification = async (site: Site) => {
    setVerificationSite(site);
    setVerification(null);
    setVerificationLoading(true);
    try {
      setVerification(await api.getSiteVerification(site.id));
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load verification challenge');
    } finally {
      setVerificationLoading(false);
    }
  };

  const handleVerify = async (method: 'dns_txt' | 'manual') => {
    if (!verificationSite) return;
    if (
      method === 'manual' &&
      !window.confirm(
        `Mark "${verificationSite.name}" as manually verified? Only use this after confirming domain ownership outside AdControl.`,
      )
    ) {
      return;
    }
    setVerifying(true);
    try {
      const result = await api.verifySite(verificationSite.id, method);
      setVerificationSite(result.site);
      setVerification(await api.getSiteVerification(verificationSite.id));
      onNotify(
        'success',
        method === 'manual'
          ? `"${result.site.name}" marked verified by administrator override.`
          : `DNS TXT record matched. "${result.site.name}" is verified.`,
      );
      fetchSites();
    } catch (err: any) {
      onNotify('error', err.message || 'Verification failed');
    } finally {
      setVerifying(false);
    }
  };

  const toggleSource = (source: AdSourceType) => {
    setAllowedSources((current) =>
      current.includes(source) ? current.filter((value) => value !== source) : [...current, source],
    );
  };

  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name || !primaryDomain) return;
    if (allowedSources.length === 0) {
      onNotify('error', 'Select at least one allowed ad source.');
      return;
    }
    setSubmitting(true);
    try {
      const allowed = allowedDomainsStr
        .split(/[,\n]/)
        .map((s) => s.trim())
        .filter((s) => s.length > 0);

      const newSite = await api.createSite({
        name,
        primary_domain: primaryDomain,
        allowed_domains: allowed.length > 0 ? allowed : [primaryDomain],
        status,
        default_disclosure_text: defaultDisclosure,
        allowed_ad_sources: allowedSources,
        ...(isAdmin ? { publisher_id: publisherId } : {}),
      });

      onNotify(
        'success',
        newSite.verification_required
          ? `Website "${newSite.name}" registered. Complete domain verification before it can serve ads.`
          : `Website "${newSite.name}" registered successfully.`,
      );
      setCreateModalOpen(false);
      resetForm();
      await fetchSites();
      // Open integration modal for quick snippet copy
      setSnippetSite(newSite);
      if (newSite.verification_required) {
        openVerification(newSite);
      }
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to create website');
    } finally {
      setSubmitting(false);
    }
  };

  const handleEditSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!editSite) return;
    if (allowedSources.length === 0) {
      onNotify('error', 'Select at least one allowed ad source.');
      return;
    }
    setSubmitting(true);
    try {
      const allowed = allowedDomainsStr
        .split(/[,\n]/)
        .map((s) => s.trim())
        .filter((s) => s.length > 0);

      const updated = await api.updateSite(editSite.id, {
        name,
        primary_domain: primaryDomain,
        allowed_domains: allowed,
        status,
        default_disclosure_text: defaultDisclosure,
        allowed_ad_sources: allowedSources,
        ...(isAdmin ? { monetization_status: monetizationStatus } : {}),
      });

      onNotify('success', `Website "${updated.name}" updated.`);
      setEditSite(null);
      resetForm();
      fetchSites();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update website');
    } finally {
      setSubmitting(false);
    }
  };

  const handleRotateKey = async (site: Site) => {
    if (!window.confirm(`Are you sure you want to rotate the public key for "${site.name}"? You will need to update the snippet on the site.`)) {
      return;
    }
    try {
      const updated = await api.rotateSiteKey(site.id);
      onNotify('success', `Rotated key for "${site.name}". New key: ${updated.public_site_key}`);
      fetchSites();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to rotate key');
    }
  };

  const handlePauseAll = async (site: Site) => {
    if (!window.confirm(`Kill Switch: Pause ALL active ads delivering to "${site.name}" immediately?`)) {
      return;
    }
    try {
      await api.pauseAllSiteCampaigns(site.id);
      onNotify('success', `All ads paused centrally for "${site.name}".`);
      fetchSites();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to pause ads');
    }
  };

  const handleDelete = async (site: Site) => {
    if (
      !window.confirm(
        `Delete website "${site.name}" and its placements? This fails safely if analytics history exists; disable the site instead to preserve reports.`,
      )
    ) {
      return;
    }
    try {
      const result = await api.deleteSite(site.id);
      onNotify('success', result.message || `Deleted website "${site.name}".`);
      fetchSites();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to delete site');
    }
  };

  const openEditModal = (site: Site) => {
    setEditSite(site);
    setName(site.name);
    setPrimaryDomain(site.primary_domain);
    setAllowedDomainsStr(site.allowed_domains.join(', '));
    setStatus(site.status);
    setPublisherId(site.publisher_id || '');
    setDefaultDisclosure(site.default_disclosure_text || '');
    setAllowedSources(site.allowed_ad_sources?.length ? site.allowed_ad_sources : ['house', 'direct', 'affiliate']);
    setMonetizationStatus(site.monetization_status || 'pending');
  };

  const resetForm = () => {
    setName('');
    setPrimaryDomain('');
    setAllowedDomainsStr('');
    setStatus('active');
    setPublisherId('');
    setDefaultDisclosure('');
    setAllowedSources(['house', 'direct', 'affiliate']);
    setMonetizationStatus('pending');
  };

  const copyToClipboard = (text: string, id: string) => {
    navigator.clipboard.writeText(text);
    setCopiedKey(id);
    setTimeout(() => setCopiedKey(null), 2000);
    onNotify('info', 'Copied to clipboard');
  };

  const getOrigin = () => {
    return window.location.origin;
  };

  const renderSnippetCode = (site: Site) => {
    const origin = getOrigin();
    if (snippetFramework === 'html') {
      return `<!-- 1. AdControl: Add once in the shared <head> or <body> -->
<script
  async
  src="${origin}/sdk/v1/adcontrol.js"
  data-site-key="${site.public_site_key}"
></script>

<!-- 2. Place this container where you want the ad to render -->
<div data-adcontrol-placement="header-banner"></div>`;
    }

    if (snippetFramework === 'react') {
      return `// React / Vite Integration
// 1. Add snippet to index.html (or load dynamically):
// <script async src="${origin}/sdk/v1/adcontrol.js" data-site-key="${site.public_site_key}"></script>

// 2. In your React component:
import React, { useEffect } from 'react';

export const HeaderAd = () => {
  useEffect(() => {
    // If dynamically mounted in SPA, trigger safe scan
    if ((window as any).AdControl) {
      (window as any).AdControl.scan();
    }
  }, []);

  return <div data-adcontrol-placement="header-banner" className="my-4" />;
};`;
    }

    if (snippetFramework === 'nextjs') {
      return `// Next.js (App Router or Pages Router)
import Script from 'next/script';

export default function Layout({ children }) {
  return (
    <>
      <Script
        src="${origin}/sdk/v1/adcontrol.js"
        data-site-key="${site.public_site_key}"
        strategy="afterInteractive"
      />
      
      {/* Placement element */}
      <div data-adcontrol-placement="header-banner" />
      
      <main>{children}</main>
    </>
  );
}`;
    }

    return `<!-- WordPress Integration -->
<!-- 1. Add to header.php or Theme Customizer -> Header Scripts: -->
<script
  async
  src="${origin}/sdk/v1/adcontrol.js"
  data-site-key="${site.public_site_key}"
></script>

<!-- 2. In your post / page or Custom HTML block: -->
<div data-adcontrol-placement="header-banner"></div>`;
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Websites & Client Keys</h2>
          <p className="text-sm text-slate-500">
            Register client domains, retrieve public snippets, and enforce domain allowlists
          </p>
        </div>

        {canWrite && (
          <button
            onClick={() => {
              resetForm();
              setCreateModalOpen(true);
            }}
            className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all self-start"
          >
            <Plus className="w-4 h-4" />
            <span>Register Website</span>
          </button>
        )}
      </div>

      {/* Sites Table */}
      <div className="bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden">
        {loading ? (
          <div className="p-12 text-center text-slate-500">Loading registered websites...</div>
        ) : sites.length === 0 ? (
          <div className="p-12 text-center text-slate-500 flex flex-col items-center gap-3">
            <Globe className="w-10 h-10 text-slate-300" />
            <div>
              <div className="font-semibold text-slate-700">No websites registered yet</div>
              <div className="text-xs text-slate-400 mt-1">
                Add your first website to generate an integration snippet.
              </div>
            </div>
            {canWrite && (
              <button
                onClick={() => setCreateModalOpen(true)}
                className="mt-2 text-xs font-semibold text-blue-600 bg-blue-50 px-3 py-1.5 rounded-lg border border-blue-200"
              >
                Add Website Now
              </button>
            )}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-6 py-3.5">Website & Primary Domain</th>
                  <th className="px-6 py-3.5">Public Site Key</th>
                  <th className="px-6 py-3.5">Allowed Origins</th>
                  <th className="px-6 py-3.5">Status</th>
                  <th className="px-6 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {sites.map((site) => (
                  <tr key={site.id} className="hover:bg-slate-50/60 transition-colors">
                    <td className="px-6 py-4">
                      <div className="font-semibold text-slate-900">{site.name}</div>
                      <div className="text-xs text-slate-500 flex items-center gap-1 mt-0.5">
                        <Globe className="w-3.5 h-3.5 text-slate-400" />
                        <span>{site.primary_domain}</span>
                      </div>
                      <div className="flex flex-wrap items-center gap-1 mt-1.5">
                        <span
                          className={`inline-flex items-center gap-1 text-[10px] font-semibold px-1.5 py-0.5 rounded-full border ${
                            verificationBadge(site).className
                          }`}
                        >
                          {site.verification_status === 'verified' ? (
                            <ShieldCheck className="w-3 h-3" />
                          ) : (
                            <ShieldAlert className="w-3 h-3" />
                          )}
                          {verificationBadge(site).label}
                        </span>
                        <span
                          className={`text-[10px] font-semibold px-1.5 py-0.5 rounded-full border ${
                            site.monetization_status === 'enabled'
                              ? 'bg-blue-50 text-blue-700 border-blue-200'
                              : 'bg-slate-100 text-slate-600 border-slate-200'
                          }`}
                        >
                          Monetization: {site.monetization_status}
                        </span>
                      </div>
                      <div className="flex flex-wrap items-center gap-1 mt-1">
                        {site.allowed_ad_sources.map((source) => (
                          <span
                            key={source}
                            className="text-[10px] font-semibold uppercase tracking-wider bg-slate-100 text-slate-600 border border-slate-200 px-1.5 py-0.5 rounded"
                          >
                            {source}
                          </span>
                        ))}
                        {isAdmin && site.publisher_id && (
                          <span className="text-[10px] text-slate-400">
                            · {publishers.find((publisher) => publisher.id === site.publisher_id)?.name || 'Publisher'}
                          </span>
                        )}
                        {!site.publisher_id && (
                          <span className="text-[10px] text-slate-400">· Platform-owned</span>
                        )}
                      </div>
                    </td>

                    <td className="px-6 py-4">
                      <div className="flex items-center gap-2">
                        <code className="text-xs font-mono bg-slate-100 text-slate-700 px-2 py-1 rounded border border-slate-200">
                          {site.public_site_key}
                        </code>
                        <button
                          onClick={() => copyToClipboard(site.public_site_key, site.id)}
                          className="text-slate-400 hover:text-slate-600 p-1 rounded"
                          title="Copy public site key"
                        >
                          {copiedKey === site.id ? (
                            <Check className="w-4 h-4 text-emerald-600" />
                          ) : (
                            <Copy className="w-4 h-4" />
                          )}
                        </button>
                      </div>
                      <div className="text-[10px] text-slate-400 mt-1">Config v{site.config_version}</div>
                    </td>

                    <td className="px-6 py-4">
                      <div className="flex flex-wrap gap-1 max-w-xs">
                        {site.allowed_domains.map((dom, i) => (
                          <span
                            key={i}
                            className="text-[11px] font-mono bg-slate-100 text-slate-600 px-2 py-0.5 rounded border border-slate-200"
                          >
                            {dom}
                          </span>
                        ))}
                      </div>
                    </td>

                    <td className="px-6 py-4">
                      <span
                        className={`inline-flex items-center gap-1 text-xs font-semibold px-2.5 py-1 rounded-full ${
                          site.status === 'active'
                            ? 'bg-emerald-50 text-emerald-700 border border-emerald-200'
                            : 'bg-slate-100 text-slate-600 border border-slate-200'
                        }`}
                      >
                        <span
                          className={`w-1.5 h-1.5 rounded-full ${
                            site.status === 'active' ? 'bg-emerald-500' : 'bg-slate-400'
                          }`}
                        />
                        {site.status}
                      </span>
                    </td>

                    <td className="px-6 py-4 text-right">
                      <div className="flex flex-wrap items-center justify-end gap-1.5">
                        <button
                          onClick={() => setSnippetSite(site)}
                          className="px-2.5 py-1.5 bg-blue-50 hover:bg-blue-100 text-blue-700 text-xs font-semibold rounded-lg flex items-center gap-1 transition-colors"
                          title="View Integration Snippet"
                        >
                          <Code2 className="w-3.5 h-3.5" />
                          <span>Snippet</span>
                        </button>

                        {canWrite && site.verification_status !== 'verified' && (
                          <button
                            onClick={() => openVerification(site)}
                            className="px-2 py-1.5 bg-amber-50 hover:bg-amber-100 text-amber-700 text-[11px] font-semibold rounded-lg flex items-center gap-1 border border-amber-200 transition-colors"
                            title="Complete domain verification"
                          >
                            <ShieldAlert className="w-3.5 h-3.5" />
                            <span>Verify</span>
                          </button>
                        )}

                        {canWrite && (
                          <>
                            <button
                              onClick={() => openEditModal(site)}
                              className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg transition-colors"
                              title="Edit Website"
                            >
                              <Edit className="w-4 h-4" />
                            </button>

                            <button
                              onClick={() => handleRotateKey(site)}
                              className="p-1.5 text-slate-400 hover:text-amber-600 hover:bg-amber-50 rounded-lg transition-colors"
                              title="Rotate Public Key"
                            >
                              <RefreshCw className="w-4 h-4" />
                            </button>

                            <button
                              onClick={() => handlePauseAll(site)}
                              className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition-colors"
                              title="Kill Switch: Pause All Ads for Site"
                            >
                              <PowerOff className="w-4 h-4" />
                            </button>

                            <button
                              onClick={() => handleDelete(site)}
                              className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition-colors"
                              title="Delete Website"
                            >
                              <Trash2 className="w-4 h-4" />
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

      {/* Create Website Modal */}
      <Modal
        isOpen={createModalOpen}
        onClose={() => setCreateModalOpen(false)}
        title="Register Website"
      >
        <form onSubmit={handleCreateSubmit} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Website Name
            </label>
            <input
              type="text"
              required
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Developer Tech Blog"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Primary Domain
            </label>
            <input
              type="text"
              required
              value={primaryDomain}
              onChange={(e) => setPrimaryDomain(e.target.value)}
              placeholder="e.g. blog.example.com or localhost:3000"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed Domains / Origins (comma separated)
            </label>
            <input
              type="text"
              value={allowedDomainsStr}
              onChange={(e) => setAllowedDomainsStr(e.target.value)}
              placeholder="e.g. blog.example.com, localhost:3000, *.example.com"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              Ad delivery requests will check Origin/Referer against this allowlist. Enter * for unrestricted development.
            </p>
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
              <option value="active">Active (Deliver ads)</option>
              <option value="disabled">Disabled (Do not deliver)</option>
            </select>
          </div>

          {isAdmin && (
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Publisher ownership
              </label>
              <select
                value={publisherId}
                onChange={(event) => setPublisherId(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="">Platform-owned (legacy)</option>
                {publishers.map((publisher) => (
                  <option key={publisher.id} value={publisher.id}>
                    {publisher.name} ({publisher.status})
                  </option>
                ))}
              </select>
              <p className="text-[11px] text-slate-400 mt-1">
                Publisher-owned sites start unverified and cannot deliver until DNS verification and monetization
                approval are complete.
              </p>
            </div>
          )}

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed ad sources
            </label>
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
              {SOURCE_OPTIONS.map((source) => (
                <label
                  key={source.value}
                  className={`flex items-start gap-2 rounded-lg border px-3 py-2 text-xs cursor-pointer ${
                    allowedSources.includes(source.value)
                      ? 'border-blue-300 bg-blue-50 text-blue-900'
                      : 'border-slate-200 bg-slate-50 text-slate-600'
                  }`}
                >
                  <input
                    type="checkbox"
                    checked={allowedSources.includes(source.value)}
                    onChange={() => toggleSource(source.value)}
                    className="mt-0.5"
                  />
                  <span>
                    <span className="font-semibold block">{source.label}</span>
                    <span className="text-[10px] opacity-80">{source.description}</span>
                  </span>
                </label>
              ))}
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Default affiliate disclosure (optional)
            </label>
            <textarea
              rows={2}
              maxLength={500}
              value={defaultDisclosure}
              onChange={(event) => setDefaultDisclosure(event.target.value)}
              placeholder="e.g. This page contains affiliate links. We may earn a commission at no extra cost to you."
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              A visible affiliate disclosure is always rendered on affiliate product cards, even when left empty
              here.
            </p>
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
              {submitting ? 'Registering...' : 'Register Website'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Edit Website Modal */}
      <Modal
        isOpen={!!editSite}
        onClose={() => setEditSite(null)}
        title={`Edit Website: ${editSite?.name}`}
      >
        <form onSubmit={handleEditSubmit} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Website Name
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
              Primary Domain
            </label>
            <input
              type="text"
              required
              value={primaryDomain}
              onChange={(e) => setPrimaryDomain(e.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed Domains (comma separated)
            </label>
            <input
              type="text"
              value={allowedDomainsStr}
              onChange={(e) => setAllowedDomainsStr(e.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
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

          {isAdmin && (
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
                Monetization approval
              </label>
              <select
                value={monetizationStatus}
                onChange={(event) => setMonetizationStatus(event.target.value as typeof monetizationStatus)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="pending">Pending</option>
                <option value="enabled">Enabled (eligible to deliver)</option>
                <option value="disabled">Disabled</option>
              </select>
              <p className="text-[11px] text-slate-400 mt-1">
                Delivery also requires a verified domain and an active publisher account.
              </p>
            </div>
          )}

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Allowed ad sources
            </label>
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
              {SOURCE_OPTIONS.map((source) => (
                <label
                  key={source.value}
                  className={`flex items-start gap-2 rounded-lg border px-3 py-2 text-xs cursor-pointer ${
                    allowedSources.includes(source.value)
                      ? 'border-blue-300 bg-blue-50 text-blue-900'
                      : 'border-slate-200 bg-slate-50 text-slate-600'
                  }`}
                >
                  <input
                    type="checkbox"
                    checked={allowedSources.includes(source.value)}
                    onChange={() => toggleSource(source.value)}
                    className="mt-0.5"
                  />
                  <span>
                    <span className="font-semibold block">{source.label}</span>
                    <span className="text-[10px] opacity-80">{source.description}</span>
                  </span>
                </label>
              ))}
            </div>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Default affiliate disclosure
            </label>
            <textarea
              rows={2}
              maxLength={500}
              value={defaultDisclosure}
              onChange={(event) => setDefaultDisclosure(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            {editSite?.publisher_id && editSite.primary_domain !== primaryDomain && (
              <p className="text-[11px] text-amber-700 mt-1">
                Changing the primary domain resets this site to unverified. Complete DNS verification again before
                delivery resumes.
              </p>
            )}
          </div>

          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setEditSite(null)}
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

      {/* Integration Snippet Modal */}
      <Modal
        isOpen={!!snippetSite}
        onClose={() => setSnippetSite(null)}
        title={`Integration Snippet — ${snippetSite?.name}`}
        maxWidth="max-w-2xl"
      >
        {snippetSite && (
          <div className="flex flex-col gap-4">
            <div className="flex items-center gap-2 p-3 bg-blue-50 border border-blue-200 rounded-lg text-xs text-blue-900">
              <Shield className="w-5 h-5 text-blue-600 shrink-0" />
              <div>
                <strong>Single Installation Promise:</strong> Paste this snippet once into your site's shared layout. After that, manage destination URLs, schedules, and creatives centrally from this dashboard without redeploying!
              </div>
            </div>

            {/* Framework Selector */}
            <div className="flex items-center gap-1.5 p-1 bg-slate-100 rounded-lg self-start text-xs font-medium">
              {(['html', 'react', 'nextjs', 'wordpress'] as const).map((fw) => (
                <button
                  key={fw}
                  onClick={() => setSnippetFramework(fw)}
                  className={`px-3 py-1 rounded-md transition-all ${
                    snippetFramework === fw
                      ? 'bg-white text-slate-900 shadow-sm font-semibold'
                      : 'text-slate-600 hover:text-slate-900'
                  }`}
                >
                  {fw === 'html'
                    ? 'Plain HTML'
                    : fw === 'react'
                    ? 'React / Vite'
                    : fw === 'nextjs'
                    ? 'Next.js'
                    : 'WordPress'}
                </button>
              ))}
            </div>

            <div className="relative">
              <pre className="bg-slate-900 text-slate-100 p-4 rounded-xl text-xs font-mono overflow-x-auto leading-relaxed border border-slate-800">
                {renderSnippetCode(snippetSite)}
              </pre>
              <button
                onClick={() => copyToClipboard(renderSnippetCode(snippetSite), 'snippet-code')}
                className="absolute top-3 right-3 bg-slate-800 hover:bg-slate-700 text-white text-xs px-3 py-1.5 rounded-lg flex items-center gap-1.5 border border-slate-700 shadow"
              >
                {copiedKey === 'snippet-code' ? (
                  <>
                    <Check className="w-3.5 h-3.5 text-emerald-400" />
                    <span>Copied</span>
                  </>
                ) : (
                  <>
                    <Copy className="w-3.5 h-3.5" />
                    <span>Copy Snippet</span>
                  </>
                )}
              </button>
            </div>

            <div className="bg-slate-50 border border-slate-200 rounded-lg p-3 text-xs text-slate-600 flex flex-col gap-1">
              <div className="font-semibold text-slate-800">Public Site Key</div>
              <div className="flex items-center justify-between">
                <code className="font-mono text-blue-700">{snippetSite.public_site_key}</code>
                <span className="text-[11px] text-slate-400">Allowed on: {snippetSite.allowed_domains.join(', ')}</span>
              </div>
            </div>
          </div>
        )}
      </Modal>

      {/* Domain verification modal */}
      <Modal
        isOpen={!!verificationSite}
        onClose={() => {
          setVerificationSite(null);
          setVerification(null);
        }}
        title={`Domain Verification — ${verificationSite?.name || ''}`}
        maxWidth="max-w-2xl"
      >
        {verificationLoading ? (
          <div className="py-10 flex items-center justify-center gap-2 text-sm text-slate-500">
            <Loader2 className="w-4 h-4 animate-spin" /> Loading verification challenge...
          </div>
        ) : verification ? (
          <div className="flex flex-col gap-4">
            <div
              className={`rounded-lg border px-4 py-3 text-xs leading-relaxed ${
                verification.verification_status === 'verified'
                  ? 'border-emerald-200 bg-emerald-50 text-emerald-900'
                  : 'border-amber-200 bg-amber-50 text-amber-900'
              }`}
            >
              <div className="flex items-center gap-2 font-semibold">
                {verification.verification_status === 'verified' ? (
                  <ShieldCheck className="w-4 h-4" />
                ) : (
                  <ShieldAlert className="w-4 h-4" />
                )}
                {verification.verification_status === 'verified'
                  ? 'Domain ownership verified'
                  : 'Domain ownership must be verified before delivery'}
              </div>
              <p className="mt-1">
                Domain: <strong>{verification.domain}</strong>
                {verification.verified_at && <> · Verified at {verification.verified_at}</>}
              </p>
              <p className="mt-1">
                Monetization: <strong>{verification.monetization_status}</strong>. Publisher accounts must also be
                active for delivery.
              </p>
            </div>

            {verification.verification_status !== 'verified' && verification.record_name && (
              <>
                <p className="text-xs text-slate-600 leading-relaxed">
                  Add a DNS TXT record at your DNS provider. AdControl checks this record server-side; it never
                  fetches your website, which avoids SSRF exposure. Keep the value exact.
                </p>

                <div className="rounded-xl border border-slate-200 overflow-hidden">
                  <div className="bg-slate-50 px-4 py-2.5 border-b border-slate-200 text-xs font-semibold text-slate-700">
                    {verification.record_type || 'TXT'} record for {verification.domain}
                  </div>
                  <div className="p-4 flex flex-col gap-3">
                    <div>
                      <div className="text-[10px] font-semibold uppercase tracking-wider text-slate-400 mb-1">
                        Name / host
                      </div>
                      <div className="flex items-center gap-2">
                        <code className="flex-1 text-xs font-mono bg-slate-900 text-emerald-300 px-3 py-2 rounded-lg break-all">
                          {verification.record_name}
                        </code>
                        <button
                          onClick={() =>
                            copyToClipboard(verification.record_name || '', 'verification-record-name')
                          }
                          className="text-slate-400 hover:text-slate-700 p-1.5 rounded-lg border border-slate-200"
                          title="Copy record name"
                        >
                          {copiedKey === 'verification-record-name' ? (
                            <Check className="w-4 h-4 text-emerald-600" />
                          ) : (
                            <Copy className="w-4 h-4" />
                          )}
                        </button>
                      </div>
                    </div>
                    <div>
                      <div className="text-[10px] font-semibold uppercase tracking-wider text-slate-400 mb-1">
                        Value
                      </div>
                      <div className="flex items-center gap-2">
                        <code className="flex-1 text-xs font-mono bg-slate-900 text-emerald-300 px-3 py-2 rounded-lg break-all">
                          {verification.record_value}
                        </code>
                        <button
                          onClick={() => copyToClipboard(verification.record_value || '', 'verification-value')}
                          className="text-slate-400 hover:text-slate-700 p-1.5 rounded-lg border border-slate-200"
                          title="Copy record value"
                        >
                          {copiedKey === 'verification-value' ? (
                            <Check className="w-4 h-4 text-emerald-600" />
                          ) : (
                            <Copy className="w-4 h-4" />
                          )}
                        </button>
                      </div>
                    </div>
                  </div>
                </div>

                {verification.instructions && verification.instructions.length > 0 && (
                  <ol className="text-xs text-slate-500 list-decimal pl-5 space-y-1">
                    {verification.instructions.map((instruction) => (
                      <li key={instruction}>{instruction}</li>
                    ))}
                  </ol>
                )}
              </>
            )}

            <div className="flex flex-wrap justify-end gap-2 pt-4 border-t border-slate-100">
              <button
                type="button"
                onClick={() => {
                  setVerificationSite(null);
                  setVerification(null);
                }}
                className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
              >
                Close
              </button>
              {verification.verification_status !== 'verified' && canWrite && (
                <>
                  {verification.manual_verification_available && (
                    <button
                      type="button"
                      onClick={() => handleVerify('manual')}
                      disabled={verifying}
                      className="px-4 py-2 text-xs font-semibold text-amber-800 bg-amber-100 hover:bg-amber-200 border border-amber-200 rounded-lg disabled:opacity-50"
                    >
                      Admin override: mark verified
                    </button>
                  )}
                  <button
                    type="button"
                    onClick={() => handleVerify('dns_txt')}
                    disabled={verifying}
                    className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold text-white bg-blue-600 hover:bg-blue-700 rounded-lg disabled:opacity-50"
                  >
                    {verifying && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
                    {verifying ? 'Checking DNS...' : 'Verify with DNS TXT'}
                  </button>
                </>
              )}
              {verification.verification_status === 'verified' && (
                <button
                  type="button"
                  onClick={() => {
                    setVerificationSite(null);
                    setVerification(null);
                  }}
                  className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold text-white bg-emerald-600 hover:bg-emerald-700 rounded-lg"
                >
                  <Check className="w-3.5 h-3.5" /> Done
                </button>
              )}
            </div>
          </div>
        ) : (
          <div className="py-8 text-center text-sm text-slate-500">
            Verification details are unavailable. Close this dialog and try again.
          </div>
        )}
      </Modal>
    </div>
  );
};
