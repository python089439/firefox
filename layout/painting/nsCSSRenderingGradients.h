/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCSSRenderingGradients_h__
#define nsCSSRenderingGradients_h__

#include "Units.h"
#include "gfxRect.h"
#include "gfxUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "nsStyleStruct.h"

class gfxPattern;

namespace mozilla {

namespace layers {
class StackingContextHelper;
}  // namespace layers

namespace wr {
class DisplayListBuilder;
}  // namespace wr

// A resolved color stop, with a specific position along the gradient line and
// a color.
struct ColorStop {
  ColorStop() : mPosition(0), mIsMidpoint(false) {}
  ColorStop(double aPosition, bool aIsMidPoint,
            const StyleAbsoluteColor& aColor)
      : mPosition(aPosition), mIsMidpoint(aIsMidPoint), mColor(aColor) {}
  double mPosition;  // along the gradient line; 0=start, 1=end
  bool mIsMidpoint;
  StyleAbsoluteColor mColor;
};

template <class T>
class MOZ_STACK_CLASS ColorStopInterpolator {
 public:
  ColorStopInterpolator(
      const nsTArray<ColorStop>& aStops,
      const StyleColorInterpolationMethod& aStyleColorInterpolationMethod,
      bool aExtend)
      : mStyleColorInterpolationMethod(aStyleColorInterpolationMethod),
        mStops(aStops),
        mExtend(aExtend) {}

  void CreateStops() {
    // This loop intentionally iterates extra stops at the beginning and end
    // if extending was requested, or in the degenerate case where only one
    // color stop was specified.
    const bool extend = mExtend || mStops.Length() == 1;
    const uint32_t iterStops = mStops.Length() - 1 + (extend ? 2 : 0);
    for (uint32_t i = 0; i < iterStops; i++) {
      auto thisindex = extend ? (i == 0 ? 0 : i - 1) : i;
      auto nextindex =
          extend && (i == iterStops - 1 || i == 0) ? thisindex : thisindex + 1;
      const auto& start = mStops[thisindex];
      const auto& end = mStops[nextindex];
      float startPosition = start.mPosition;
      float endPosition = end.mPosition;
      // For CSS non-repeating gradients with longer hue specified, we have to
      // pretend there is a stop beyond the last stop, and one before the first.
      // This is never the case on SVG gradients as they only use shorter hue.
      //
      // See https://bugzilla.mozilla.org/show_bug.cgi?id=1885716 for more info.
      uint32_t extraStops = 0;
      if (extend) {
        // If we're extending, we just need a single new stop, which will
        // duplicate the end being extended; do not create interpolated stops
        // within in the extension area!
        if (i == 0) {
          startPosition = std::min(startPosition, 0.0f);
          extraStops = 1;
        }
        if (i == iterStops - 1) {
          endPosition = std::max(endPosition, 1.0f);
          extraStops = 1;
        }
      }
      if (!extraStops) {
        // Within the actual gradient range, figure out how many extra stops
        // to use for this section of the gradient.
        extraStops = (uint32_t)(floor(endPosition * kFullRangeExtraStops) -
                                floor(startPosition * kFullRangeExtraStops));
        extraStops = std::clamp(extraStops, 1U, kFullRangeExtraStops);
      }
      float step = 1.0f / (float)extraStops;
      for (uint32_t extraStop = 0; extraStop <= extraStops; extraStop++) {
        auto progress = (float)extraStop * step;
        auto position =
            startPosition + progress * (endPosition - startPosition);
        StyleAbsoluteColor color =
            Servo_InterpolateColor(mStyleColorInterpolationMethod,
                                   &start.mColor, &end.mColor, progress);
        static_cast<T*>(this)->CreateStop(float(position),
                                          gfx::ToDeviceColor(color));
      }
    }
  }

 protected:
  StyleColorInterpolationMethod mStyleColorInterpolationMethod;
  const nsTArray<ColorStop>& mStops;
  // This indicates that we want to extend the endPosition on the last stop,
  // which only matters if this is a CSS non-repeating gradient with
  // StyleHueInterpolationMethod::Longer (only valid for hsl/hwb/lch/oklch).
  bool mExtend;

