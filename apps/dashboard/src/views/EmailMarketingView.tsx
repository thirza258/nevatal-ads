import React, { useEffect, useState } from 'react';
import { api } from '../api';
import { EmailSettings, SmtpEncryption } from '../types';
import {
  AlertCircle,
  CheckCircle2,
  Eye,
  EyeOff,
  Loader2,
  Mail,
  RefreshCw,
  Save,
  Send,
  Server,
  ShieldCheck,
  Trash2,
} from 'lucide-react';

interface EmailMarketingViewProps {
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

type PasswordAction = 'keep' | 'replace' | 'clear';

interface FormState {
  enabled: boolean;
  host: string;
  port: string;
  encryption: SmtpEncryption;
  username: string;
  from_email: string;
  from_name: string;
  reply_to: string;
}

const EMPTY_FORM: FormState = {
  enabled: false,
  host: '',
  port: '587',
  encryption: 'starttls',
  username: '',
  from_email: '',
  from_name: '',
  reply_to: '',
};

const inputClass =
  'px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-normal text-slate-900 focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500 disabled:opacity-60';

function formFromSettings(settings: EmailSettings): FormState {
  return {
    enabled: settings.enabled,
    host: settings.host,
    port: String(settings.port || 587),
    encryption: (settings.encryption as SmtpEncryption) || 'starttls',
    username: settings.username,
    from_email: settings.from_email,
    from_name: settings.from_name,
    reply_to: settings.reply_to,
  };
}

function looksLikeEmail(value: string): boolean {
  return /^[^\s@]+@[^\s@]+$/.test(value.trim());
}

export const EmailMarketingView: React.FC<EmailMarketingViewProps> = ({ onNotify }) => {
  const [settings, setSettings] = useState<EmailSettings | null>(null);
  const [form, setForm] = useState<FormState>(EMPTY_FORM);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [password, setPassword] = useState('');
  const [passwordAction, setPasswordAction] = useState<PasswordAction>('keep');
  const [showPassword, setShowPassword] = useState(false);
  const [testTo, setTestTo] = useState('');
  const [testing, setTesting] = useState(false);
  const [testStatus, setTestStatus] = useState<{ type: 'success' | 'error'; message: string } | null>(
    null,
  );

  const applySettings = (next: EmailSettings) => {
    setSettings(next);
    setForm(formFromSettings(next));
    setTestTo((current) => current || next.from_email);
    setPassword('');
    setPasswordAction('keep');
    setShowPassword(false);
  };

  const loadSettings = async () => {
    setLoading(true);
    try {
      applySettings(await api.getEmailSettings());
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load email settings');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadSettings();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const updateField = <K extends keyof FormState>(key: K, value: FormState[K]) => {
    setForm((current) => ({ ...current, [key]: value }));
  };

  const validate = (): string | null => {
    const host = form.host.trim();
    const fromEmail = form.from_email.trim();
    if (!form.enabled && !host && !fromEmail) return null;

    if (!host) return 'Enter the SMTP server hostname.';
    if (!/^[A-Za-z0-9.-]+$/.test(host)) return 'The SMTP host may only contain letters, digits, dots, and hyphens.';
    const port = Number(form.port);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      return 'The SMTP port must be a whole number between 1 and 65535.';
    }
    if (!looksLikeEmail(fromEmail)) return 'Enter a valid sender email address.';
    if (form.reply_to.trim() && !looksLikeEmail(form.reply_to)) {
      return 'Enter a valid Reply-To address, or leave it blank.';
    }
    if (passwordAction === 'replace' && password.length > 512) {
      return 'The SMTP password is too long.';
    }
    if (form.enabled && form.username.trim() && passwordAction === 'keep' && !settings?.password_set) {
      return 'Enter the SMTP password for the configured username.';
    }
    if (
      form.enabled &&
      !form.username.trim() &&
      (passwordAction === 'replace' || (settings?.password_set && passwordAction !== 'clear'))
    ) {
      return 'Enter an SMTP username or remove the stored password.';
    }
    return null;
  };

  const handleSave = async (event: React.FormEvent) => {
    event.preventDefault();
    const problem = validate();
    if (problem) {
      onNotify('error', problem);
      return;
    }

    setSaving(true);
    try {
      const payload = {
        enabled: form.enabled,
        host: form.host.trim(),
        port: Number(form.port),
        encryption: form.encryption,
        username: form.username.trim(),
        from_email: form.from_email.trim(),
        from_name: form.from_name.trim(),
        reply_to: form.reply_to.trim(),
        ...(passwordAction === 'replace' ? { password } : {}),
        ...(passwordAction === 'clear' ? { password: '' } : {}),
      };
      const saved = await api.updateEmailSettings(payload);
      applySettings(saved);
      onNotify('success', 'Email settings saved.');
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to save email settings');
    } finally {
      setSaving(false);
    }
  };

  const handleSendTest = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!looksLikeEmail(testTo)) {
      setTestStatus({ type: 'error', message: 'Enter a valid recipient email address.' });
      return;
    }

