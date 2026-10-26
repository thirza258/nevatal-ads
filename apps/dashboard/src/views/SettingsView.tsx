import React, { useEffect, useState } from 'react';
import { api } from '../api';
import { AppSettings } from '../types';
import {
  DatabaseBackup,
  KeyRound,
  Loader2,
  RefreshCw,
  ServerCog,
  ShieldAlert,
  ShieldCheck,
} from 'lucide-react';

interface SettingsViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
  onSessionInvalidated: () => void;
}

export const SettingsView: React.FC<SettingsViewProps> = ({ onNotify, onSessionInvalidated }) => {
  const [settings, setSettings] = useState<AppSettings | null>(null);
  const [loading, setLoading] = useState(true);
  const [currentPassword, setCurrentPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [submitting, setSubmitting] = useState(false);

  const loadSettings = async () => {
    setLoading(true);
    try {
      setSettings(await api.getSettings());
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load settings');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadSettings();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const handlePasswordChange = async (event: React.FormEvent) => {
    event.preventDefault();
    if (newPassword.length < 12) {
      onNotify('error', 'The new password must be at least 12 characters.');
      return;
    }
    if (newPassword !== confirmPassword) {
      onNotify('error', 'The new passwords do not match.');
      return;
    }
    setSubmitting(true);
    try {
      const result = await api.changePassword(currentPassword, newPassword);
      onNotify('success', result.message || 'Password changed. All sessions were invalidated.');
      setCurrentPassword('');
      setNewPassword('');
      setConfirmPassword('');
      // The API invalidated every session, including this one.
      window.setTimeout(onSessionInvalidated, 1200);
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to change password');
    } finally {
      setSubmitting(false);
    }
  };

  const sdkUrl = settings ? `${window.location.origin}${settings.sdk_url}` : '';

  return (
    <div className="flex flex-col gap-6">
      <div className="flex items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Settings</h2>
          <p className="text-sm text-slate-500 mt-1">
            Runtime configuration and account security. Server secrets are never sent to the browser.
          </p>
        </div>
        <button
          onClick={loadSettings}
          disabled={loading}
          className="inline-flex items-center gap-1.5 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-60 px-3 py-2 rounded-lg transition-colors"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
        </button>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-5">
          <div className="flex items-center gap-2">
            <ServerCog className="w-5 h-5 text-slate-500" />
            <h3 className="text-base font-semibold text-slate-900">Deployment</h3>
          </div>

          {loading && !settings ? (
            <p className="text-sm text-slate-500">Loading configuration…</p>
          ) : settings ? (
            <>
              <div className="grid grid-cols-2 gap-4 text-sm">
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">Environment</div>
                  <div className="mt-1 text-slate-800 font-medium">{settings.environment}</div>
                </div>
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">Timezone</div>
                  <div className="mt-1 text-slate-800 font-medium">{settings.timezone}</div>
                </div>
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">SDK version</div>
                  <div className="mt-1 text-slate-800 font-medium">v{settings.sdk_version}</div>
                </div>
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">Config cache</div>
                  <div className="mt-1 text-slate-800 font-medium">{settings.config_cache_seconds}s</div>
                </div>
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">Event retention</div>
                  <div className="mt-1 text-slate-800 font-medium">{settings.event_retention_days} days</div>
                </div>
                <div>
                  <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold">Click token TTL</div>
                  <div className="mt-1 text-slate-800 font-medium">
                    {Math.round(settings.click_token_ttl_seconds / 3600)} hours
                  </div>
                </div>
              </div>

              <div>
                <div className="text-[11px] uppercase tracking-wider text-slate-400 font-semibold mb-1.5">SDK URL</div>
                <code className="block text-xs bg-slate-50 border border-slate-200 rounded-lg px-3 py-2 text-slate-700 break-all">
                  {sdkUrl}
                </code>
              </div>

              <div className="rounded-lg bg-slate-50 border border-slate-200 p-3 text-xs text-slate-600 leading-relaxed">
                Rate limits: delivery {settings.delivery_rate_limit_per_minute}/min per IP+site key, events{' '}
                {settings.event_rate_limit_per_minute}/min per IP+site key. Config max stale{' '}
                {settings.max_stale_seconds}s.
              </div>
            </>
          ) : (
            <p className="text-sm text-rose-600">Settings could not be loaded.</p>
          )}
        </div>

        <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-4">
          <div className="flex items-center gap-2">
            <KeyRound className="w-5 h-5 text-slate-500" />
            <h3 className="text-base font-semibold text-slate-900">Change password</h3>
          </div>
          <p className="text-xs text-slate-500 leading-relaxed">
            Changing the password signs out every active admin session, including this one. Use at least 12
            characters and store it in a password manager.
          </p>

          <form onSubmit={handlePasswordChange} className="flex flex-col gap-3">
            <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              Current password
              <input
                type="password"
                autoComplete="current-password"
                required
                value={currentPassword}
                onChange={(event) => setCurrentPassword(event.target.value)}
                className="px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-normal text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </label>
            <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              New password (12+ characters)
              <input
                type="password"
                autoComplete="new-password"
                minLength={12}
                maxLength={128}
                required
                value={newPassword}
                onChange={(event) => setNewPassword(event.target.value)}
                className="px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-normal text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </label>
            <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              Confirm new password
              <input
                type="password"
                autoComplete="new-password"
                minLength={12}
                maxLength={128}
                required
                value={confirmPassword}
                onChange={(event) => setConfirmPassword(event.target.value)}
                className="px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-normal text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </label>
            <button
              type="submit"
              disabled={submitting}
              className="mt-1 inline-flex items-center justify-center gap-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-60 text-white text-sm font-semibold px-4 py-2 rounded-lg transition-colors"
            >
              {submitting ? <Loader2 className="w-4 h-4 animate-spin" /> : <ShieldCheck className="w-4 h-4" />}
              {submitting ? 'Updating…' : 'Update password'}
            </button>
          </form>
        </div>

        <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-3 lg:col-span-2">
          <div className="flex items-center gap-2">
            <DatabaseBackup className="w-5 h-5 text-slate-500" />
            <h3 className="text-base font-semibold text-slate-900">Database backups</h3>
          </div>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-sm text-slate-600 leading-relaxed">
            <p>
              The API takes verified online SQLite backups every{' '}
              <strong className="text-slate-800">BACKUP_INTERVAL_HOURS</strong> (default 24h) into{' '}
              <code className="text-xs bg-slate-100 px-1.5 py-0.5 rounded">/data/backups</code>, keeping{' '}
              <strong className="text-slate-800">BACKUP_RETENTION</strong> copies. Backups are available on the
              host under <code className="text-xs bg-slate-100 px-1.5 py-0.5 rounded">./data/backups</code>.
            </p>
            <p>
              Take an on-demand snapshot with <code className="text-xs bg-slate-100 px-1.5 py-0.5 rounded">./scripts/backup.sh</code>{' '}
              and restore with <code className="text-xs bg-slate-100 px-1.5 py-0.5 rounded">./scripts/restore.sh &lt;file&gt;</code>.
              Copy backups off the host and encrypt them. RAID is not a substitute for backups.
            </p>
          </div>
          <div className="flex items-start gap-2 rounded-lg border border-amber-200 bg-amber-50 p-3 text-xs text-amber-900 leading-relaxed">
            <ShieldAlert className="w-4 h-4 shrink-0 mt-0.5" />
            <span>
              Server-side <code>SECRET_KEY</code>, <code>ADMIN_PASSWORD</code>, and backup configuration are
              managed through the deployment environment, never in the dashboard.
            </span>
          </div>
        </div>
      </div>
    </div>
  );
};
