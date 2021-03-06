/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayersChild.h"
#include "BasicTiledThebesLayer.h"
#include "gfxImageSurface.h"
#include "sampler.h"

#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
#include "cairo.h"
#include <sstream>
using mozilla::layers::Layer;
static void DrawDebugOverlay(gfxImageSurface* imgSurf, int x, int y)
{
  gfxContext c(imgSurf);

  // Draw border
  c.NewPath();
  c.SetDeviceColor(gfxRGBA(0.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(0,0),imgSurf->GetSize()));
  c.Stroke();

  // Build tile description
  std::stringstream ss;
  ss << x << ", " << y;

  // Draw text using cairo toy text API
  cairo_t* cr = c.GetCairo();
  cairo_set_font_size(cr, 10);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, ss.str().c_str(), &extents);

  int textWidth = extents.width + 6;

  c.NewPath();
  c.SetDeviceColor(gfxRGBA(0.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(2,2),gfxSize(textWidth, 15)));
  c.Fill();

  c.NewPath();
  c.SetDeviceColor(gfxRGBA(1.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(2,2),gfxSize(textWidth, 15)));
  c.Stroke();

  c.NewPath();
  cairo_move_to(cr, 4, 13);
  cairo_show_text(cr, ss.str().c_str());

}

#endif

namespace mozilla {
namespace layers {

gfxASurface::gfxImageFormat
BasicTiledLayerBuffer::GetFormat() const
{
  if (mThebesLayer->CanUseOpaqueSurface()) {
    return gfxASurface::ImageFormatRGB16_565;
  } else {
    return gfxASurface::ImageFormatARGB32;
  }
}

void
BasicTiledLayerBuffer::PaintThebes(BasicTiledThebesLayer* aLayer,
                                   const nsIntRegion& aNewValidRegion,
                                   const nsIntRegion& aPaintRegion,
                                   LayerManager::DrawThebesLayerCallback aCallback,
                                   void* aCallbackData)
{
  mThebesLayer = aLayer;
  mCallback = aCallback;
  mCallbackData = aCallbackData;

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  long start = PR_IntervalNow();
#endif
  if (UseSinglePaintBuffer()) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    {
      SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesSingleBufferAlloc");
      mSinglePaintBuffer = new gfxImageSurface(gfxIntSize(bounds.width, bounds.height), GetFormat(), !aLayer->CanUseOpaqueSurface());
      mSinglePaintBufferOffset = nsIntPoint(bounds.x, bounds.y);
    }
    nsRefPtr<gfxContext> ctxt = new gfxContext(mSinglePaintBuffer);
    ctxt->NewPath();
    ctxt->Translate(gfxPoint(-bounds.x, -bounds.y));
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
    if (PR_IntervalNow() - start > 3) {
      printf_stderr("Slow alloc %i\n", PR_IntervalNow() - start);
    }
    start = PR_IntervalNow();
#endif
    SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesSingleBufferDraw");
    mCallback(mThebesLayer, ctxt, aPaintRegion, aPaintRegion, mCallbackData);
  }

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 30) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    printf_stderr("Time to draw %i: %i, %i, %i, %i\n", PR_IntervalNow() - start, bounds.x, bounds.y, bounds.width, bounds.height);
    if (aPaintRegion.IsComplex()) {
      printf_stderr("Complex region\n");
      nsIntRegionRectIterator it(aPaintRegion);
      for (const nsIntRect* rect = it.Next(); rect != nsnull; rect = it.Next()) {
        printf_stderr(" rect %i, %i, %i, %i\n", rect->x, rect->y, rect->width, rect->height);
      }
    }
  }
  start = PR_IntervalNow();
#endif

  SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesUpdate");
  Update(aNewValidRegion, aPaintRegion);

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 10) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    printf_stderr("Time to tile %i: %i, %i, %i, %i\n", PR_IntervalNow() - start, bounds.x, bounds.y, bounds.width, bounds.height);
  }
#endif

  mThebesLayer = nsnull;
  mCallback = nsnull;
  mCallbackData = nsnull;
  mSinglePaintBuffer = nsnull;
}

