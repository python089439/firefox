/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DisplayPortUtils.h"

#include <ostream>

#include "FrameMetrics.h"
#include "RetainedDisplayListBuilder.h"
#include "WindowRenderer.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/APZPublicUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/LayersMessageUtils.h"
#include "mozilla/layers/PAPZ.h"
#include "nsLayoutUtils.h"
#include "nsPlaceholderFrame.h"
#include "nsSubDocumentFrame.h"

namespace mozilla {

using gfx::IntSize;

using layers::FrameMetrics;
using layers::ScrollableLayerGuid;

typedef ScrollableLayerGuid::ViewID ViewID;

static LazyLogModule sDisplayportLog("apz.displayport");

/* static */
DisplayPortMargins DisplayPortMargins::FromAPZ(const ScreenMargin& aMargins,
                                               const CSSPoint& aVisualOffset,
                                               const CSSPoint& aLayoutOffset) {
  return DisplayPortMargins{aMargins, aVisualOffset, aLayoutOffset};
}

/* static */
DisplayPortMargins DisplayPortMargins::ForScrollContainerFrame(
    ScrollContainerFrame* aScrollContainerFrame, const ScreenMargin& aMargins) {
  CSSPoint visualOffset;
  CSSPoint layoutOffset;
  if (aScrollContainerFrame) {
    PresShell* presShell = aScrollContainerFrame->PresShell();
    layoutOffset =
        CSSPoint::FromAppUnits(aScrollContainerFrame->GetScrollPosition());
    if (aScrollContainerFrame->IsRootScrollFrameOfDocument()) {
      visualOffset =
          CSSPoint::FromAppUnits(presShell->GetVisualViewportOffset());

    } else {
      visualOffset = layoutOffset;
    }
  }
  return DisplayPortMargins{aMargins, visualOffset, layoutOffset};
}

/* static */
DisplayPortMargins DisplayPortMargins::ForContent(
    nsIContent* aContent, const ScreenMargin& aMargins) {
  return ForScrollContainerFrame(
      aContent ? nsLayoutUtils::FindScrollContainerFrameFor(aContent) : nullptr,
      aMargins);
}

ScreenMargin DisplayPortMargins::GetRelativeToLayoutViewport(
    ContentGeometryType aGeometryType,
    ScrollContainerFrame* aScrollContainerFrame,
    const CSSToScreenScale2D& aDisplayportScale) const {
  // APZ wants |mMargins| applied relative to the visual viewport.
  // The main-thread painting code applies margins relative to
  // the layout viewport. To get the main thread to paint the
  // area APZ wants, apply a translation between the two. The
  // magnitude of the translation depends on whether we are
  // applying the displayport to scrolled or fixed content.
  CSSPoint scrollDeltaCss =
      ComputeAsyncTranslation(aGeometryType, aScrollContainerFrame);
  ScreenPoint scrollDelta = scrollDeltaCss * aDisplayportScale;
  ScreenMargin margins = mMargins;
  margins.left -= scrollDelta.x;
  margins.right += scrollDelta.x;
  margins.top -= scrollDelta.y;
  margins.bottom += scrollDelta.y;
  return margins;
}

std::ostream& operator<<(std::ostream& aOs,
                         const DisplayPortMargins& aMargins) {
  if (aMargins.mVisualOffset == CSSPoint() &&
      aMargins.mLayoutOffset == CSSPoint()) {
    aOs << aMargins.mMargins;
  } else {
    aOs << "{" << aMargins.mMargins << "," << aMargins.mVisualOffset << ","
        << aMargins.mLayoutOffset << "}";
  }
  return aOs;
}

CSSPoint DisplayPortMargins::ComputeAsyncTranslation(
    ContentGeometryType aGeometryType,
    ScrollContainerFrame* aScrollContainerFrame) const {
  // If we are applying the displayport to scrolled content, the
  // translation is the entire difference between the visual and
  // layout offsets.
  if (aGeometryType == ContentGeometryType::Scrolled) {
    return mVisualOffset - mLayoutOffset;
  }

  // If we are applying the displayport to fixed content, only
  // part of the difference between the visual and layout offsets
  // should be applied. This is because fixed content remains fixed
  // to the layout viewport, and some of the async delta between
  // the visual and layout offsets can drag the layout viewport
  // with it. We want only the remaining delta, i.e. the offset of
  // the visual viewport relative to the (async-scrolled) layout
  // viewport.
  if (!aScrollContainerFrame) {
    // Displayport on a non-scrolling frame for some reason.
    // There will be no divergence between the two viewports.
    return CSSPoint();
  }
  // Fixed content is always fixed to an RSF.
  MOZ_ASSERT(aScrollContainerFrame->IsRootScrollFrameOfDocument());
  if (!aScrollContainerFrame->PresShell()->IsVisualViewportSizeSet()) {
    // Zooming is disabled, so the layout viewport tracks the
    // visual viewport completely.
    return CSSPoint();
  }
  // Use KeepLayoutViewportEnclosingViewportVisual() to compute
  // an async layout viewport the way APZ would.
  const CSSRect visualViewport{
      mVisualOffset,
      // TODO: There are probably some edge cases here around async zooming
      // that are not currently being handled properly. For proper handling,
      // we'd likely need to save APZ's async zoom when populating
      // mVisualOffset, and using it to adjust the visual viewport size here.
      // Note that any incorrectness caused by this will only occur transiently
      // during async zooming.
      CSSSize::FromAppUnits(
          aScrollContainerFrame->PresShell()->GetVisualViewportSize())};
  const CSSRect scrollableRect = CSSRect::FromAppUnits(
      nsLayoutUtils::CalculateExpandedScrollableRect(aScrollContainerFrame));
  CSSRect asyncLayoutViewport{
      mLayoutOffset,
      CSSSize::FromAppUnits(aScrollContainerFrame->GetScrollPortRect().Size())};
  FrameMetrics::KeepLayoutViewportEnclosingVisualViewport(
      visualViewport, scrollableRect, /* out */ asyncLayoutViewport);
  return mVisualOffset - asyncLayoutViewport.TopLeft();
}

static nsRect GetDisplayPortFromRectData(nsIContent* aContent,
                                         DisplayPortPropertyData* aRectData) {
  // In the case where the displayport is set as a rect, we assume it is
  // already aligned and clamped as necessary. The burden to do that is
  // on the setter of the displayport. In practice very few places set the
  // displayport directly as a rect (mostly tests).
  return aRectData->mRect;
}

static nsRect GetDisplayPortFromMarginsData(
    nsIContent* aContent, DisplayPortMarginsPropertyData* aMarginsData,
    const DisplayPortOptions& aOptions) {
  // In the case where the displayport is set via margins, we apply the margins
  // to a base rect. Then we align the expanded rect based on the alignment
  // requested, and finally, clamp it to the size of the scrollable rect.

  nsRect base;
  if (nsRect* baseData = static_cast<nsRect*>(
          aContent->GetProperty(nsGkAtoms::DisplayPortBase))) {
    base = *baseData;
  } else {
    // In theory we shouldn't get here, but we do sometimes (see bug 1212136).
    // Fall through for graceful handling.
  }

  nsIFrame* frame = nsLayoutUtils::GetScrollContainerFrameFromContent(aContent);
  if (!frame) {
    // Turns out we can't really compute it. Oops. We still should return
    // something sane.
    NS_WARNING(
        "Attempting to get a displayport from a content with no primary "
        "frame!");
    return base;
  }

  bool isRoot = false;
  if (aContent->OwnerDoc()->GetRootElement() == aContent) {
    isRoot = true;
  }

  ScrollContainerFrame* scrollContainerFrame = frame->GetScrollTargetFrame();
  nsPoint scrollPos;
  if (scrollContainerFrame) {
    scrollPos = scrollContainerFrame->GetScrollPosition();
  }

  nsPresContext* presContext = frame->PresContext();
  int32_t auPerDevPixel = presContext->AppUnitsPerDevPixel();

  LayoutDeviceToScreenScale2D res =
      LayoutDeviceToParentLayerScale(
          presContext->PresShell()->GetCumulativeResolution()) *
      nsLayoutUtils::GetTransformToAncestorScaleCrossProcessForFrameMetrics(
          frame);

  // Calculate the expanded scrollable rect, which we'll be clamping the
  // displayport to.
  nsRect expandedScrollableRect =
      nsLayoutUtils::CalculateExpandedScrollableRect(frame);

  // GetTransformToAncestorScale() can return 0. In this case, just return the
  // base rect (clamped to the expanded scrollable rect), as other calculations
  // would run into divisions by zero.
  if (res == LayoutDeviceToScreenScale2D(0, 0)) {
    // Make sure the displayport remains within the scrollable rect.
    return base.MoveInsideAndClamp(expandedScrollableRect - scrollPos);
  }

  // First convert the base rect to screen pixels
  LayoutDeviceToScreenScale2D parentRes = res;
  if (isRoot) {
    // the base rect for root scroll frames is specified in the parent document
    // coordinate space, so it doesn't include the local resolution.
    float localRes = presContext->PresShell()->GetResolution();
    parentRes.xScale /= localRes;
    parentRes.yScale /= localRes;
  }
  ScreenRect screenRect =
      LayoutDeviceRect::FromAppUnits(base, auPerDevPixel) * parentRes;

  // Note on the correctness of applying the alignment in Screen space:
  //   The correct space to apply the alignment in would be Layer space, but
  //   we don't necessarily know the scale to convert to Layer space at this
  //   point because Layout may not yet have chosen the resolution at which to
  //   render (it chooses that in FrameLayerBuilder, but this can be called
  //   during display list building). Therefore, we perform the alignment in
  //   Screen space, which basically assumes that Layout chose to render at
  //   screen resolution; since this is what Layout does most of the time,
  //   this is a good approximation. A proper solution would involve moving
  //   the choosing of the resolution to display-list building time.
  ScreenSize alignment;

  PresShell* presShell = presContext->PresShell();
  MOZ_ASSERT(presShell);

  ScreenMargin margins = aMarginsData->mMargins.GetRelativeToLayoutViewport(
      aOptions.mGeometryType, scrollContainerFrame,
      presContext->CSSToDevPixelScale() * res);

  if (presShell->IsDisplayportSuppressed() ||
      aContent->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
    alignment = ScreenSize(1, 1);
  } else {
    // Moving the displayport is relatively expensive with WR so we use a larger
    // alignment that causes the displayport to move less frequently. The
    // alignment scales up with the size of the base rect so larger scrollframes
    // use a larger alignment, but we clamp the alignment to a power of two
    // between 128 and 1024 (inclusive).
    // This naturally also increases the size of the displayport compared to
    // always using a 128 alignment, so the displayport multipliers are also
    // correspondingly smaller when WR is enabled to prevent the displayport
    // from becoming too big.
    gfx::Size multiplier =
        layers::apz::GetDisplayportAlignmentMultiplier(screenRect.Size());
    alignment = ScreenSize(128 * multiplier.width, 128 * multiplier.height);
  }

  // Avoid division by zero.
  if (alignment.width == 0) {
    alignment.width = 128;
  }
  if (alignment.height == 0) {
    alignment.height = 128;
  }

  // Expand the rect by the margins
  screenRect.Inflate(margins);

  ScreenPoint scrollPosScreen =
      LayoutDevicePoint::FromAppUnits(scrollPos, auPerDevPixel) * res;

  // Align the display port.
  screenRect += scrollPosScreen;
  float x = alignment.width * floor(screenRect.x / alignment.width);
  float y = alignment.height * floor(screenRect.y / alignment.height);
  float w = alignment.width * ceil(screenRect.width / alignment.width + 1);
  float h = alignment.height * ceil(screenRect.height / alignment.height + 1);
  screenRect = ScreenRect(x, y, w, h);
  screenRect -= scrollPosScreen;

  // Convert the aligned rect back into app units.
  nsRect result = LayoutDeviceRect::ToAppUnits(screenRect / res, auPerDevPixel);

  // Make sure the displayport remains within the scrollable rect.
  result = result.MoveInsideAndClamp(expandedScrollableRect - scrollPos);

  return result;
}

static bool GetDisplayPortData(
    nsIContent* aContent, DisplayPortPropertyData** aOutRectData,
    DisplayPortMarginsPropertyData** aOutMarginsData) {
  MOZ_ASSERT(aOutRectData && aOutMarginsData);

  *aOutRectData = static_cast<DisplayPortPropertyData*>(
      aContent->GetProperty(nsGkAtoms::DisplayPort));
  *aOutMarginsData = static_cast<DisplayPortMarginsPropertyData*>(
      aContent->GetProperty(nsGkAtoms::DisplayPortMargins));

  if (!*aOutRectData && !*aOutMarginsData) {
    // This content element has no displayport data at all
    return false;
  }

  if (*aOutRectData && *aOutMarginsData) {
    // choose margins if equal priority
    if ((*aOutRectData)->mPriority > (*aOutMarginsData)->mPriority) {
      *aOutMarginsData = nullptr;
    } else {
      *aOutRectData = nullptr;
    }
  }

  NS_ASSERTION((*aOutRectData == nullptr) != (*aOutMarginsData == nullptr),
               "Only one of aOutRectData or aOutMarginsData should be set!");

  return true;
}

static bool GetWasDisplayPortPainted(nsIContent* aContent) {
  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;

  if (!GetDisplayPortData(aContent, &rectData, &marginsData)) {
    return false;
  }

  return rectData ? rectData->mPainted : marginsData->mPainted;
}

bool DisplayPortUtils::IsMissingDisplayPortBaseRect(nsIContent* aContent) {
  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;

  if (GetDisplayPortData(aContent, &rectData, &marginsData) && marginsData) {
    return !aContent->GetProperty(nsGkAtoms::DisplayPortBase);
  }

  return false;
}

static void TranslateFromScrollPortToScrollContainerFrame(nsIContent* aContent,
                                                          nsRect* aRect) {
  MOZ_ASSERT(aRect);
  if (ScrollContainerFrame* scrollContainerFrame =
          nsLayoutUtils::FindScrollContainerFrameFor(aContent)) {
    *aRect += scrollContainerFrame->GetScrollPortRect().TopLeft();
  }
}

static bool GetDisplayPortImpl(nsIContent* aContent, nsRect* aResult,
                               const DisplayPortOptions& aOptions) {
  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;

  if (!GetDisplayPortData(aContent, &rectData, &marginsData)) {
    return false;
  }

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (frame && !frame->PresShell()->AsyncPanZoomEnabled()) {
    return false;
  }

  if (!aResult) {
    // We have displayport data, but the caller doesn't want the actual
    // rect, so we don't need to actually compute it.
    return true;
  }

  bool isDisplayportSuppressed = false;

  if (frame) {
    nsPresContext* presContext = frame->PresContext();
    MOZ_ASSERT(presContext);
    PresShell* presShell = presContext->PresShell();
    MOZ_ASSERT(presShell);
    isDisplayportSuppressed = presShell->IsDisplayportSuppressed();
  }

  nsRect result;
  if (rectData) {
    result = GetDisplayPortFromRectData(aContent, rectData);
  } else if (isDisplayportSuppressed ||
             nsLayoutUtils::ShouldDisableApzForElement(aContent) ||
             aContent->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
    // Note: the above conditions should be in sync with the conditions in
    // WillUseEmptyDisplayPortMargins.

    // Make a copy of the margins data but set the margins to empty.
    // Do not create a new DisplayPortMargins object with
    // DisplayPortMargins::Empty(), because that will record the visual
    // and layout scroll offsets in place right now on the DisplayPortMargins,
    // and those are only meant to be recorded when the margins are stored.
    DisplayPortMarginsPropertyData noMargins = *marginsData;
    noMargins.mMargins.mMargins = ScreenMargin();
    result = GetDisplayPortFromMarginsData(aContent, &noMargins, aOptions);
  } else {
    result = GetDisplayPortFromMarginsData(aContent, marginsData, aOptions);
  }

  if (aOptions.mRelativeTo == DisplayportRelativeTo::ScrollFrame) {
    TranslateFromScrollPortToScrollContainerFrame(aContent, &result);
  }

  *aResult = result;
  return true;
}

bool DisplayPortUtils::GetDisplayPort(nsIContent* aContent, nsRect* aResult,
                                      const DisplayPortOptions& aOptions) {
  return GetDisplayPortImpl(aContent, aResult, aOptions);
}

bool DisplayPortUtils::HasDisplayPort(nsIContent* aContent) {
  return GetDisplayPort(aContent, nullptr);
}

bool DisplayPortUtils::HasPaintedDisplayPort(nsIContent* aContent) {
  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;
  GetDisplayPortData(aContent, &rectData, &marginsData);
  if (rectData) {
    return rectData->mPainted;
  }
  if (marginsData) {
    return marginsData->mPainted;
  }
  return false;
}

void DisplayPortUtils::MarkDisplayPortAsPainted(nsIContent* aContent) {
  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;
  GetDisplayPortData(aContent, &rectData, &marginsData);
  MOZ_ASSERT(rectData || marginsData,
             "MarkDisplayPortAsPainted should only be called for an element "
             "with a displayport");
  if (rectData) {
    rectData->mPainted = true;
  }
  if (marginsData) {
    marginsData->mPainted = true;
  }
}

bool DisplayPortUtils::HasNonMinimalDisplayPort(nsIContent* aContent) {
  return !aContent->GetProperty(nsGkAtoms::MinimalDisplayPort) &&
         HasDisplayPort(aContent);
}

bool DisplayPortUtils::HasNonMinimalNonZeroDisplayPort(nsIContent* aContent) {
  if (aContent->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
    return false;
  }

  DisplayPortPropertyData* rectData = nullptr;
  DisplayPortMarginsPropertyData* marginsData = nullptr;
  if (!GetDisplayPortData(aContent, &rectData, &marginsData)) {
    return false;
  }

  if (!marginsData) {
    // We have a display port, so if we don't have margin data we must have rect
    // data. We consider such as non zero and non minimal, it's probably not too
    // important as display port rects are only used in tests.
    return true;
  }

  if (marginsData->mMargins.mMargins != ScreenMargin()) {
    return true;
  }

  return false;
}

/* static */
bool DisplayPortUtils::GetDisplayPortForVisibilityTesting(nsIContent* aContent,
                                                          nsRect* aResult) {
  MOZ_ASSERT(aResult);
  return GetDisplayPortImpl(
      aContent, aResult,
      DisplayPortOptions().With(DisplayportRelativeTo::ScrollFrame));
}

void DisplayPortUtils::InvalidateForDisplayPortChange(
    nsIContent* aContent, bool aHadDisplayPort, const nsRect& aOldDisplayPort,
    const nsRect& aNewDisplayPort, RepaintMode aRepaintMode) {
  if (aRepaintMode != RepaintMode::Repaint) {
    return;
  }

  bool changed =
      !aHadDisplayPort || !aOldDisplayPort.IsEqualEdges(aNewDisplayPort);

  nsIFrame* frame = nsLayoutUtils::FindScrollContainerFrameFor(aContent);
  if (changed && frame) {
    // It is important to call SchedulePaint on the same frame that we set the
    // dirty rect properties on so we can find the frame later to remove the
    // properties.
    frame->SchedulePaint();

    if (!nsLayoutUtils::AreRetainedDisplayListsEnabled()) {
      return;
    }

    if (StaticPrefs::layout_display_list_retain_sc()) {
      // DisplayListBuildingDisplayPortRect property is not used when retain sc
      // mode is enabled.
      return;
    }

    auto* builder = nsLayoutUtils::GetRetainedDisplayListBuilder(frame);
    if (!builder) {
      return;
    }

    bool found;
    nsRect* rect = frame->GetProperty(
        nsDisplayListBuilder::DisplayListBuildingDisplayPortRect(), &found);

    if (!found) {
      rect = new nsRect();
      frame->AddProperty(
          nsDisplayListBuilder::DisplayListBuildingDisplayPortRect(), rect);
      frame->SetHasOverrideDirtyRegion(true);

      DL_LOGV("Adding display port building rect for frame %p\n", frame);
      RetainedDisplayListData* data = builder->Data();
      data->Flags(frame) += RetainedDisplayListData::FrameFlag::HasProps;
    } else {
      MOZ_ASSERT(rect, "this property should only store non-null values");
    }

    if (aHadDisplayPort) {
      // We only need to build a display list for any new areas added
      nsRegion newRegion(aNewDisplayPort);
      newRegion.SubOut(aOldDisplayPort);
      rect->UnionRect(*rect, newRegion.GetBounds());
    } else {
      rect->UnionRect(*rect, aNewDisplayPort);
    }
  }
}

bool DisplayPortUtils::SetDisplayPortMargins(
    nsIContent* aContent, PresShell* aPresShell,
    const DisplayPortMargins& aMargins,
    ClearMinimalDisplayPortProperty aClearMinimalDisplayPortProperty,
    uint32_t aPriority, RepaintMode aRepaintMode) {
  MOZ_ASSERT(aContent);
  MOZ_ASSERT(aContent->GetComposedDoc() == aPresShell->GetDocument());

  DisplayPortMarginsPropertyData* currentData =
      static_cast<DisplayPortMarginsPropertyData*>(
          aContent->GetProperty(nsGkAtoms::DisplayPortMargins));
  if (currentData && currentData->mPriority > aPriority) {
    return false;
  }

  if (currentData && currentData->mMargins.mVisualOffset != CSSPoint() &&
      aMargins.mVisualOffset == CSSPoint()) {
    // If we hit this, then it's possible that we're setting a displayport
    // that is wrong because the old one had a layout/visual adjustment and
    // the new one does not.
    MOZ_LOG(sDisplayportLog, LogLevel::Warning,
            ("Dropping visual offset %s",
             ToString(currentData->mMargins.mVisualOffset).c_str()));
  }

  nsIFrame* scrollFrame =
      nsLayoutUtils::GetScrollContainerFrameFromContent(aContent);

  nsRect oldDisplayPort;
  bool hadDisplayPort = false;
  bool wasPainted = GetWasDisplayPortPainted(aContent);
  if (scrollFrame) {
    // We only use the two return values from this function to call
    // InvalidateForDisplayPortChange. InvalidateForDisplayPortChange does
    // nothing if aContent does not have a frame. So getting the displayport is
    // useless if the content has no frame, so we avoid calling this to avoid
    // triggering a warning about not having a frame.
    hadDisplayPort = GetDisplayPort(aContent, &oldDisplayPort);
  }

  aContent->SetProperty(
      nsGkAtoms::DisplayPortMargins,
      new DisplayPortMarginsPropertyData(aMargins, aPriority, wasPainted),
      nsINode::DeleteProperty<DisplayPortMarginsPropertyData>);

  if (aClearMinimalDisplayPortProperty ==
      ClearMinimalDisplayPortProperty::Yes) {
    if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Debug) &&
        aContent->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
      mozilla::layers::ScrollableLayerGuid::ViewID viewID =
          mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;
      nsLayoutUtils::FindIDFor(aContent, &viewID);
      MOZ_LOG(sDisplayportLog, LogLevel::Debug,
              ("SetDisplayPortMargins removing MinimalDisplayPort prop on "
               "scrollId=%" PRIu64 "\n",
               viewID));
    }
    aContent->RemoveProperty(nsGkAtoms::MinimalDisplayPort);
  }

  ScrollContainerFrame* scrollContainerFrame =
      scrollFrame ? scrollFrame->GetScrollTargetFrame() : nullptr;
  if (!scrollContainerFrame) {
    return true;
  }

  nsRect newDisplayPort;
  DebugOnly<bool> hasDisplayPort = GetDisplayPort(aContent, &newDisplayPort);
  MOZ_ASSERT(hasDisplayPort);

  if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Debug)) {
    mozilla::layers::ScrollableLayerGuid::ViewID viewID =
        mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;
    nsLayoutUtils::FindIDFor(aContent, &viewID);
    if (!hadDisplayPort) {
      MOZ_LOG(sDisplayportLog, LogLevel::Debug,
              ("SetDisplayPortMargins %s on scrollId=%" PRIu64 ", newDp=%s\n",
               ToString(aMargins).c_str(), viewID,
               ToString(newDisplayPort).c_str()));
    } else {
      // Use verbose level logging for when an existing displayport got its
      // margins updated.
      MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
              ("SetDisplayPortMargins %s on scrollId=%" PRIu64 ", newDp=%s\n",
               ToString(aMargins).c_str(), viewID,
               ToString(newDisplayPort).c_str()));
    }
  }

  InvalidateForDisplayPortChange(aContent, hadDisplayPort, oldDisplayPort,
                                 newDisplayPort, aRepaintMode);

  scrollContainerFrame->TriggerDisplayPortExpiration();

  // Display port margins changing means that the set of visible frames may
  // have drastically changed. Check if we should schedule an update.
  hadDisplayPort = scrollContainerFrame
                       ->GetDisplayPortAtLastApproximateFrameVisibilityUpdate(
                           &oldDisplayPort);

  bool needVisibilityUpdate = !hadDisplayPort;
  // Check if the total size has changed by a large factor.
  if (!needVisibilityUpdate) {
    if ((newDisplayPort.width > 2 * oldDisplayPort.width) ||
        (oldDisplayPort.width > 2 * newDisplayPort.width) ||
        (newDisplayPort.height > 2 * oldDisplayPort.height) ||
        (oldDisplayPort.height > 2 * newDisplayPort.height)) {
      needVisibilityUpdate = true;
    }
  }
  // Check if it's moved by a significant amount.
  if (!needVisibilityUpdate) {
    if (nsRect* baseData = static_cast<nsRect*>(
            aContent->GetProperty(nsGkAtoms::DisplayPortBase))) {
      nsRect base = *baseData;
      if ((std::abs(newDisplayPort.X() - oldDisplayPort.X()) > base.width) ||
          (std::abs(newDisplayPort.XMost() - oldDisplayPort.XMost()) >
           base.width) ||
          (std::abs(newDisplayPort.Y() - oldDisplayPort.Y()) > base.height) ||
          (std::abs(newDisplayPort.YMost() - oldDisplayPort.YMost()) >
           base.height)) {
        needVisibilityUpdate = true;
      }
    }
  }
  if (needVisibilityUpdate) {
    aPresShell->ScheduleApproximateFrameVisibilityUpdateNow();
  }

  return true;
}

