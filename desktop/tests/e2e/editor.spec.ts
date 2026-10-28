// Layout and behaviour of the editor against the in-page mock host.

import { expect, test, type Page } from '@playwright/test';
import { outputSize, sourcePoint } from '../../src/shared/geometry';

async function openRoll(page: Page) {
  await page.goto('/?open=');
  await expect(page.getByTestId('tier-badge')).toHaveText('full', { timeout: 20_000 });
}

const state = (page: Page, expr: string) =>
  page.evaluate((e) => {
    const s = (window as unknown as { __spk: { sessionStore: { getState(): unknown } } }).__spk.sessionStore.getState();
    return new Function('s', `return ${e}`)(s);
  }, expr);

test('the window: two rails, the bar, the canvas, the filmstrip', async ({ page }) => {
  await openRoll(page);
  await expect(page.getByTestId('left-rail')).toContainText('Film and Print');
  await expect(page.getByTestId('right-rail')).toContainText('Parameters');
  await expect(page.locator('.thumb')).toHaveCount(8);
  await expect(page.getByTestId('histogram')).toBeVisible();
  await expect(page.getByTestId('navigator').locator('canvas')).toBeVisible();
});

test('a film edit reaches the sidecar and the engine', async ({ page }) => {
  await openRoll(page);
  await page.locator('[data-stock="kodak_gold_200"]').click();
  expect(await state(page, 's.sidecar.params.filmStock')).toBe('kodak_gold_200');
  await expect(page.getByTestId('tier-badge')).toHaveText('full', { timeout: 20_000 });
  await page.keyboard.press('Control+z');
  expect(await state(page, 's.sidecar.params.filmStock')).toBe('kodak_portra_400');
});

test('trap 32: an arrow typed in a field does not change the frame', async ({ page }) => {
  await openRoll(page);
  const first = await state(page, 's.selection');
  const field = page.getByRole('textbox', { name: 'Film Exposure value' });
  await field.click();
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowRight');
  expect(await state(page, 's.selection')).toBe(first);
  await page.keyboard.press('Escape');
  await page.getByTestId('canvas').click();
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => state(page, 's.selection')).not.toBe(first);
});

test('a quarter turn swaps the output size readout', async ({ page }) => {
  await openRoll(page);
  const before = await page.getByTestId('crop-size').textContent();
  await page.getByTestId('rotate-right').click();
  const after = await page.getByTestId('crop-size').textContent();
  const [w, h] = before!.split(' · ')[0]!.split(' × ');
  expect(after!.startsWith(`${h} × ${w}`)).toBe(true);
});

test('filmstrip pointer drag reorders frames without changing selection or picked set', async ({ page }) => {
  await openRoll(page);
  const before = await state(page, '({ frames:s.frames.map(f=>f.path), selection:s.selection, picked:[...s.picked] })') as { frames: string[]; selection: string; picked: string[] };
  const thumbs = page.locator('.thumb');
  const first = await thumbs.first().boundingBox();
  // Drop onto a visible cell; an offscreen cell's bounding box lies behind
  // the rail and is not a pointer drop target.
  const last = await thumbs.nth(3).boundingBox();
  expect(first).not.toBeNull();
  expect(last).not.toBeNull();
  const start = { x: first!.x + first!.width / 2, y: first!.y + first!.height / 2 };
  const end = { x: last!.x + last!.width / 2, y: last!.y + last!.height / 2 };
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 12, start.y, { steps: 2 });
  await page.mouse.move(end.x, end.y, { steps: 8 });
  await page.mouse.up();
  await expect.poll(() => state(page, 's.frames.map(f=>f.path)')).toEqual([
    ...before.frames.slice(1, 4), before.frames[0], ...before.frames.slice(4),
  ]);
  expect(await state(page, '({ selection:s.selection, picked:s.picked })')).toEqual({ selection: before.selection, picked: before.picked });
  const ordered = await state(page, 's.frames.map(f=>f.path)');
  await page.reload();
  await expect.poll(() => state(page, 's.frames.map(f=>f.path)')).toEqual(ordered);
});