BasicTiledLayerTile
BasicTiledLayerBuffer::ValidateTileInternal(BasicTiledLayerTile aTile,
                                            const nsIntPoint& aTileOrigin,
                                            const nsIntRect& aDirtyRect)
{
  if (aTile == GetPlaceholderTile()) {
    gfxImageSurface* tmpTile = new gfxImageSurface(gfxIntSize(GetTileLength(), GetTileLength()),
                                                   GetFormat(), !mThebesLayer->CanUseOpaqueSurface());
    aTile = BasicTiledLayerTile(tmpTile);
  }

  gfxRect drawRect(aDirtyRect.x - aTileOrigin.x, aDirtyRect.y - aTileOrigin.y,
                   aDirtyRect.width, aDirtyRect.height);

  // Use the gfxReusableSurfaceWrapper, which will reuse the surface
  // if the compositor no longer has a read lock, otherwise the surface
  // will be copied into a new writable surface.
  gfxImageSurface* writableSurface;
  aTile.mSurface = aTile.mSurface->GetWritable(&writableSurface);

  // Bug 742100, this gfxContext really should live on the stack.
  nsRefPtr<gfxContext> ctxt = new gfxContext(writableSurface);
 if (!mThebesLayer->CanUseOpaqueSurface()) {
    ctxt->NewPath();
    ctxt->SetOperator(gfxContext::OPERATOR_CLEAR);
    ctxt->Rectangle(drawRect, true);
    ctxt->Fill();
    ctxt->SetOperator(gfxContext::OPERATOR_OVER);
  }
  if (mSinglePaintBuffer) {
    ctxt->NewPath();
    ctxt->SetSource(mSinglePaintBuffer.get(),
                    gfxPoint(mSinglePaintBufferOffset.x - aDirtyRect.x + drawRect.x,
                             mSinglePaintBufferOffset.y - aDirtyRect.y + drawRect.y));
    ctxt->Rectangle(drawRect, true);
    ctxt->Fill();
  } else {
    ctxt->NewPath();
    ctxt->Translate(gfxPoint(-aTileOrigin.x, -aTileOrigin.y));
    nsIntPoint a = aTileOrigin;
    mCallback(mThebesLayer, ctxt, nsIntRegion(nsIntRect(a, nsIntSize(GetTileLength(), GetTileLength()))), aDirtyRect, mCallbackData);
  }

#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
  DrawDebugOverlay(writableSurface, aTileOrigin.x, aTileOrigin.y);
  //aTile->DumpAsDataURL();
#endif

  return aTile;
}

BasicTiledLayerTile
BasicTiledLayerBuffer::ValidateTile(BasicTiledLayerTile aTile,
                                    const nsIntPoint& aTileOrigin,
                                    const nsIntRegion& aDirtyRegion)
{

  SAMPLE_LABEL("BasicTiledLayerBuffer", "ValidateTile");

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (aDirtyRegion.IsComplex()) {
    printf_stderr("Complex region\n");
  }
#endif

  nsIntRegionRectIterator it(aDirtyRegion);
  for (const nsIntRect* rect = it.Next(); rect != nsnull; rect = it.Next()) {
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
    printf_stderr(" break into subrect %i, %i, %i, %i\n", rect->x, rect->y, rect->width, rect->height);
#endif
    aTile = ValidateTileInternal(aTile, aTileOrigin, *rect);
  }

  return aTile;
}

void
BasicTiledThebesLayer::FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
{
  aAttrs = ThebesLayerAttributes(GetValidRegion());
}

void
BasicTiledThebesLayer::PaintThebes(gfxContext* aContext,
                                   LayerManager::DrawThebesLayerCallback aCallback,
                                   void* aCallbackData,
                                   ReadbackProcessor* aReadback)
{
  if (!aCallback) {
    BasicManager()->SetTransactionIncomplete();
    return;
  }

  if (!HasShadow()) {
    NS_ASSERTION(false, "Shadow requested for painting\n");
    return;
  }

  nsIntRegion regionToPaint = mVisibleRegion;
  regionToPaint.Sub(regionToPaint, mValidRegion);
  if (regionToPaint.IsEmpty())
    return;

  mTiledBuffer.PaintThebes(this, mVisibleRegion, regionToPaint, aCallback, aCallbackData);
  mTiledBuffer.ReadLock();
  mValidRegion = mVisibleRegion;

  BasicManager()->PaintedTiledLayerBuffer(BasicManager()->Hold(this), &mTiledBuffer);
}

} // mozilla
} // layers