void DisplayPortUtils::SetDisplayPortBase(nsIContent* aContent,
                                          const nsRect& aBase) {
  if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Verbose)) {
    ViewID viewId = nsLayoutUtils::FindOrCreateIDFor(aContent);
    MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
            ("Setting base rect %s for scrollId=%" PRIu64 "\n",
             ToString(aBase).c_str(), viewId));
  }
  if (nsRect* baseData = static_cast<nsRect*>(
          aContent->GetProperty(nsGkAtoms::DisplayPortBase))) {
    *baseData = aBase;
    return;
  }

  aContent->SetProperty(nsGkAtoms::DisplayPortBase, new nsRect(aBase),
                        nsINode::DeleteProperty<nsRect>);
}

void DisplayPortUtils::SetDisplayPortBaseIfNotSet(nsIContent* aContent,
                                                  const nsRect& aBase) {
  if (aContent->GetProperty(nsGkAtoms::DisplayPortBase)) {
    return;
  }
  if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Verbose)) {
    ViewID viewId = nsLayoutUtils::FindOrCreateIDFor(aContent);
    MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
            ("Setting base rect %s for scrollId=%" PRIu64 "\n",
             ToString(aBase).c_str(), viewId));
  }

  aContent->SetProperty(nsGkAtoms::DisplayPortBase, new nsRect(aBase),
                        nsINode::DeleteProperty<nsRect>);
}