    setTesting(true);
    setTestStatus(null);
    try {
      const result = await api.sendTestEmail(testTo.trim());
      setTestStatus({ type: 'success', message: result.message || `Test email sent to ${testTo.trim()}.` });
      onNotify('success', 'Test email sent.');
    } catch (err: any) {
      setTestStatus({ type: 'error', message: err.message || 'The test email could not be sent.' });
    } finally {
      setTesting(false);
    }
  };

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col sm:flex-row sm:items-end sm:justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Email Marketing</h2>
          <p className="text-sm text-slate-500 mt-1">
            SMTP transport for outbound campaigns. Credentials are encrypted at rest and never sent
            back to the browser.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={loadSettings}
            disabled={loading || saving}
            className="inline-flex items-center gap-1.5 text-xs font-semibold text-slate-700 bg-white border border-slate-200 hover:bg-slate-50 disabled:opacity-60 px-3 py-2 rounded-lg transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh
          </button>
          <button
            type="submit"
            form="email-settings-form"
            disabled={loading || saving}
            className="inline-flex items-center gap-1.5 bg-blue-600 hover:bg-blue-700 disabled:opacity-60 text-white text-sm font-semibold px-4 py-2 rounded-lg transition-colors"
          >
            {saving ? <Loader2 className="w-4 h-4 animate-spin" /> : <Save className="w-4 h-4" />}
            {saving ? 'Saving…' : 'Save changes'}
          </button>
        </div>
      </div>

      {settings?.updated_at && (
        <div className="flex flex-wrap items-center gap-x-4 gap-y-1 text-xs text-slate-500">
          <span className="inline-flex items-center gap-1.5">
            <span
              className={`inline-block h-2 w-2 rounded-full ${
                settings.enabled ? 'bg-emerald-500' : 'bg-amber-400'
              }`}
              aria-hidden="true"
            />
            {settings.enabled ? 'Sending enabled' : 'Sending disabled'}
          </span>
          <span>
            Last saved {new Date(settings.updated_at).toLocaleString()}
            {settings.updated_by ? ` by ${settings.updated_by}` : ''}
          </span>
        </div>
      )}

      <form id="email-settings-form" onSubmit={handleSave} className="grid grid-cols-1 xl:grid-cols-2 gap-6">
        <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-5">
          <div className="flex items-center gap-2">
            <Server className="w-5 h-5 text-slate-500" />
            <h3 className="text-base font-semibold text-slate-900">SMTP server</h3>
          </div>

          <label className="flex items-start gap-3 rounded-lg border border-slate-200 bg-slate-50 p-3">
            <input
              type="checkbox"
              checked={form.enabled}
              onChange={(event) => updateField('enabled', event.target.checked)}
              className="mt-0.5 h-4 w-4 rounded border-slate-300 text-blue-600 focus:ring-blue-500"
            />
            <span className="text-xs text-slate-600 leading-relaxed">
              <span className="block text-sm font-semibold text-slate-800">Enable email sending</span>
              Campaign email is only sent through this server while the switch is on. Test emails
              still work when it is off.
            </span>
          </label>

          <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
            <label className="sm:col-span-2 flex flex-col gap-1 text-xs font-semibold text-slate-700">
              Host
              <input
                type="text"
                autoComplete="off"
                placeholder="smtp.example.com"
                value={form.host}
                onChange={(event) => updateField('host', event.target.value)}
                className={inputClass}
              />
            </label>
            <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              Port
              <input
                type="number"
                min={1}
                max={65535}
                inputMode="numeric"
                value={form.port}
                onChange={(event) => updateField('port', event.target.value)}
                className={inputClass}
              />
            </label>
          </div>

          <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
            Encryption
            <select
              value={form.encryption}
              onChange={(event) => updateField('encryption', event.target.value as SmtpEncryption)}
              className={inputClass}
            >
              <option value="starttls">STARTTLS (usually port 587)</option>
              <option value="ssl">SSL/TLS (usually port 465)</option>
              <option value="none">None (not recommended)</option>
            </select>
          </label>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              Username
              <input
                type="text"
                autoComplete="username"
                placeholder="apikey or mailbox user"
                value={form.username}
                onChange={(event) => updateField('username', event.target.value)}
                className={inputClass}
              />
            </label>
            <div className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
              <label htmlFor="smtp-password">Password</label>
              <div className="relative">
                <input
                  id="smtp-password"
                  type={showPassword ? 'text' : 'password'}
                  autoComplete="new-password"
                  placeholder={
                    passwordAction === 'clear'
                      ? 'Stored password will be removed'
                      : settings?.password_set
                        ? 'Stored password — leave blank to keep'
                        : 'SMTP password'
                  }
                  value={password}
                  onChange={(event) => {
                    setPassword(event.target.value);
                    setPasswordAction(event.target.value ? 'replace' : 'keep');
                  }}
                  className={`${inputClass} w-full pr-10`}
                />
                <button
                  type="button"
                  onClick={() => setShowPassword((current) => !current)}
                  aria-label={showPassword ? 'Hide password' : 'Show password'}
                  className="absolute inset-y-0 right-0 flex w-10 items-center justify-center text-slate-400 hover:text-slate-600"
                >
                  {showPassword ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
                </button>
              </div>
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-x-4 gap-y-2 text-xs">
            <span className="inline-flex items-center gap-1.5 text-slate-500">
              <ShieldCheck className="w-3.5 h-3.5 text-emerald-600" />
              {settings?.password_set && passwordAction !== 'clear'
                ? 'A password is stored (encrypted).'
                : 'No password stored.'}
            </span>
            {settings?.password_set && passwordAction !== 'clear' && (
              <button
                type="button"
                onClick={() => {
                  // The API requires credentials while sending is enabled.
                  if (form.enabled && form.username.trim()) {
                    onNotify('info', 'Turn off email sending before removing the stored password.');
                    return;
                  }
                  setPassword('');
                  setPasswordAction('clear');
                }}
                className="inline-flex items-center gap-1 font-semibold text-rose-600 hover:text-rose-700"
              >
                <Trash2 className="w-3.5 h-3.5" /> Remove password
              </button>
            )}
            {passwordAction === 'clear' && (
              <span className="inline-flex items-center gap-2 text-rose-600">
                Password will be cleared when you save.
                <button
                  type="button"
                  onClick={() => setPasswordAction('keep')}
                  className="font-semibold underline hover:no-underline"
                >
                  Undo
                </button>
              </span>
            )}
          </div>

          <p className="text-[11px] leading-relaxed text-slate-500">
            Leave the username blank for relays that allow your server IP without authentication.
            Passwords are encrypted with the deployment <code>SECRET_KEY</code>; rotating that key
            requires re-entering the password here.
          </p>
        </div>

        <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-5">
          <div className="flex items-center gap-2">
            <Mail className="w-5 h-5 text-slate-500" />
            <h3 className="text-base font-semibold text-slate-900">Sender identity</h3>
          </div>

          <p className="text-xs text-slate-500 leading-relaxed">
            This name and address appear in the From header of every marketing email. Use a domain
            you control and publish SPF/DKIM/DMARC records with your SMTP provider for deliverability.
          </p>

          <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
            From name
            <input
              type="text"
              maxLength={128}
              placeholder="AdControl Marketing"
              value={form.from_name}
              onChange={(event) => updateField('from_name', event.target.value)}
              className={inputClass}
            />
          </label>
          <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
            From email
            <input
              type="email"
              autoComplete="off"
              placeholder="newsletter@example.com"
              value={form.from_email}
              onChange={(event) => updateField('from_email', event.target.value)}
              className={inputClass}
            />
          </label>
          <label className="flex flex-col gap-1 text-xs font-semibold text-slate-700">
            Reply-To (optional)
            <input
              type="email"
              autoComplete="off"
              placeholder="replies@example.com"
              value={form.reply_to}
              onChange={(event) => updateField('reply_to', event.target.value)}
              className={inputClass}
            />
          </label>
        </div>
      </form>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm p-6 flex flex-col gap-4">
        <div className="flex items-center gap-2">
          <Send className="w-5 h-5 text-slate-500" />
          <h3 className="text-base font-semibold text-slate-900">Send a test email</h3>
        </div>
        <p className="text-xs text-slate-500 leading-relaxed">
          Verifies the saved configuration end to end. Test sends are rate limited to five per
          minute and use TLS certificate verification.
        </p>

        <form onSubmit={handleSendTest} className="flex flex-col sm:flex-row gap-3 sm:items-end">
          <label className="flex flex-1 flex-col gap-1 text-xs font-semibold text-slate-700">
            Recipient
            <input
              type="email"
              autoComplete="off"
              placeholder="you@example.com"
              value={testTo}
              onChange={(event) => setTestTo(event.target.value)}
              className={inputClass}
            />
          </label>
          <button
            type="submit"
            disabled={testing || loading}
            className="inline-flex items-center justify-center gap-2 bg-slate-900 hover:bg-slate-800 disabled:opacity-60 text-white text-sm font-semibold px-4 py-2 rounded-lg transition-colors"
          >
            {testing ? <Loader2 className="w-4 h-4 animate-spin" /> : <Send className="w-4 h-4" />}
            {testing ? 'Sending…' : 'Send test'}
          </button>
        </form>

        {testStatus && (
          <div
            role="status"
            className={`flex items-start gap-2 rounded-lg border p-3 text-xs leading-relaxed ${
              testStatus.type === 'success'
                ? 'border-emerald-200 bg-emerald-50 text-emerald-900'
                : 'border-rose-200 bg-rose-50 text-rose-900'
            }`}
          >
            {testStatus.type === 'success' ? (
              <CheckCircle2 className="w-4 h-4 shrink-0 mt-0.5" />
            ) : (
              <AlertCircle className="w-4 h-4 shrink-0 mt-0.5" />
            )}
            <span>{testStatus.message}</span>
          </div>
        )}
      </div>
    </div>
  );
};
