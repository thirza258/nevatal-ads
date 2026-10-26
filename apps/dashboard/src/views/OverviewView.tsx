import React, { useEffect, useState } from 'react';
import { api } from '../api';
import { Site, Campaign, AnalyticsSummary, TimeseriesPoint, AuditLogItem } from '../types';
import { NavTab } from '../components/Sidebar';
import {
  Eye,
  MousePointerClick,
  Percent,
  Megaphone,
  Globe,
  Plus,
  ArrowUpRight,
  TrendingUp,
  Clock,
  ExternalLink,
  Ticket,
} from 'lucide-react';

interface OverviewViewProps {
  onNavigate: (tab: NavTab) => void;
  onOpenCreateAd: () => void;
  onOpenCreateSite: () => void;
  onOpenCreatePlacement: () => void;
  role: string;
}

export const OverviewView: React.FC<OverviewViewProps> = ({
  onNavigate,
  onOpenCreateAd,
  onOpenCreateSite,
  onOpenCreatePlacement,
  role,
}) => {
  const canWrite = role === 'admin' || role === 'publisher';
  const isAdmin = role === 'admin';
  const [summary, setSummary] = useState<AnalyticsSummary>({
    impressions: 0,
    clicks: 0,
    outbound_redirects: 0,
    affiliate_clicks: 0,
    ctr: 0,
    confirmed_conversions: 0,
    confirmed_commission: 0,
  });
  const [timeseries, setTimeseries] = useState<TimeseriesPoint[]>([]);
  const [sites, setSites] = useState<Site[]>([]);
  const [campaigns, setCampaigns] = useState<Campaign[]>([]);
  const [auditLogs, setAuditLogs] = useState<AuditLogItem[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      try {
        const [sumRes, tsRes, sitesRes, campsRes, logsRes] = await Promise.all([
          api.getAnalyticsSummary(),
          api.getAnalyticsTimeseries({ days: 7 }),
          api.getSites(),
          api.getCampaigns(),
          isAdmin ? api.getAuditLogs(5) : Promise.resolve([] as AuditLogItem[]),
        ]);
        setSummary(sumRes);
        setTimeseries(tsRes);
        setSites(sitesRes);
        setCampaigns(campsRes);
        setAuditLogs(logsRes);
      } catch (err) {
        console.error('Failed to load overview data', err);
      } finally {
        setLoading(false);
      }
    };
    load();
  }, [isAdmin]);

  const activeCampaignsCount = campaigns.filter((c) => c.status === 'active').length;

  if (loading) {
    return (
      <div className="flex items-center justify-center h-64 text-slate-500">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-blue-600"></div>
      </div>
    );
  }

  // Find max value in timeseries for relative bar height
  const maxEvents = Math.max(...timeseries.map((t) => Math.max(t.impressions, t.clicks)), 10);

  return (
    <div className="flex flex-col gap-8">
      {/* Header with Quick Actions */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Platform Overview</h2>
          <p className="text-sm text-slate-500">
            Real-time delivery telemetry and multi-site advertising performance
          </p>
        </div>

        {canWrite ? (
          <div className="flex items-center gap-2">
            <button
              onClick={onOpenCreateSite}
              className="flex items-center gap-1.5 px-3 py-2 bg-white hover:bg-slate-50 border border-slate-200 text-slate-700 font-semibold text-xs rounded-lg shadow-sm transition-all"
            >
              <Plus className="w-4 h-4 text-slate-500" />
              <span>Add Website</span>
            </button>
            <button
              onClick={onOpenCreatePlacement}
              className="flex items-center gap-1.5 px-3 py-2 bg-white hover:bg-slate-50 border border-slate-200 text-slate-700 font-semibold text-xs rounded-lg shadow-sm transition-all"
            >
              <Plus className="w-4 h-4 text-slate-500" />
              <span>New Placement</span>
            </button>
            <button
              onClick={onOpenCreateAd}
              className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg shadow-sm shadow-blue-500/20 transition-all"
            >
              <Plus className="w-4 h-4" />
              <span>Create Ad</span>
            </button>
          </div>
        ) : (
          <span className="text-xs font-medium text-slate-500 bg-slate-100 border border-slate-200 px-3 py-2 rounded-lg">
            Analyst access · read-only reporting
          </span>
        )}
      </div>

      {/* KPI Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-6 gap-4">
        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Active Campaigns
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">{activeCampaignsCount}</div>
            <div className="text-xs text-slate-400 mt-0.5">
              out of {campaigns.length} total campaigns
            </div>
          </div>
          <div className="w-12 h-12 bg-blue-50 rounded-xl flex items-center justify-center text-blue-600">
            <Megaphone className="w-6 h-6" />
          </div>
        </div>

        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Total Impressions
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">
              {summary.impressions.toLocaleString()}
            </div>
            <div className="text-xs text-slate-400 mt-0.5">Deduplicated visibility events</div>
          </div>
          <div className="w-12 h-12 bg-indigo-50 rounded-xl flex items-center justify-center text-indigo-600">
            <Eye className="w-6 h-6" />
          </div>
        </div>

        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Total Clicks
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">
              {summary.clicks.toLocaleString()}
            </div>
            <div className="text-xs text-slate-400 mt-0.5">Validated ad destination redirects</div>
          </div>
          <div className="w-12 h-12 bg-emerald-50 rounded-xl flex items-center justify-center text-emerald-600">
            <MousePointerClick className="w-6 h-6" />
          </div>
        </div>

        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Outbound Redirects
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">
              {(summary.outbound_redirects || 0).toLocaleString()}
            </div>
            <div className="text-xs text-slate-400 mt-0.5">Off-site visits via first-party /r/</div>
          </div>
          <div className="w-12 h-12 bg-sky-50 rounded-xl flex items-center justify-center text-sky-600">
            <ExternalLink className="w-6 h-6" />
          </div>
        </div>

        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Affiliate Clicks
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">
              {(summary.affiliate_clicks || 0).toLocaleString()}
            </div>
            <div className="text-xs text-slate-400 mt-0.5">Observed only; commissions not connected</div>
          </div>
          <div className="w-12 h-12 bg-teal-50 rounded-xl flex items-center justify-center text-teal-600">
            <Ticket className="w-6 h-6" />
          </div>
        </div>

        <div className="bg-white p-5 rounded-xl border border-slate-200 shadow-sm flex items-center justify-between">
          <div>
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">
              Average CTR
            </div>
            <div className="text-2xl font-bold text-slate-900 mt-1">
              {summary.ctr.toFixed(2)}%
            </div>
            <div className="text-xs text-slate-400 mt-0.5">Click-Through Rate estimate</div>
          </div>
          <div className="w-12 h-12 bg-amber-50 rounded-xl flex items-center justify-center text-amber-600">
            <Percent className="w-6 h-6" />
          </div>
        </div>
      </div>

      {/* Daily Performance Trend Chart */}
      <div className="bg-white p-6 rounded-xl border border-slate-200 shadow-sm flex flex-col gap-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <TrendingUp className="w-5 h-5 text-blue-600" />
            <h3 className="font-semibold text-slate-900">7-Day Delivery & Engagement Activity</h3>
          </div>
          <button
            onClick={() => onNavigate('analytics')}
            className="text-xs text-blue-600 hover:text-blue-700 font-semibold flex items-center gap-1"
          >
            <span>Detailed Analytics</span>
            <ArrowUpRight className="w-3.5 h-3.5" />
          </button>
        </div>

        {timeseries.length === 0 ? (
          <div className="h-44 flex flex-col items-center justify-center text-slate-400 text-sm">
            <span>No delivery event activity recorded yet.</span>
            <span className="text-xs text-slate-400 mt-1">
              Install the snippet or run the Interactive Sandbox to generate live impressions!
            </span>
          </div>
        ) : (
          <div className="flex flex-col gap-3">
            <div className="h-44 flex items-end gap-3 pt-6 pb-2 border-b border-slate-100">
              {timeseries.map((pt) => {
                const impHeight = Math.max(8, Math.round((pt.impressions / maxEvents) * 100));
                const clkHeight = Math.max(8, Math.round((pt.clicks / maxEvents) * 100));

                return (
                  <div key={pt.date} className="flex-1 flex flex-col items-center gap-1.5 h-full justify-end group">
                    <div className="flex items-end gap-1 w-full justify-center h-full">
                      {/* Impressions bar */}
                      <div
                        style={{ height: `${impHeight}%` }}
                        className="w-1/2 max-w-[20px] bg-blue-500 rounded-t group-hover:bg-blue-600 transition-all relative"
                        title={`${pt.date}: ${pt.impressions} Impressions`}
                      />
                      {/* Clicks bar */}
                      <div
                        style={{ height: `${clkHeight}%` }}
                        className="w-1/2 max-w-[20px] bg-emerald-500 rounded-t group-hover:bg-emerald-600 transition-all relative"
                        title={`${pt.date}: ${pt.clicks} Clicks`}
                      />
                    </div>
                    <span className="text-[10px] text-slate-400 font-medium truncate w-full text-center">
                      {pt.date.slice(5)}
                    </span>
                  </div>
                );
              })}
            </div>
            <div className="flex items-center justify-center gap-6 text-xs text-slate-500">
              <span className="flex items-center gap-1.5">
                <span className="w-3 h-3 bg-blue-500 rounded"></span> Impressions
              </span>
              <span className="flex items-center gap-1.5">
                <span className="w-3 h-3 bg-emerald-500 rounded"></span> Clicks
              </span>
            </div>
          </div>
        )}
      </div>

      {/* Grid: Registered Sites + Recent Activity */}
      <div className={`grid grid-cols-1 gap-6 ${isAdmin ? 'lg:grid-cols-3' : 'lg:grid-cols-1'}`}>
        {/* Websites List */}
        <div
          className={`bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden flex flex-col ${
            isAdmin ? 'lg:col-span-2' : ''
          }`}
        >
          <div className="px-6 py-4 border-b border-slate-100 flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Globe className="w-4 h-4 text-slate-600" />
              <h3 className="font-semibold text-slate-900">Registered Websites</h3>
            </div>
            <button
              onClick={() => onNavigate('websites')}
              className="text-xs text-blue-600 hover:text-blue-700 font-semibold flex items-center gap-1"
            >
              <span>Manage All</span>
              <ArrowUpRight className="w-3.5 h-3.5" />
            </button>
          </div>

          <div className="divide-y divide-slate-100">
            {sites.length === 0 ? (
              <div className="p-8 text-center text-slate-400 text-sm">
                No websites registered yet. Click "Add Website" to get your first public site key.
              </div>
            ) : (
              sites.slice(0, 5).map((s) => (
                <div key={s.id} className="p-4 px-6 flex items-center justify-between hover:bg-slate-50/60 transition-colors">
                  <div>
                    <div className="font-semibold text-sm text-slate-900 flex items-center gap-2">
                      {s.name}
                      <span
                        className={`text-[10px] font-semibold px-2 py-0.5 rounded-full ${
                          s.status === 'active'
                            ? 'bg-emerald-50 text-emerald-700 border border-emerald-200'
                            : 'bg-slate-100 text-slate-500'
                        }`}
                      >
                        {s.status}
                      </span>
                    </div>
                    <div className="text-xs text-slate-500 mt-0.5">{s.primary_domain}</div>
                  </div>

                  <div className="text-right">
                    <code className="text-xs bg-slate-100 text-slate-700 px-2 py-1 rounded font-mono">
                      {s.public_site_key}
                    </code>
                    <div className="text-[10px] text-slate-400 mt-1">Config v{s.config_version}</div>
                  </div>
                </div>
              ))
            )}
          </div>
        </div>

        {isAdmin && (
        <div className="bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden flex flex-col">
          <div className="px-6 py-4 border-b border-slate-100 flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Clock className="w-4 h-4 text-slate-600" />
              <h3 className="font-semibold text-slate-900">Recent Admin Audit</h3>
            </div>
            <button
              onClick={() => onNavigate('audit')}
              className="text-xs text-blue-600 hover:text-blue-700 font-semibold"
            >
              View Full
            </button>
          </div>

          <div className="divide-y divide-slate-100 flex-1">
            {auditLogs.length === 0 ? (
              <div className="p-6 text-center text-slate-400 text-xs">No admin actions recorded yet.</div>
            ) : (
              auditLogs.map((log) => (
                <div key={log.id} className="p-3 px-6 text-xs flex flex-col gap-1">
                  <div className="flex items-center justify-between">
                    <span className="font-semibold text-slate-800 text-[11px] bg-slate-100 px-1.5 py-0.5 rounded">
                      {log.action}
                    </span>
                    <span className="text-[10px] text-slate-400">{log.created_at.slice(11, 19)}</span>
                  </div>
                  <div className="text-slate-500 truncate">
                    by <span className="font-medium text-slate-700">{log.actor}</span> ({log.entity_type})
                  </div>
                </div>
              ))
            )}
          </div>
        </div>
        )}
      </div>
    </div>
  );
};