void DisplayPortUtils::RemoveDisplayPort(nsIContent* aContent) {
  aContent->RemoveProperty(nsGkAtoms::DisplayPort);
  aContent->RemoveProperty(nsGkAtoms::DisplayPortMargins);
}

bool DisplayPortUtils::ViewportHasDisplayPort(nsPresContext* aPresContext) {
  nsIFrame* rootScrollContainerFrame =
      aPresContext->PresShell()->GetRootScrollContainerFrame();
  return rootScrollContainerFrame &&
         HasDisplayPort(rootScrollContainerFrame->GetContent());
}

bool DisplayPortUtils::IsFixedPosFrameInDisplayPort(const nsIFrame* aFrame) {
  // Fixed-pos frames are parented by the viewport frame or the page content
  // frame. We'll assume that printing/print preview don't have displayports for
  // their pages!
  nsIFrame* parent = aFrame->GetParent();
  if (!parent || parent->GetParent() ||
      aFrame->StyleDisplay()->mPosition != StylePositionProperty::Fixed) {
    return false;
  }
  return ViewportHasDisplayPort(aFrame->PresContext());
}

// We want to this return true for the scroll frame, but not the
// scrolled frame (which has the same content).
bool DisplayPortUtils::FrameHasDisplayPort(nsIFrame* aFrame,
                                           const nsIFrame* aScrolledFrame) {
  if (!aFrame->GetContent() || !HasDisplayPort(aFrame->GetContent())) {
    return false;
  }
  ScrollContainerFrame* sf = do_QueryFrame(aFrame);
  if (sf) {
    if (aScrolledFrame && aScrolledFrame != sf->GetScrolledFrame()) {
      return false;
    }
    return true;
  }
  return false;
}