test('scene placement applies a solved curve and the print mask is undoable', async ({ page }) => {
  await openRoll(page);
  await page.getByTestId('placement-auto').click();
  await expect.poll(() => state(page, 's.sidecar.params.sceneLatitude.active')).toBe(true);
  expect(await state(page, 's.sidecar.params.sceneLatitude.highlightRoom')).toBeGreaterThan(0);
  await expect(page.getByTestId('tier-badge')).toHaveText('full');
  const mask = page.locator('[data-section="contrastMask"]');
  await mask.getByRole('switch').click();
  await expect.poll(() => state(page, 's.sidecar.params.contrastMask.active')).toBe(true);
  await page.keyboard.press('Control+z');
  await expect.poll(() => state(page, 's.sidecar.params.contrastMask.active')).toBe(false);
});

test('white balance pointer sampling maps crop and quarter-turn coordinates to the source image', async ({ page }) => {
  await openRoll(page);
  const geometry = await state(page, 's.sidecar.geometry') as {
    crop: { x: number; y: number; width: number; height: number };
    quarterTurns: number; flipH: boolean; flipV: boolean; angle: number;
    aspect: string; intendedSize: unknown; lockedRatio: unknown;
  };
  const changed = { ...geometry, crop: { x: 0.17, y: 0.11, width: 0.62, height: 0.73 }, quarterTurns: 1, flipH: true, angle: 0 };
  await page.evaluate((g) => {
    const w = window as unknown as { __spk: { sessionStore: { getState(): { sidecar: Record<string, unknown> }; setState(v: unknown): void } } };
    const s = w.__spk.sessionStore.getState();
    w.__spk.sessionStore.setState({ sidecar: { ...s.sidecar, geometry: g } });
  }, changed);
  await page.getByTestId('wb-picker').click();
  await expect(page.getByTestId('wb-picker')).toContainText('Cancel');

  const sample = { x: 0.27, y: 0.69 };
  const native = await state(page, 's.nativeSize') as { width: number; height: number };
  const canvasBox = await page.getByTestId('canvas').boundingBox();
  expect(canvasBox).not.toBeNull();
  const dpr = await page.evaluate(() => devicePixelRatio || 1);
  const viewport = { width: canvasBox!.width * dpr, height: canvasBox!.height * dpr };
  const output = outputSize(changed as never, native);
  const zoom = Math.min(viewport.width / output.width, viewport.height / output.height);
  const outWidth = output.width * zoom;
  const outHeight = output.height * zoom;
  const offsetX = (viewport.width - outWidth) / 2;
  const offsetY = (viewport.height - outHeight) / 2;
  const clickPoint = { clientX: canvasBox!.x + (offsetX + sample.x * outWidth) / dpr, clientY: canvasBox!.y + (offsetY + sample.y * outHeight) / dpr };
  const expectedSource = sourcePoint(changed as never, sample, native);
  await page.mouse.click(clickPoint.clientX, clickPoint.clientY);
  await expect.poll(() => state(page, 's.sidecar.decode.temperature')).toBe(2000 + Math.round(expectedSource.x * 10_000));
  expect(await state(page, 's.sidecar.decode.tint')).toBe(-150 + Math.round(expectedSource.y * 300));
});

test('the language switch renames the rails', async ({ page }) => {
  await openRoll(page);
  await page.evaluate(() => (window as unknown as { __spk: { settingsStore: { getState(): { set(k: string, v: string): void } } } }).__spk.settingsStore.getState().set('language', 'simplifiedChinese'));
  await expect(page.getByTestId('left-rail')).toContainText('胶片与相纸');
  await page.evaluate(() => (window as unknown as { __spk: { settingsStore: { getState(): { set(k: string, v: string): void } } } }).__spk.settingsStore.getState().set('language', 'english'));
  await expect(page.getByTestId('left-rail')).toContainText('Film and Print');
});

test('the export page names the file and holds the frame while it runs', async ({ page }) => {
  await openRoll(page);
  await page.getByTestId('export-button').click();
  await expect(page.getByTestId('export-page')).toBeVisible();
  await expect(page.getByTestId('export-sample')).toContainText('DSC_0001_Portra_400');
  await page.getByTestId('export-run').click();
  await expect.poll(() => state(page, 's.batchExporting')).toBe(false);
  await expect(page.getByTestId('export-page')).toContainText('DSC_0001');
});
