#pragma once
#include <GfxRenderer.h>

namespace UIRenderUtils {

// Grayscale anti-aliasing pass for UI screens. Precondition: the BW frame has
// already been rendered AND displayed (displayBuffer). displayGrayBuffer only
// drives the gray AA-edge pixels — every other pixel keeps its current state
// on the panel — so this can only enhance a frame that is already on screen.
// renderFn must draw the complete screen content; it is re-rendered after the
// grayscale passes to restore the framebuffer as the differential baseline.
template <typename RenderFn>
void renderUIAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.clearScreen();
  renderFn();
  renderer.cleanupGrayscaleWithFrameBuffer();
}

} // namespace UIRenderUtils
