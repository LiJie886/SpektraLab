import { useState } from 'react';
import { CONTRAST_MASK_DEFAULT, CONTRAST_MASK_SCALE, SCENE_LATITUDE_DEFAULT } from '@shared/params';
import { ScrubSlider, Section, ToggleRow } from '../controls/controls';
import { t, tz } from '../i18n';
import { fitScenePlacement, setParams, useSession } from '../state/session';
import { unsupportedReason } from './gate';

export function ScenePlacementSection() {
  const sid = useSession((s) => s.engineSession);
  const p = useSession((s) => s.sidecar.params);
  const key = p.placementIsRight ? 'sceneLatitudeOther' : 'sceneLatitude';
  return <PlacementControls key={`${sid}:${JSON.stringify(p[key])}`} />;
}

function PlacementControls() {
  const p = useSession((s) => s.sidecar.params);
  const gate = useSession((s) => s.gate);
  const sid = useSession((s) => s.engineSession);
  const busy = useSession((s) => s.developing || s.redecoding || s.batchExporting || s.exportOpen);
  const key = p.placementIsRight ? 'sceneLatitudeOther' : 'sceneLatitude';
  const sl = p[key];
  const [highlights, setHighlights] = useState(sl.highlightPullBack);
  const [shadows, setShadows] = useState(sl.shadowPullBack);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const reason = unsupportedReason('scene_latitude_mapping', gate.sceneLatitude) ??
    (p.digitalIntermediate && !p.scanFilm ? tz('Unavailable in Digital Intermediate mode.', '数字中间片模式下不可用。') : null);
  const disabled = !!reason || !sid || busy || pending;
  const fit = async (automatic: boolean) => {
    if (disabled) return;
    setPending(true);
    setError(null);
    try {
      await fitScenePlacement(automatic ? {} : { highlight_pull_back: highlights, shadow_pull_back: shadows });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setPending(false);
    }
  };
  return (
    <Section id="scenePlacement" title={t('sectionScenePlacement')} unsupported={reason}
      onReset={() => setParams((q) => ({ ...q, [key]: { ...SCENE_LATITUDE_DEFAULT } }))}
      resetHelp={t('helpResetPlacement')} resetEnabled={sl.active && !pending && !busy}>
      <ToggleRow label={tz('Enable', '启用')} on={sl.active} disabled={disabled || (!sl.highlightRoom && !sl.shadowRoom)}
        onChange={(active) => setParams((q) => ({ ...q, [key]: { ...q[key], active } }))} />
      <ScrubSlider label={t('placementHighlight')} value={highlights} range={[0, 8]} snap={0.25} onChange={setHighlights} disabled={disabled} reason={reason ?? undefined} />
      <ScrubSlider label={t('placementShadow')} value={shadows} range={[0, Math.min(8, sl.maxLift - 0.01)]} snap={0.25} onChange={setShadows} disabled={disabled} reason={reason ?? undefined} />
      <div className="row" style={{ gap: 8 }}>
        <button className="btn" disabled={disabled} onClick={() => void fit(false)} data-testid="placement-apply">{pending ? tz('Fitting…', '正在拟合…') : tz('Apply', '应用')}</button>
        <button className="btn" disabled={disabled} onClick={() => void fit(true)} data-testid="placement-auto">{tz('Auto fit', '自动拟合')}</button>
      </div>
      <div className="caption">{tz('Fit highlights and shadows to this film and paper. Zero leaves that side unchanged.', '按当前胶片和相纸拟合高光与阴影；设为 0 时不调整该侧。')}</div>
      {error && <div className="caption" role="alert">{error}</div>}
    </Section>
  );
}

export function ContrastMaskSection() {
  const p = useSession((s) => s.sidecar.params);
  const gate = useSession((s) => s.gate);
  const positivePaper = useSession((s) => s.catalog.isPositive(s.sidecar.params.printStock));
  const m = p.contrastMask;
  const reason = unsupportedReason('contrast_mask', gate.contrastMask) ??
    (!p.printEffects ? t('reasonPrintEffectsOff') :
      p.scanFilm || p.digitalIntermediate || positivePaper ? tz('Available for negative-to-paper printing only.', '仅在负片到相纸的印相流程中可用。') : null);
  const disabled = !!reason;
  const change = (patch: Partial<typeof m>) => setParams((q) => ({ ...q, contrastMask: { ...q.contrastMask, ...patch } }));
  return (
    <Section id="contrastMask" title={tz('Contrast Mask', '对比度蒙版')} unsupported={reason}
      onReset={() => setParams((q) => ({ ...q, contrastMask: { ...CONTRAST_MASK_DEFAULT } }))} resetHelp={t('helpReset')}>
      <ToggleRow label={tz('Enable', '启用')} on={m.active} onChange={(active) => change({ active })} disabled={disabled} reason={reason ?? undefined} />
      <ScrubSlider label={tz('Highlights', '高光')} value={m.highlights} range={[0, 3]} snap={0.25} onChange={(highlights) => change({ highlights })} disabled={disabled || !m.active} />
      <ScrubSlider label={tz('Shadows', '阴影')} value={m.shadows} range={[0, 3]} snap={0.25} onChange={(shadows) => change({ shadows })} disabled={disabled || !m.active} />
      <ScrubSlider label={tz('Midtone core', '中间调保护范围')} value={m.core} range={[0, 3]} zero={1} snap={0.25} onChange={(core) => change({ core })} disabled={disabled || !m.active} />
      <ScrubSlider label={tz('Radius (%)', '半径（%）')} value={m.scale * 100} range={[CONTRAST_MASK_SCALE[0] * 100, CONTRAST_MASK_SCALE[1] * 100]} zero={3} snap={0.5} onChange={(v) => change({ scale: v / 100 })} disabled={disabled || !m.active} />
      <div className="caption">{tz('Controls print contrast while retaining the developed negative.', '调整印相反差，复用已显影的底片。')}</div>
    </Section>
  );
}