bool DisplayPortUtils::CalculateAndSetDisplayPortMargins(
    ScrollContainerFrame* aScrollContainerFrame, RepaintMode aRepaintMode) {
  nsIContent* content = aScrollContainerFrame->GetContent();
  MOZ_ASSERT(content);

  FrameMetrics metrics =
      nsLayoutUtils::CalculateBasicFrameMetrics(aScrollContainerFrame);
  ScreenMargin displayportMargins = layers::apz::CalculatePendingDisplayPort(
      metrics, ParentLayerPoint(0.0f, 0.0f));
  PresShell* presShell = aScrollContainerFrame->PresShell();

  DisplayPortMargins margins = DisplayPortMargins::ForScrollContainerFrame(
      aScrollContainerFrame, displayportMargins);

  return SetDisplayPortMargins(content, presShell, margins,
                               ClearMinimalDisplayPortProperty::Yes, 0,
                               aRepaintMode);
}

bool DisplayPortUtils::MaybeCreateDisplayPort(
    nsDisplayListBuilder* aBuilder, ScrollContainerFrame* aScrollContainerFrame,
    RepaintMode aRepaintMode) {
  MOZ_ASSERT(aBuilder->IsPaintingToWindow());

  nsIContent* content = aScrollContainerFrame->GetContent();
  if (!content) {
    return false;
  }

  // We perform an optimization where we ensure that at least one
  // async-scrollable frame (i.e. one that WantsAsyncScroll()) has a
  // displayport. If that's not the case yet, and we are async-scrollable, we
  // will get a displayport.
  MOZ_ASSERT(nsLayoutUtils::AsyncPanZoomEnabled(aScrollContainerFrame));
  if (!aBuilder->HaveScrollableDisplayPort() &&
      aScrollContainerFrame->WantAsyncScroll()) {
    bool haveDisplayPort = HasNonMinimalNonZeroDisplayPort(content);
    // If we don't already have a displayport, calculate and set one.
    if (!haveDisplayPort) {
      // We only use the viewId for logging purposes, but create it
      // unconditionally to minimize impact of enabling logging. If we don't
      // assign a viewId here it will get assigned later anyway so functionally
      // there should be no difference.
      ViewID viewId = nsLayoutUtils::FindOrCreateIDFor(content);
      MOZ_LOG(
          sDisplayportLog, LogLevel::Debug,
          ("Setting DP on first-encountered scrollId=%" PRIu64 "\n", viewId));

      CalculateAndSetDisplayPortMargins(aScrollContainerFrame, aRepaintMode);
#ifdef DEBUG
      haveDisplayPort = HasNonMinimalDisplayPort(content);
      MOZ_ASSERT(haveDisplayPort,
                 "should have a displayport after having just set it");
#endif
    }

    // Record that the we now have a scrollable display port.
    aBuilder->SetHaveScrollableDisplayPort();
    return true;
  }
  return false;
}

