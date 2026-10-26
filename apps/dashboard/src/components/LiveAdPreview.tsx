import React from 'react';
import { ExternalLink, ShieldCheck, AlertTriangle, ShoppingBag } from 'lucide-react';
import { AdFormat } from '../types';

interface LiveAdPreviewProps {
  format: AdFormat;
  title?: string;
  body?: string;
  imageUrl?: string;
  destinationUrl?: string;
  ctaLabel?: string;
  altText?: string;
  merchant?: string;
  priceText?: string;
  disclosureText?: string;
}

export const LiveAdPreview: React.FC<LiveAdPreviewProps> = ({
  format,
  title = 'Sample Headline Offer',
  body = 'Explore premium services and special deals designed for your business growth.',
  imageUrl,
  destinationUrl = 'https://example.com',
  ctaLabel = 'Learn More',
  altText = 'Ad creative',
  merchant,
  priceText,
  disclosureText = 'Affiliate disclosure: we may earn a commission from purchases made through this link.',
}) => {
  const isHttps = destinationUrl.startsWith('https://');
  const isUnsafe =
    destinationUrl.toLowerCase().startsWith('javascript:') ||
    destinationUrl.toLowerCase().startsWith('data:');

  return (
    <div className="bg-slate-50 border border-slate-200 rounded-xl p-4 flex flex-col gap-3">
      <div className="flex items-center justify-between text-xs text-slate-500 font-medium pb-2 border-b border-slate-200">
        <span className="flex items-center gap-1.5 uppercase tracking-wider font-semibold text-slate-600">
          <span className="w-2 h-2 rounded-full bg-blue-500 animate-pulse"></span>
          Live Creative Preview ({format.replace(/_/g, ' ')})
        </span>

        <span className="flex items-center gap-1">
          {isUnsafe ? (
            <span className="text-rose-600 flex items-center gap-1 font-semibold">
              <AlertTriangle className="w-3.5 h-3.5" /> Unsafe URL
            </span>
          ) : isHttps ? (
            <span className="text-emerald-600 flex items-center gap-1">
              <ShieldCheck className="w-3.5 h-3.5" /> Secure HTTPS
            </span>
          ) : (
            <span className="text-amber-600 flex items-center gap-1">
              <AlertTriangle className="w-3.5 h-3.5" /> HTTP (HTTPS Recommended)
            </span>
          )}
        </span>
      </div>

      {/* Render Format Container */}
      <div className="bg-white rounded-lg p-3 border border-slate-200 shadow-sm">
        {format === 'responsive_banner' || format === 'image' ? (
          <div className="relative group block rounded-md overflow-hidden bg-slate-100 border border-slate-200">
            {imageUrl ? (
              <img
                src={imageUrl}
                alt={altText || title}
                className="w-full h-auto max-h-56 object-cover"
                onError={(e) => {
                  (e.target as HTMLImageElement).src =
                    'https://via.placeholder.com/728x90/e2e8f0/475569?text=Image+Not+Found';
                }}
              />
            ) : (
              <div className="w-full h-36 bg-gradient-to-r from-blue-50 to-indigo-50 flex flex-col items-center justify-center text-slate-400 p-4 text-center">
                <span className="text-sm font-semibold text-slate-600 mb-1">{title}</span>
                <span className="text-xs">Provide an image URL to preview banner graphic</span>
              </div>
            )}
            <span className="absolute top-2 right-2 bg-slate-900/80 text-white text-[10px] font-bold px-2 py-0.5 rounded tracking-wider uppercase">
              Ad
            </span>
          </div>
        ) : format === 'native_card' ? (
          <div className="border border-slate-200 rounded-lg overflow-hidden bg-white max-w-sm">
            {imageUrl && (
              <div className="relative">
                <img
                  src={imageUrl}
                  alt={altText || title}
                  className="w-full h-40 object-cover"
                  onError={(e) => {
                    (e.target as HTMLImageElement).src =
                      'https://via.placeholder.com/400x200/e2e8f0/475569?text=Creative+Asset';
                  }}
                />
                <span className="absolute top-2 right-2 bg-slate-900/80 text-white text-[10px] font-bold px-2 py-0.5 rounded tracking-wider uppercase">
                  Sponsored
                </span>
              </div>
            )}
            <div className="p-4 flex flex-col gap-2">
              <h4 className="text-base font-semibold text-slate-900 leading-snug">{title}</h4>
              <p className="text-xs text-slate-600 line-clamp-2 leading-relaxed">{body}</p>
              <div className="pt-2 flex items-center justify-between">
                <span className="inline-block bg-blue-600 text-white text-xs font-semibold px-3 py-1.5 rounded-md hover:bg-blue-700 transition-colors">
                  {ctaLabel || 'Learn More'}
                </span>
                <span className="text-[11px] text-slate-400 flex items-center gap-1 truncate max-w-[140px]">
                  {destinationUrl} <ExternalLink className="w-3 h-3 shrink-0" />
                </span>
              </div>
            </div>
          </div>
        ) : format === 'product_card' ? (
          <div className="border border-slate-200 rounded-lg overflow-hidden bg-white max-w-sm">
            <div className="flex items-center justify-between px-3 py-2 bg-slate-50 border-b border-slate-200">
              <span className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wider text-slate-500">
                <ShoppingBag className="w-3 h-3" />
                Sponsored product
              </span>
              {merchant && (
                <span className="text-[10px] font-semibold text-emerald-700 truncate max-w-[140px]">{merchant}</span>
              )}
            </div>
            {imageUrl ? (
              <img
                src={imageUrl}
                alt={altText || title}
                className="w-full h-40 object-cover bg-slate-100"
                onError={(e) => {
                  (e.target as HTMLImageElement).style.opacity = '0.25';
                }}
              />
            ) : (
              <div className="w-full h-32 bg-gradient-to-r from-emerald-50 to-teal-50 flex items-center justify-center text-xs text-slate-400">
                Product image required
              </div>
            )}
            <div className="p-4 flex flex-col gap-2">
              <h4 className="text-base font-semibold text-slate-900 leading-snug">{title}</h4>
              {body && <p className="text-xs text-slate-600 line-clamp-2 leading-relaxed">{body}</p>}
              <div className="flex items-center justify-between gap-2 pt-1">
                {priceText ? (
                  <span className="text-sm font-bold text-slate-900">{priceText}</span>
                ) : (
                  <span className="text-[11px] text-slate-400">Price not provided</span>
                )}
                <span className="inline-block bg-emerald-600 text-white text-xs font-semibold px-3 py-1.5 rounded-md">
                  {ctaLabel || 'View offer'}
                </span>
              </div>
              <p className="text-[10px] leading-snug text-slate-500 border-t border-slate-100 pt-2">
                {disclosureText}
              </p>
            </div>
          </div>
        ) : (
          <div className="flex items-center gap-2 p-2">
            <span className="bg-slate-100 text-slate-600 text-[10px] font-semibold uppercase px-1.5 py-0.5 rounded border border-slate-200">
              Ad
            </span>
            <span className="text-sm font-semibold text-blue-600 underline flex items-center gap-1 hover:text-blue-700">
              {title} <ExternalLink className="w-3.5 h-3.5" />
            </span>
          </div>
        )}
      </div>

      <div className="text-[11px] text-slate-500 truncate">
        <span className="font-medium text-slate-700">Target:</span> {destinationUrl}
      </div>
    </div>
  );
};
