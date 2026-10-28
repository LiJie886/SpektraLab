// The store's edit doors, against a stand-in host and platform: what makes an
// undo step and what does not.

import { beforeEach, describe, expect, it, vi } from 'vitest';

const calls: string[] = [];
const orderWrites: string[][] = [];
let nextOrderWriteGate: Promise<void> | null = null;
let whiteBalanceReply: Promise<{ temperature_k: number; tint: number }> | null = null;
let storeReadImpl: (() => Promise<unknown | null>) | null = null;
let placementReply: Promise<Record<string, unknown>> | null = null;
vi.mock('../host/client', () => {
  class HostCallError extends Error {
    code = 'internal';
  }
  const h = {
    setParams: vi.fn(async () => {
      calls.push('set_params');
      return {};
    }),
    render: vi.fn(async (_sid: string, tier: string) => {
      calls.push(`render:${tier}`);
      return { width: 4, height: 4, data: new Uint8Array(64), colorSpace: 'sRGB', tier, reprinted: false, ms: 1 };
    }),
    redecode: vi.fn(async (_sid: string, decode: object) => {
      calls.push('redecode:' + JSON.stringify(decode));
      return { session: 's1', width: 6000, height: 4000, kind: 'raw', metadata: { orientation: 1 }, params: {} };
    }),
    whiteBalancePick: vi.fn(async (_sid: string, x: number, y: number) => {
      calls.push(`white_balance_pick:${x},${y}`);
      if (whiteBalanceReply) return whiteBalanceReply;
      return { temperature_k: 6100, tint: -3.5 };
    }),
    close: vi.fn(async () => {}),
    sceneLatitude: vi.fn(async () => {
      calls.push('scene_latitude');
      if (placementReply) return placementReply;
      return { fit: { valid: true, highlight: { pull_back: 1 }, shadow: { pull_back: 0 }, params_delta: {
        scene_latitude_active: true, scene_latitude_norm: 'power',
        scene_latitude_highlight_knee: 1, scene_latitude_highlight_room: 2,
        scene_latitude_shadow_knee: -2, scene_latitude_shadow_room: 0,
        scene_latitude_rolloff: 2, scene_latitude_max_lift: 4,
      } } };
    }),
  };
  return { HostCallError, host: () => h };
});
vi.mock('../platform', () => ({
  platform: () => ({
    sidecarLoad: async () => null,
    log: () => {}, sidecarSave: async () => '',
    listImages: async (dir: string) => [{ path: `${dir}/only.raw`, name: 'only.raw', size: 1, mtime_ms: 1 }],
    storeRead: async () => storeReadImpl ? storeReadImpl() : null,
    setTitle: async () => {},
    storeWrite: async (_area: string, _name: string, value: string[]) => {
      orderWrites.push(value);
      const gate = nextOrderWriteGate;
      nextOrderWriteGate = null;
      if (gate) await gate;
    },
  }),
  baseName: (p: string) => p.split(/[\\/]/).pop() ?? p,
  dirName: (p: string) => p,
}));

import { beginWhiteBalancePick, cancelWhiteBalancePick, fitScenePlacement, openFolder, recomputeFilmFormat, reorderFrames, sampleWhiteBalance, sessionStore, setDecode, setGeometry, setParams, undo } from './session';
import type { HelloResult } from '@shared/protocol';
import { settingsStore } from './settings';
import { newSidecar } from '@shared/sidecar';

// Undo steps coalesce within 500 ms; each case starts well past the last one.
let clock = 0;
beforeEach(() => {
  clock += 10_000;
  vi.spyOn(performance, 'now').mockReturnValue(clock);
  calls.length = 0;
  orderWrites.length = 0;
  nextOrderWriteGate = null;
  whiteBalanceReply = null;
  storeReadImpl = null;
  placementReply = null;
  sessionStore.setState({
    selection: '/x/a.cr2',
    engineSession: 's1',
    nativeSize: { width: 6000, height: 4000 },
    sidecar: newSidecar(),
    undo: [],
    redo: [],
    batchExporting: false,
  });
  settingsStore.getState().set('recalculateEffectsAfterCrop', true);
});