  // This could be made tunable, but at 1.0/128 the error is largely
  // irrelevant, as WebRender re-encodes it to 128 pairs of stops.
  //
  // Note that we don't attempt to place the positions of these stops
  // precisely at intervals, we just add this many extra stops across the
  // range where it is convenient.
  inline static const uint32_t kFullRangeExtraStops = 128;
};

class nsCSSGradientRenderer final {
 public:
  /**
   * Prepare a nsCSSGradientRenderer for a gradient for an element.
   * aIntrinsicSize - the size of the source gradient.
   */
  static nsCSSGradientRenderer Create(nsPresContext* aPresContext,
                                      ComputedStyle* aComputedStyle,
                                      const StyleGradient& aGradient,
                                      const nsSize& aIntrinsiceSize);

  /**
   * Draw the gradient to aContext
   * aDest - where the first tile of gradient is
   * aFill - the area to be filled with tiles of aDest
   * aSrc - the area of the gradient that will fill aDest
   * aRepeatSize - the distance from the origin of a tile
   *               to the next origin of a tile
   * aDirtyRect - pixels outside of this area may be skipped
   */
  void Paint(gfxContext& aContext, const nsRect& aDest, const nsRect& aFill,
             const nsSize& aRepeatSize, const mozilla::CSSIntRect& aSrc,
             const nsRect& aDirtyRect, float aOpacity = 1.0);

  /**
   * Collect the gradient parameters
   */
  void BuildWebRenderParameters(float aOpacity, wr::ExtendMode& aMode,
                                nsTArray<wr::GradientStop>& aStops,
                                LayoutDevicePoint& aLineStart,
                                LayoutDevicePoint& aLineEnd,
                                LayoutDeviceSize& aGradientRadius,
                                LayoutDevicePoint& aGradientCenter,
                                float& aGradientAngle);

  /**
   * Build display items for the gradient
   * aLayer - the layer to make this display item relative to
   * aDest - where the first tile of gradient is
   * aFill - the area to be filled with tiles of aDest
   * aRepeatSize - the distance from the origin of a tile
   *               to the next origin of a tile
   * aSrc - the area of the gradient that will fill aDest
   */
  void BuildWebRenderDisplayItems(wr::DisplayListBuilder& aBuilder,
                                  const layers::StackingContextHelper& aSc,
                                  const nsRect& aDest, const nsRect& aFill,
                                  const nsSize& aRepeatSize,
                                  const mozilla::CSSIntRect& aSrc,
                                  bool aIsBackfaceVisible,
                                  float aOpacity = 1.0);

 private:
  nsCSSGradientRenderer()
      : mPresContext(nullptr),
        mGradient(nullptr),
        mRadiusX(0.0),
        mRadiusY(0.0),
        mAngle(0.0) {}

  /**
   * Attempts to paint the tiles for a gradient by painting it once to an
   * offscreen surface and then painting that offscreen surface with
   * ExtendMode::Repeat to cover all tiles.
   *
   * Returns false if the optimization wasn't able to be used, in which case
   * a fallback should be used.
   */
  bool TryPaintTilesWithExtendMode(
      gfxContext& aContext, gfxPattern* aGradientPattern, nscoord aXStart,
      nscoord aYStart, const gfxRect& aDirtyAreaToFill, const nsRect& aDest,
      const nsSize& aRepeatSize, bool aForceRepeatToCoverTiles);

  nsPresContext* mPresContext;
  const StyleGradient* mGradient;
  nsTArray<ColorStop> mStops;
  gfxPoint mLineStart, mLineEnd;  // only for linear/radial gradients
  double mRadiusX, mRadiusY;      // only for radial gradients
  gfxPoint mCenter;               // only for conic gradients
  float mAngle;                   // only for conic gradients
};

}  // namespace mozilla

#endif /* nsCSSRenderingGradients_h__ */
