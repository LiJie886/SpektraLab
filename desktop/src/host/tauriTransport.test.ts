import { describe, expect, it, vi } from 'vitest';

const mock = vi.hoisted(() => ({ listen: vi.fn(), invoke: vi.fn() }));
vi.mock('@tauri-apps/api/core', () => ({ invoke: mock.invoke }));
vi.mock('@tauri-apps/api/event', () => ({ listen: mock.listen }));

import { tauriTransport } from './tauriTransport';

describe('Tauri host startup', () => {
  it('subscribes before reading the initial host state', async () => {
    let registered!: (unlisten: () => void) => void;
    mock.listen.mockReturnValueOnce(new Promise((resolve) => { registered = resolve; }));
    mock.invoke.mockResolvedValueOnce({ phase: 'ready' });
    const transport = tauriTransport();
    transport.onState(() => {});
    const state = transport.state();
    await Promise.resolve();
    expect(mock.invoke).not.toHaveBeenCalled();
    registered(() => {});
    await expect(state).resolves.toEqual({ phase: 'ready' });
    expect(mock.invoke).toHaveBeenCalledWith('host_state');
  });
});