describe('scene placement application', () => {
  it('stores the fitted curve as one undoable film-side edit', async () => {
    await fitScenePlacement({ highlight_pull_back: 1, shadow_pull_back: 0 });
    const s = sessionStore.getState();
    expect(s.sidecar.params.sceneLatitude).toMatchObject({ active: true, highlightKnee: 1, highlightRoom: 2, highlightPullBack: 1 });
    expect(s.undo).toHaveLength(1);
  });

  it('ignores a fit if parameters change while the host is analysing', async () => {
    let resolve!: (value: Record<string, unknown>) => void;
    placementReply = new Promise((r) => { resolve = r; });
    const pending = fitScenePlacement({ highlight_pull_back: 1 });
    await vi.waitFor(() => expect(calls).toContain('scene_latitude'));
    setParams((p) => ({ ...p, exposureCompensationEV: 1 }));
    resolve({ fit: { valid: false, issues: [{ message: 'stale fit' }] } });
    await expect(pending).rejects.toThrow();
    expect(sessionStore.getState().sidecar.params.sceneLatitude.active).toBe(false);
    expect(sessionStore.getState().sidecar.params.exposureCompensationEV).toBe(1);
  });
});

describe('the derived film format', () => {
  it('is written without an undo step', () => {
    const before = sessionStore.getState().sidecar.params.filmFormatMM;
    sessionStore.setState({ sidecar: { ...newSidecar(), params: { ...newSidecar().params, filmFormatMM: before + 7 } } });
    recomputeFilmFormat();
    const s = sessionStore.getState();
    expect(s.sidecar.params.filmFormatMM).toBeCloseTo(before, 3);
    expect(s.undo).toHaveLength(0);
  });

  it('rides along with the crop that moved it: one undo step, and undo restores both', async () => {
    const mm0 = sessionStore.getState().sidecar.params.filmFormatMM;
    setGeometry((g) => ({ ...g, crop: { x: 0.25, y: 0.25, width: 0.5, height: 0.5 } }));
    const s = sessionStore.getState();
    expect(s.undo).toHaveLength(1);
    expect(s.sidecar.params.filmFormatMM).not.toBeCloseTo(mm0, 3);
    undo();
    const u = sessionStore.getState();
    expect(u.sidecar.geometry.crop.width).toBe(1);
    expect(u.sidecar.params.filmFormatMM).toBeCloseTo(mm0, 3);
    await vi.waitFor(() => expect(calls).toContain('render:live'));
  });

  it('a real edit still makes an undo step', () => {
    setParams((p) => ({ ...p, exposureCompensationEV: 1 }));
    expect(sessionStore.getState().undo).toHaveLength(1);
  });
});