nsIFrame* DisplayPortUtils::OneStepInAsyncScrollableAncestorChain(
    nsIFrame* aFrame) {
  if (aFrame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
      nsLayoutUtils::IsReallyFixedPos(aFrame)) {
    return aFrame->PresShell()->GetRootScrollContainerFrame();
  }
  return nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
}

void DisplayPortUtils::SetZeroMarginDisplayPortOnAsyncScrollableAncestors(
    nsIFrame* aFrame) {
  nsIFrame* frame = aFrame;
  while (frame) {
    frame = OneStepInAsyncScrollableAncestorChain(frame);
    if (!frame) {
      break;
    }
    ScrollContainerFrame* scrollAncestor =
        nsLayoutUtils::GetAsyncScrollableAncestorFrame(frame);
    if (!scrollAncestor) {
      break;
    }
    frame = scrollAncestor;
    MOZ_ASSERT(scrollAncestor->WantAsyncScroll() ||
               frame->PresShell()->GetRootScrollContainerFrame() == frame);
    if (nsLayoutUtils::AsyncPanZoomEnabled(frame) &&
        !HasDisplayPort(frame->GetContent())) {
      SetDisplayPortMargins(frame->GetContent(), frame->PresShell(),
                            DisplayPortMargins::Empty(frame->GetContent()),
                            ClearMinimalDisplayPortProperty::No, 0,
                            RepaintMode::Repaint);
    }
  }
}

