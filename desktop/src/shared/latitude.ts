import type { SceneLatitudeSettings } from './params';

/** Accept only the engine's solved curve, never send UI pull-backs as knees. */
export function resolvedPlacement(current: SceneLatitudeSettings, reply: Record<string, unknown>): SceneLatitudeSettings {
  const fit = reply.fit as { valid?: boolean; issues?: { message?: string }[]; params_delta?: Record<string, unknown>; highlight?: { pull_back?: number }; shadow?: { pull_back?: number } } | undefined;
  if (fit?.valid !== true || !fit.params_delta) throw new Error(fit?.issues?.map((i) => i.message).filter(Boolean).join('; ') || 'No valid scene placement was found.');
  const d = fit.params_delta;
  const number = (name: string) => {
    const v = d[`scene_latitude_${name}`];
    if (typeof v !== 'number' || !Number.isFinite(v)) throw new Error(`Invalid scene placement: ${name}`);
    return v;
  };
  const next: SceneLatitudeSettings = {
    ...current,
    active: d.scene_latitude_active === true,
    norm: String(d.scene_latitude_norm),
    highlightKnee: number('highlight_knee'), highlightRoom: number('highlight_room'),
    shadowKnee: number('shadow_knee'), shadowRoom: number('shadow_room'),
    rolloff: number('rolloff'), maxLift: number('max_lift'),
    highlightPullBack: fit.highlight?.pull_back ?? 0,
    shadowPullBack: fit.shadow?.pull_back ?? 0,
  };
  if (!['power', 'y', 'max'].includes(next.norm) ||
      next.highlightRoom < 0 || next.highlightRoom > 24 || next.shadowRoom < 0 || next.shadowRoom > 24 ||
      Math.abs(next.highlightKnee) > 24 || Math.abs(next.shadowKnee) > 24 ||
      !Number.isFinite(next.highlightPullBack) || !Number.isFinite(next.shadowPullBack) ||
      next.highlightPullBack < 0 || next.shadowPullBack < 0 ||
      next.rolloff < 1 || next.rolloff > 4 || next.maxLift < 0.25 || next.maxLift > 12 ||
      (next.highlightRoom > 0 && next.shadowRoom > 0 && next.shadowKnee >= next.highlightKnee)) throw new Error('Invalid scene placement curve.');
  return next;
}
