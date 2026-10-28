import { describe, expect, it } from 'vitest';
import { applyFrameOrder, frameOrderStoreName, moveFrame } from './frameOrder';

const files = [
  { path: 'C:/photos/a.raw', name: 'a.raw', size: 1, mtime_ms: 1 },
  { path: 'C:/photos/b.raw', name: 'b.raw', size: 1, mtime_ms: 1 },
  { path: 'C:/photos/c.raw', name: 'c.raw', size: 1, mtime_ms: 1 },
];

describe('folder frame order', () => {
  it('applies a stored order while dropping removed and duplicate paths and appending new files', () => {
    expect(applyFrameOrder(files, ['C:/photos/c.raw', 'C:/photos/removed.raw', 'C:/photos/c.raw']))
      .toEqual([files[2], files[0], files[1]]);
  });

  it('moves only the requested library entry', () => {
    expect(moveFrame(files, files[0]!.path, files[2]!.path)).toEqual([files[1], files[2], files[0]]);
    expect(moveFrame(files, 'outside.raw', files[2]!.path)).toBe(files);
  });

  it('creates a deterministic safe storage name per folder', () => {
    expect(frameOrderStoreName('C:\\Photos')).toBe(frameOrderStoreName('C:\\Photos'));
    expect(frameOrderStoreName('C:\\Photos')).not.toBe(frameOrderStoreName('D:\\Photos'));
    expect(frameOrderStoreName('C:\\Photos')).toMatch(/^folder-[a-f0-9]{16}\.json$/);
  });
});
