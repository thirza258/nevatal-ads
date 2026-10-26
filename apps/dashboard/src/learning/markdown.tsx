import React, { ReactNode, useMemo } from 'react';

/**
 * Safe Markdown renderer.
 *
 * It never injects HTML and never uses dangerouslySetInnerHTML. Raw HTML in a
 * lesson is displayed as text, which is intentional: lesson authors get
 * headings, emphasis, links, lists, quotes, and code, not an HTML surface.
 */
export function safeLinkHref(raw: string): string | null {
  const value = raw.trim();
  if (!value) return null;
  if (value.startsWith('/') && !value.startsWith('//')) return value;
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:') return null;
    if (url.username || url.password) return null;
    return url.toString();
  } catch (_) {
    return null;
  }
}

function renderInline(text: string, keyPrefix: string): ReactNode[] {
  const nodes: ReactNode[] = [];
  let buffer = '';
  let index = 0;
  let nodeIndex = 0;

  const flush = () => {
    if (!buffer) return;
    nodes.push(buffer);
    buffer = '';
  };

  while (index < text.length) {
    const char = text[index];

    if (char === '`') {
      const end = text.indexOf('`', index + 1);
      if (end > index + 1) {
        flush();
        nodes.push(
          <code key={`${keyPrefix}-code-${nodeIndex++}`} className="rounded bg-slate-100 px-1.5 py-0.5 text-[0.9em] text-slate-800">
            {text.slice(index + 1, end)}
          </code>,
        );
        index = end + 1;
        continue;
      }
    }

    if (char === '*' && text[index + 1] === '*') {
      const end = text.indexOf('**', index + 2);
      if (end > index + 2) {
        flush();
        nodes.push(
          <strong key={`${keyPrefix}-strong-${nodeIndex++}`} className="font-semibold text-slate-950">
            {renderInline(text.slice(index + 2, end), `${keyPrefix}-s${nodeIndex}`)}
          </strong>,
        );
        index = end + 2;
        continue;
      }
    }

    if (char === '*') {
      const end = text.indexOf('*', index + 1);
      const inner = end > index + 1 ? text.slice(index + 1, end) : '';
      if (inner && !/^\s|\s$/.test(inner)) {
        flush();
        nodes.push(
          <em key={`${keyPrefix}-em-${nodeIndex++}`}>
            {renderInline(inner, `${keyPrefix}-e${nodeIndex}`)}
          </em>,
        );
        index = end + 1;
        continue;
      }
    }

    if (char === '[') {
      const match = text.slice(index).match(/^\[([^\]]+)\]\(([^)\s]+)\)/);
      if (match) {
        flush();
        const href = safeLinkHref(match[2]);
        if (href) {
          nodes.push(
            <a
              key={`${keyPrefix}-link-${nodeIndex++}`}
              href={href}
              className="font-medium text-teal-800 underline decoration-teal-300 underline-offset-2 hover:text-teal-950"
              {...(href.startsWith('https://') ? { target: '_blank', rel: 'noopener noreferrer' } : {})}
            >
              {renderInline(match[1], `${keyPrefix}-l${nodeIndex}`)}
            </a>,
          );
        } else {
          nodes.push(match[1]);
        }
        index += match[0].length;
        continue;
      }
    }

    buffer += char;
    index += 1;
  }

  flush();
  return nodes;
}

