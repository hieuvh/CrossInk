#pragma once
#include <GfxRenderer.h>

namespace UIRenderUtils {

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