bool DisplayPortUtils::MaybeCreateDisplayPortInFirstScrollFrameEncountered(
    nsIFrame* aFrame, nsDisplayListBuilder* aBuilder) {
  // Don't descend into the tab bar in chrome, it can be very large and does not
  // contain any async scrollable elements.
  if (XRE_IsParentProcess() && aFrame->GetContent() &&
      aFrame->GetContent()->GetID() == nsGkAtoms::tabbrowser_arrowscrollbox) {
    return false;
  }
  if (aFrame->IsScrollContainerOrSubclass()) {
    auto* sf = static_cast<ScrollContainerFrame*>(aFrame);
    if (MaybeCreateDisplayPort(aBuilder, sf, RepaintMode::Repaint)) {
      // If this was the first displayport found in the first scroll container
      // frame encountered, mark the scroll container frame with the current
      // paint sequence number. This is used later to ensure the displayport
      // created is never expired. When there is a scrollable frame with a first
      // scrollable sequence number found that does not match the current paint
      // sequence number (may occur if the dom was mutated in some way), the
      // value will be reset.
      sf->SetIsFirstScrollableFrameSequenceNumber(
          Some(nsDisplayListBuilder::GetPaintSequenceNumber()));
      return true;
    }
  } else if (aFrame->IsPlaceholderFrame()) {
    nsPlaceholderFrame* placeholder = static_cast<nsPlaceholderFrame*>(aFrame);
    nsIFrame* oof = placeholder->GetOutOfFlowFrame();
    if (oof && !nsLayoutUtils::IsPopup(oof) &&
        MaybeCreateDisplayPortInFirstScrollFrameEncountered(oof, aBuilder)) {
      return true;
    }
  } else if (aFrame->IsSubDocumentFrame()) {
    PresShell* presShell = static_cast<nsSubDocumentFrame*>(aFrame)
                               ->GetSubdocumentPresShellForPainting(0);
    if (nsIFrame* root = presShell ? presShell->GetRootFrame() : nullptr) {
      if (MaybeCreateDisplayPortInFirstScrollFrameEncountered(root, aBuilder)) {
        return true;
      }
    }
  }
  // Checking mMozSubtreeHiddenOnlyVisually is relatively slow because it
  // involves loading more memory. It's only allowed in chrome sheets so let's
  // only support it in the parent process so we can mostly optimize this out in
  // content processes.
  if (XRE_IsParentProcess() &&
      aFrame->StyleUIReset()->mMozSubtreeHiddenOnlyVisually) {
    // Only descend the visible card of deck / tabpanels
    return false;
  }
  for (nsIFrame* child : aFrame->PrincipalChildList()) {
    if (MaybeCreateDisplayPortInFirstScrollFrameEncountered(child, aBuilder)) {
      return true;
    }
  }
  return false;
}