function startsBlock(line: string): boolean {
  return (
    /^\s*```/.test(line) ||
    /^#{1,6}\s+/.test(line) ||
    /^\s*[-*+]\s+/.test(line) ||
    /^\s*\d+[.)]\s+/.test(line) ||
    /^\s*>\s?/.test(line) ||
    /^\s*---+$/.test(line)
  );
}

function parseBlocks(source: string): ReactNode[] {
  const lines = source.replace(/\r\n?/g, '\n').split('\n');
  const blocks: ReactNode[] = [];
  let index = 0;
  let blockKey = 0;

  while (index < lines.length) {
    const line = lines[index];
    if (!line.trim()) {
      index += 1;
      continue;
    }

    const fence = line.match(/^\s*```/);
    if (fence) {
      const code: string[] = [];
      index += 1;
      while (index < lines.length && !/^\s*```/.test(lines[index])) {
        code.push(lines[index]);
        index += 1;
      }
      if (index < lines.length) index += 1;
      blocks.push(
        <pre key={`code-${blockKey++}`} className="overflow-x-auto rounded-xl border border-slate-200 bg-slate-950 p-4 text-sm leading-relaxed text-slate-100">
          <code>{code.join('\n')}</code>
        </pre>,
      );
      continue;
    }

    const heading = line.match(/^(#{1,6})\s+(.*)$/);
    if (heading) {
      const depth = Math.min(heading[1].length + 1, 6);
      const children = renderInline(heading[2], `h-${blockKey}`);
      const className =
        depth === 2
          ? 'mt-10 text-2xl font-bold tracking-tight text-slate-950 first:mt-0 sm:text-3xl'
          : depth === 3
            ? 'mt-8 text-xl font-semibold tracking-tight text-slate-950 first:mt-0'
            : 'mt-6 text-lg font-semibold text-slate-900 first:mt-0';
      if (depth === 2) blocks.push(<h2 key={`h-${blockKey++}`} className={className}>{children}</h2>);
      else if (depth === 3) blocks.push(<h3 key={`h-${blockKey++}`} className={className}>{children}</h3>);
      else if (depth === 4) blocks.push(<h4 key={`h-${blockKey++}`} className={className}>{children}</h4>);
      else if (depth === 5) blocks.push(<h5 key={`h-${blockKey++}`} className={className}>{children}</h5>);
      else blocks.push(<h6 key={`h-${blockKey++}`} className={className}>{children}</h6>);
      index += 1;
      continue;
    }

    if (/^\s*---+$/.test(line)) {
      blocks.push(<hr key={`hr-${blockKey++}`} className="my-8 border-slate-200" />);
      index += 1;
      continue;
    }

    if (/^\s*[-*+]\s+/.test(line)) {
      const items: string[] = [];
      while (index < lines.length && /^\s*[-*+]\s+/.test(lines[index])) {
        items.push(lines[index].replace(/^\s*[-*+]\s+/, ''));
        index += 1;
      }
      blocks.push(
        <ul key={`ul-${blockKey++}`} className="my-5 list-disc space-y-2 pl-6 marker:text-teal-700">
          {items.map((item, itemIndex) => (
            <li key={`ul-${blockKey}-${itemIndex}`}>{renderInline(item, `ul-${blockKey}-${itemIndex}`)}</li>
          ))}
        </ul>,
      );
      continue;
    }

    const ordered = line.match(/^\s*(\d+)[.)]\s+/);
    if (ordered) {
      const start = Number(ordered[1]) || 1;
      const items: string[] = [];
      while (index < lines.length) {
        const item = lines[index].match(/^\s*\d+[.)]\s+(.*)$/);
        if (!item) break;
        items.push(item[1]);
        index += 1;
      }
      blocks.push(
        <ol key={`ol-${blockKey++}`} start={start} className="my-5 list-decimal space-y-2 pl-6 marker:font-semibold marker:text-teal-800">
          {items.map((item, itemIndex) => (
            <li key={`ol-${blockKey}-${itemIndex}`}>{renderInline(item, `ol-${blockKey}-${itemIndex}`)}</li>
          ))}
        </ol>,
      );
      continue;
    }

    if (/^\s*>\s?/.test(line)) {
      const quote: string[] = [];
      while (index < lines.length && /^\s*>\s?/.test(lines[index])) {
        quote.push(lines[index].replace(/^\s*>\s?/, ''));
        index += 1;
      }
      blocks.push(
        <blockquote key={`quote-${blockKey++}`} className="my-6 border-l-4 border-teal-600 bg-teal-50/60 px-5 py-4 text-slate-700">
          {renderInline(quote.join(' '), `quote-${blockKey}`)}
        </blockquote>,
      );
      continue;
    }

    const paragraph: string[] = [line.trim()];
    index += 1;
    while (index < lines.length && lines[index].trim() && !startsBlock(lines[index])) {
      paragraph.push(lines[index].trim());
      index += 1;
    }
    blocks.push(
      <p key={`p-${blockKey++}`} className="my-4 leading-7 text-slate-700">
        {renderInline(paragraph.join(' '), `p-${blockKey}`)}
      </p>,
    );
  }

  return blocks;
}

interface MarkdownProps {
  source: string;
  className?: string;
  emptyMessage?: string;
}

export const Markdown: React.FC<MarkdownProps> = ({
  source,
  className = '',
  emptyMessage = 'No lesson content has been added yet.',
}) => {
  const content = useMemo(() => parseBlocks(source || ''), [source]);
  if (!source.trim()) {
    return <p className="text-sm text-slate-500">{emptyMessage}</p>;
  }
  return <div className={`learning-prose ${className}`.trim()}>{content}</div>;
};
