import { describe, expect, it } from 'vitest';
import { SCENE_LATITUDE_DEFAULT, FILM_PARAMS_DEFAULT, featureGate, delta } from './params';
import { resolvedPlacement } from './latitude';

const reply = () => ({ fit: { valid: true, highlight: { pull_back: 2 }, shadow: { pull_back: 1 }, params_delta: {
  scene_latitude_active: true, scene_latitude_norm: 'power',
  scene_latitude_highlight_knee: 1, scene_latitude_highlight_room: 2,
  scene_latitude_shadow_knee: -1, scene_latitude_shadow_room: 1,
  scene_latitude_rolloff: 2, scene_latitude_max_lift: 4,
} } });

describe('scene placement fit', () => {
  it('keeps the engine-resolved curve distinct from the requested pull-back', () => {
    const fit = resolvedPlacement(SCENE_LATITUDE_DEFAULT, reply());
    expect(fit.highlightPullBack).toBe(2);
    expect(fit.highlightKnee).toBe(1);
    expect(fit.shadowKnee).toBe(-1);
    expect(fit.shadowPercentile).toBe(0.1);
    const d = delta({ ...FILM_PARAMS_DEFAULT, sceneLatitude: fit }, FILM_PARAMS_DEFAULT, featureGate([]));
    expect(d.delta.scene_latitude_highlight_knee).toBe(1);
    expect([...d.layers]).toEqual(['shoot']);
  });
  it('refuses failed, incomplete or crossed fits without altering the saved settings', () => {
    expect(() => resolvedPlacement(SCENE_LATITUDE_DEFAULT, { fit: { valid: false, issues: [{ message: 'knees cross' }] } })).toThrow('knees cross');
    expect(() => resolvedPlacement(SCENE_LATITUDE_DEFAULT, { fit: { valid: true, params_delta: {} } })).toThrow();
    const crossed = reply();
    crossed.fit.params_delta.scene_latitude_shadow_knee = 2;
    expect(() => resolvedPlacement(SCENE_LATITUDE_DEFAULT, crossed)).toThrow();
    expect(SCENE_LATITUDE_DEFAULT.active).toBe(false);
  });
  it('enables the mask only when supported and uses print-side invalidation', () => {
    const p = { ...FILM_PARAMS_DEFAULT, contrastMask: { ...FILM_PARAMS_DEFAULT.contrastMask, active: true, highlights: 1 } };
    const enabled = delta(p, FILM_PARAMS_DEFAULT, featureGate([]));
    expect(enabled.delta.contrast_mask_active).toBe(true);
    expect([...enabled.layers]).toEqual(['print']);
    expect(delta(p, FILM_PARAMS_DEFAULT, featureGate(['contrast_mask'])).delta.contrast_mask_active).toBeUndefined();
  });
});
