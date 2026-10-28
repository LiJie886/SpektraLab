import type { FileEntry } from '../platform';

/** Stable, filesystem-safe key for one folder's private library order. */
export function frameOrderStoreName(folder: string): string {
  const bytes = new TextEncoder().encode(folder);
  let a = 0x811c9dc5;
  let b = 0x9e3779b9;
  for (const byte of bytes) {
    a = Math.imul(a ^ byte, 0x01000193) >>> 0;
    b = Math.imul(b ^ byte, 0x85ebca6b) >>> 0;
  }
  return `folder-${a.toString(16).padStart(8, '0')}${b.toString(16).padStart(8, '0')}.json`;
}

/** Stored entries are paths; stale paths are dropped and new files append. */
export function applyFrameOrder(files: FileEntry[], stored: unknown): FileEntry[] {
  if (!Array.isArray(stored)) return files;
  const byPath = new Map(files.map((file) => [file.path, file]));
  const ordered: FileEntry[] = [];
  const seen = new Set<string>();
  for (const path of stored) {
    if (typeof path !== 'string' || seen.has(path)) continue;
    const file = byPath.get(path);
    if (file) {
      ordered.push(file);
      seen.add(path);
    }
  }
  for (const file of files) if (!seen.has(file.path)) ordered.push(file);
  return ordered;
}

export function moveFrame<T extends { path: string }>(frames: T[], source: string, target: string): T[] {
  const from = frames.findIndex((frame) => frame.path === source);
  const to = frames.findIndex((frame) => frame.path === target);
  if (from < 0 || to < 0 || from === to) return frames;
  const next = [...frames];
  const [item] = next.splice(from, 1);
  next.splice(to, 0, item!);
  return next;
}
