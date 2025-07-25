/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsContainerFrameInlines_h___
#define nsContainerFrameInlines_h___

#include "nsContainerFrame.h"

template <typename ISizeData, typename F>
void nsContainerFrame::DoInlineIntrinsicISize(ISizeData* aData,
                                              F& aHandleChildren) {
  using namespace mozilla;

  auto GetMargin = [](const AnchorResolvedMargin& aCoord) -> nscoord {
    if (!aCoord->IsLengthPercentage()) {
      MOZ_ASSERT(aCoord->IsAuto(), "Didn't resolve anchor functions first?");
      return 0;
    }
    return aCoord->AsLengthPercentage().Resolve(0);
  };

  if (GetPrevInFlow()) {
    return;  // Already added.
  }

  WritingMode wm = GetWritingMode();
  Side startSide = wm.PhysicalSideForInlineAxis(LogicalEdge::Start);
  Side endSide = wm.PhysicalSideForInlineAxis(LogicalEdge::End);

  const nsStylePadding* stylePadding = StylePadding();
  const nsStyleBorder* styleBorder = StyleBorder();
  const nsStyleMargin* styleMargin = StyleMargin();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);

  // This goes at the beginning no matter how things are broken and how
  // messy the bidi situations are, since per CSS2.1 section 8.6
  // (implemented in bug 328168), the startSide border is always on the
  // first line.
  // This frame is a first-in-flow, but it might have a previous bidi
  // continuation, in which case that continuation should handle the startSide
  // border.
  // For box-decoration-break:clone we setup clonePBM = startPBM + endPBM and
  // add that to each line.  For box-decoration-break:slice clonePBM is zero.
  nscoord clonePBM = 0;  // PBM = PaddingBorderMargin
  const bool sliceBreak =
      styleBorder->mBoxDecorationBreak == StyleBoxDecorationBreak::Slice;
  if (!GetPrevContinuation() || MOZ_UNLIKELY(!sliceBreak)) {
    nscoord startPBM =
        // clamp negative calc() to 0
        std::max(stylePadding->mPadding.Get(startSide).Resolve(0), 0) +
        styleBorder->GetComputedBorderWidth(startSide) +
        GetMargin(styleMargin->GetMargin(startSide, anchorResolutionParams));
    if (MOZ_LIKELY(sliceBreak)) {
      aData->mCurrentLine += startPBM;
    } else {
      clonePBM = startPBM;
    }
  }

  nscoord endPBM =
      // clamp negative calc() to 0
      std::max(stylePadding->mPadding.Get(endSide).Resolve(0), 0) +
      styleBorder->GetComputedBorderWidth(endSide) +
      GetMargin(styleMargin->GetMargin(endSide, anchorResolutionParams));
  if (MOZ_UNLIKELY(!sliceBreak)) {
    clonePBM += endPBM;
    aData->mCurrentLine += clonePBM;
  }

  const LineListIterator* savedLine = aData->mLine;
  nsIFrame* const savedLineContainer = aData->LineContainer();

  nsContainerFrame* lastInFlow;
  for (nsContainerFrame* nif = this; nif;
       nif = static_cast<nsContainerFrame*>(nif->GetNextInFlow())) {
    if (aData->mCurrentLine == 0) {
      aData->mCurrentLine = clonePBM;
    }
    aHandleChildren(nif, aData);

    // After we advance to our next-in-flow, the stored line and line container
    // may no longer be correct. Just forget them.
    aData->mLine = nullptr;
    aData->SetLineContainer(nullptr);

    lastInFlow = nif;
  }

  aData->mLine = savedLine;
  aData->SetLineContainer(savedLineContainer);

  // This goes at the end no matter how things are broken and how
  // messy the bidi situations are, since per CSS2.1 section 8.6
  // (implemented in bug 328168), the endSide border is always on the
  // last line.
  // We reached the last-in-flow, but it might have a next bidi
  // continuation, in which case that continuation should handle
  // the endSide border.
  if (MOZ_LIKELY(!lastInFlow->GetNextContinuation() && sliceBreak)) {
    aData->mCurrentLine += endPBM;
  }
}

#endif  // nsContainerFrameInlines_h___