describe('white balance at decode (R2)', () => {
  const hello = { methods: ['open', 'redecode', 'render'] } as unknown as HelloResult;

  it('a decode edit is one undo step and re-decodes the session, then renders', async () => {
    sessionStore.setState({ hello, frameKind: 'raw' });
    setDecode((d) => ({ ...d, whiteBalance: 'Custom', temperature: 3200, tint: 0 }));
    expect(sessionStore.getState().undo).toHaveLength(1);
    await vi.waitFor(() => expect(calls).toContain('redecode:{"white_balance":{"mode":"custom","temperature_k":3200,"tint":0}}'));
    await vi.waitFor(() => expect(calls.indexOf('render:live')).toBeGreaterThan(calls.findIndex((c) => c.startsWith('redecode'))));
    calls.length = 0;
    undo();
    expect(sessionStore.getState().sidecar.decode.whiteBalance).toBe('As Shot');
    await vi.waitFor(() => expect(calls).toContain('redecode:{"white_balance":{"mode":"as_shot"}}'));
  });

  it('a raster frame is not re-decoded', async () => {
    sessionStore.setState({ hello, frameKind: 'tiff', selection: '/x/a.tif' });
    setDecode((d) => ({ ...d, whiteBalance: 'Custom', temperature: 3200 }));
    await vi.waitFor(() => expect(calls).toContain('render:live'));
    expect(calls.some((c) => c.startsWith('redecode'))).toBe(false);
  });

  it('samples the host RAW neutral and applies it as one undoable decode edit', async () => {
    sessionStore.setState({ hello: { methods: ['open', 'redecode', 'white_balance_pick'] } as unknown as HelloResult, frameKind: 'raw' });
    beginWhiteBalancePick();
    expect(sessionStore.getState().whiteBalancePicking).toBe(true);
    await sampleWhiteBalance(0.3, 0.7);
    expect(calls).toContain('white_balance_pick:0.3,0.7');
    expect(sessionStore.getState().sidecar.decode).toMatchObject({ whiteBalance: 'Custom', temperature: 6100, tint: -3.5 });
    expect(sessionStore.getState().undo).toHaveLength(1);
  });

  it('cancels an in-flight sample and ignores it if batch export starts before the reply', async () => {
    sessionStore.setState({ hello: { methods: ['white_balance_pick'] } as unknown as HelloResult, frameKind: 'raw' });
    let resolve!: (v: { temperature_k: number; tint: number }) => void;
    whiteBalanceReply = new Promise((r) => { resolve = r; });
    beginWhiteBalancePick();
    const pending = sampleWhiteBalance(0.4, 0.6);
    expect(sessionStore.getState().whiteBalanceSampling).toBe(true);
    cancelWhiteBalancePick();
    resolve({ temperature_k: 7100, tint: 8 });
    await pending;
    expect(sessionStore.getState().sidecar.decode.temperature).toBe(5500);
    expect(sessionStore.getState().undo).toHaveLength(0);

    whiteBalanceReply = new Promise((r) => { resolve = r; });
    beginWhiteBalancePick();
    const blocked = sampleWhiteBalance(0.4, 0.6);
    sessionStore.setState({ batchExporting: true });
    resolve({ temperature_k: 7100, tint: 8 });
    await blocked;
    expect(sessionStore.getState().sidecar.decode.temperature).toBe(5500);
    expect(sessionStore.getState().undo).toHaveLength(0);
  });

  it('serializes folder-order writes, keeps selection and picked paths, and persists the latest order', async () => {
    const a = { path: '/photos/a.raw', name: 'a.raw', size: 1, mtime_ms: 1 };
    const b = { path: '/photos/b.raw', name: 'b.raw', size: 1, mtime_ms: 1 };
    const c = { path: '/photos/c.raw', name: 'c.raw', size: 1, mtime_ms: 1 };
    sessionStore.setState({ folder: '/photos', frames: [a, b, c], selection: b.path, picked: [a.path, b.path] });
    let release!: () => void;
    nextOrderWriteGate = new Promise<void>((r) => { release = r; });
    reorderFrames(a.path, c.path);
    reorderFrames(c.path, b.path);
    await vi.waitFor(() => expect(orderWrites).toHaveLength(1));
    expect(orderWrites).toHaveLength(1);
    release();
    await vi.waitFor(() => expect(orderWrites).toHaveLength(2));
    expect(orderWrites[1]).toEqual([c.path, b.path, a.path]);
    expect(sessionStore.getState().selection).toBe(b.path);
    expect(sessionStore.getState().picked).toEqual([a.path, b.path]);
  });

  it('does not let a slower folder-order read replace a newer library', async () => {
    const pending: Array<(v: unknown | null) => void> = [];
    storeReadImpl = () => new Promise((resolve) => pending.push(resolve));
    const first = openFolder('/first');
    const second = openFolder('/second');
    await vi.waitFor(() => expect(pending).toHaveLength(2));
    pending[1]!(null);
    await second;
    pending[0]!(null);
    await first;
    expect(sessionStore.getState().folder).toBe('/second');
    expect(sessionStore.getState().frames.map((f) => f.path)).toEqual(['/second/only.raw']);
  });
});