void DisplayPortUtils::ExpireDisplayPortOnAsyncScrollableAncestor(
    nsIFrame* aFrame) {
  nsIFrame* frame = aFrame;
  while (frame) {
    frame = OneStepInAsyncScrollableAncestorChain(frame);
    if (!frame) {
      break;
    }
    ScrollContainerFrame* scrollAncestor =
        nsLayoutUtils::GetAsyncScrollableAncestorFrame(frame);
    if (!scrollAncestor) {
      break;
    }
    frame = scrollAncestor;
    MOZ_ASSERT(frame);
    if (!frame) {
      break;
    }
    MOZ_ASSERT(scrollAncestor->WantAsyncScroll() ||
               frame->PresShell()->GetRootScrollContainerFrame() == frame);
    if (HasDisplayPort(frame->GetContent())) {
      scrollAncestor->TriggerDisplayPortExpiration();
      // Stop after the first trigger. If it failed, there's no point in
      // continuing because all the rest of the frames we encounter are going
      // to be ancestors of |scrollAncestor| which will keep its displayport.
      // If the trigger succeeded, we stop because when the trigger executes
      // it will call this function again to trigger the next ancestor up the
      // chain.
      break;
    }
  }
}

Maybe<nsRect> DisplayPortUtils::GetRootDisplayportBase(PresShell* aPresShell) {
  DebugOnly<nsPresContext*> pc = aPresShell->GetPresContext();
  MOZ_ASSERT(pc, "this function should be called after PresShell::Init");
  MOZ_ASSERT(pc->IsRootContentDocumentCrossProcess() ||
             !pc->GetParentPresContext());

  dom::BrowserChild* browserChild = dom::BrowserChild::GetFrom(aPresShell);
  if (browserChild && !browserChild->IsTopLevel()) {
    // If this is an in-process root in on OOP iframe, use the visible rect if
    // it's been set.
    return browserChild->GetVisibleRect();
  }

  nsIFrame* frame = aPresShell->GetRootScrollContainerFrame();
  if (!frame) {
    frame = aPresShell->GetRootFrame();
  }

  nsRect baseRect;
  if (frame) {
    baseRect = GetDisplayportBase(frame);
  } else {
    baseRect = nsRect(nsPoint(0, 0),
                      aPresShell->GetPresContext()->GetVisibleArea().Size());
  }

  return Some(baseRect);
}

nsRect DisplayPortUtils::GetDisplayportBase(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);

  return nsRect(nsPoint(),
                nsLayoutUtils::CalculateCompositionSizeForFrame(aFrame));
}

bool DisplayPortUtils::WillUseEmptyDisplayPortMargins(nsIContent* aContent) {
  MOZ_ASSERT(HasDisplayPort(aContent));
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame) {
    return false;
  }

  // Note these conditions should be in sync with the conditions where we use
  // empty margins to calculate display port in GetDisplayPortImpl
  return aContent->GetProperty(nsGkAtoms::MinimalDisplayPort) ||
         frame->PresShell()->IsDisplayportSuppressed() ||
         nsLayoutUtils::ShouldDisableApzForElement(aContent);
}

}  // namespace mozilla
