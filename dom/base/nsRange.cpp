/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the DOM Range object.
 */

#include "RangeBoundary.h"
#include "nscore.h"
#include "nsRange.h"

#include "nsDebug.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsIContent.h"
#include "mozilla/dom/Document.h"
#include "nsError.h"
#include "nsINodeList.h"
#include "nsGkAtoms.h"
#include "nsContentUtils.h"
#include "nsFrameSelection.h"
#include "nsLayoutUtils.h"
#include "nsTextFrame.h"
#include "nsContainerFrame.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/dom/CharacterData.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/RangeBinding.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Text.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/ToString.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Likely.h"
#include "nsCSSFrameConstructor.h"
#include "nsStyleStruct.h"
#include "nsStyleStructInlines.h"
#include "nsComputedDOMStyle.h"
#include "mozilla/dom/InspectorFontFace.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

namespace mozilla {
extern LazyLogModule sSelectionAPILog;
extern void LogStackForSelectionAPI();

template <typename SPT, typename SRT, typename EPT, typename ERT>
static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName1,
                            const RangeBoundaryBase<SPT, SRT>& aBoundary1,
                            const char* aArgName2,
                            const RangeBoundaryBase<EPT, ERT>& aBoundary2,
                            const char* aArgName3, bool aBoolArg) {
  if (aBoundary1 == aBoundary2) {
    MOZ_LOG(sSelectionAPILog, LogLevel::Info,
            ("%p nsRange::%s(%s=%s=%s, %s=%s)", aSelection, aFuncName,
             aArgName1, aArgName2, ToString(aBoundary1).c_str(), aArgName3,
             aBoolArg ? "true" : "false"));
  } else {
    MOZ_LOG(
        sSelectionAPILog, LogLevel::Info,
        ("%p nsRange::%s(%s=%s, %s=%s, %s=%s)", aSelection, aFuncName,
         aArgName1, ToString(aBoundary1).c_str(), aArgName2,
         ToString(aBoundary2).c_str(), aArgName3, aBoolArg ? "true" : "false"));
  }
}
}  // namespace mozilla

using namespace mozilla;
using namespace mozilla::dom;

template already_AddRefed<nsRange> nsRange::Create(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    ErrorResult& aRv, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template already_AddRefed<nsRange> nsRange::Create(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    ErrorResult& aRv, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template already_AddRefed<nsRange> nsRange::Create(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    ErrorResult& aRv, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template already_AddRefed<nsRange> nsRange::Create(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAlloCrossShadowBoundary);

template nsresult nsRange::SetStartAndEnd(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult nsRange::SetStartAndEnd(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult nsRange::SetStartAndEnd(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult nsRange::SetStartAndEnd(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);

template void nsRange::DoSetRange(const RangeBoundary& aStartBoundary,
                                  const RangeBoundary& aEndBoundary,
                                  nsINode* aRootNode, bool aNotInsertedYet,
                                  RangeBehaviour aRangeBehaviour);
template void nsRange::DoSetRange(const RangeBoundary& aStartBoundary,
                                  const RawRangeBoundary& aEndBoundary,
                                  nsINode* aRootNode, bool aNotInsertedYet,
                                  RangeBehaviour aRangeBehaviour);
template void nsRange::DoSetRange(const RawRangeBoundary& aStartBoundary,
                                  const RangeBoundary& aEndBoundary,
                                  nsINode* aRootNode, bool aNotInsertedYet,
                                  RangeBehaviour aRangeBehaviour);
template void nsRange::DoSetRange(const RawRangeBoundary& aStartBoundary,
                                  const RawRangeBoundary& aEndBoundary,
                                  nsINode* aRootNode, bool aNotInsertedYet,
                                  RangeBehaviour aRangeBehaviour);

template void nsRange::CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary);
template void nsRange::CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary);
template void nsRange::CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary);
template void nsRange::CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary);

JSObject* nsRange::WrapObject(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) {
  return Range_Binding::Wrap(aCx, this, aGivenProto);
}

DocGroup* nsRange::GetDocGroup() const {
  return mOwner ? mOwner->GetDocGroup() : nullptr;
}

/******************************************************
 * stack based utility class for managing monitor
 ******************************************************/

static void InvalidateAllFrames(nsINode* aNode) {
  MOZ_ASSERT(aNode, "bad arg");

  nsIFrame* frame = nullptr;
  switch (aNode->NodeType()) {
    case nsINode::TEXT_NODE:
    case nsINode::ELEMENT_NODE: {
      nsIContent* content = static_cast<nsIContent*>(aNode);
      frame = content->GetPrimaryFrame();
      break;
    }
    case nsINode::DOCUMENT_NODE: {
      Document* doc = static_cast<Document*>(aNode);
      PresShell* presShell = doc ? doc->GetPresShell() : nullptr;
      frame = presShell ? presShell->GetRootFrame() : nullptr;
      break;
    }
  }
  for (nsIFrame* f = frame; f; f = f->GetNextContinuation()) {
    f->InvalidateFrameSubtree();
  }
}

/******************************************************
 * constructor/destructor
 ******************************************************/

nsTArray<RefPtr<nsRange>>* nsRange::sCachedRanges = nullptr;

nsRange::~nsRange() {
  NS_ASSERTION(!IsInAnySelection(), "deleting nsRange that is in use");

  // we want the side effects (releases and list removals)
  DoSetRange(RawRangeBoundary(), RawRangeBoundary(), nullptr);
}

nsRange::nsRange(nsINode* aNode)
    : AbstractRange(aNode, /* aIsDynamicRange = */ true, TreeKind::DOM),
      mNextStartRef(nullptr),
      mNextEndRef(nullptr) {
  // printf("Size of nsRange: %zu\n", sizeof(nsRange));

  static_assert(sizeof(nsRange) <= 248,
                "nsRange size shouldn't be increased as far as possible");
}

/* static */
already_AddRefed<nsRange> nsRange::Create(nsINode* aNode) {
  MOZ_ASSERT(aNode);
  if (!sCachedRanges || sCachedRanges->IsEmpty()) {
    return do_AddRef(new nsRange(aNode));
  }
  RefPtr<nsRange> range = sCachedRanges->PopLastElement().forget();
  range->Init(aNode);
  return range.forget();
}

/* static */
template <typename SPT, typename SRT, typename EPT, typename ERT>
already_AddRefed<nsRange> nsRange::Create(
    const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
    const RangeBoundaryBase<EPT, ERT>& aEndBoundary, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == aEndBoundary.GetTreeKind());

  // If we fail to initialize the range a lot, nsRange should have a static
  // initializer since the allocation cost is not cheap in hot path.
  RefPtr<nsRange> range = nsRange::Create(aStartBoundary.GetContainer());
  aRv = range->SetStartAndEnd(aStartBoundary, aEndBoundary,
                              aAllowCrossShadowBoundary);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  return range.forget();
}

/*
 * When a new boundary is given to a nsRange, compare its position with other
 * existing boundaries to see if we need to collapse the end points.
 *
 * aRange: The nsRange that aNewBoundary is being set to.
 * aNewRoot: The shadow-including root of the container of aNewBoundary
 * aNewBoundary: The new boundary
 * aIsSetStart: true if GetRangeBehaviour is called by nsRange::SetStart,
 * false otherwise
 * aAllowCrossShadowBoundary: Indicates whether the boundaries allowed to cross
 * shadow boundary or not
 */
static RangeBehaviour GetRangeBehaviour(
    const nsRange* aRange, const nsINode* aNewRoot,
    const RawRangeBoundary& aNewBoundaryInDOM,
    const Maybe<RawRangeBoundary>& aNewBoundaryInFlat, const bool aIsSetStart,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  if (!aRange->IsPositioned()) {
    return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
  }

  MOZ_ASSERT(aRange->GetRoot());

  if (aNewRoot != aRange->GetRoot()) {
    // Boundaries are in different document (or not connected), so collapse
    // the both the default range and the crossBoundaryRange range.
    if (aNewRoot->GetComposedDoc() != aRange->GetRoot()->GetComposedDoc()) {
      return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
    }

    // Always collapse both ranges if the one of the roots is an UA widget
    // regardless whether the boundaries are allowed to cross shadow boundary
    // or not.
    if (AbstractRange::IsRootUAWidget(aNewRoot) ||
        AbstractRange::IsRootUAWidget(aRange->GetRoot())) {
      return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
    }

    if (const CrossShadowBoundaryRange* crossShadowBoundaryRange =
            aRange->GetCrossShadowBoundaryRange()) {
      // Check if the existing-other-side boundary in
      // aRange::mCrossShadowBoundaryRange has the same root
      // as aNewRoot. If this is the case, it means
      // aRange::mCrossShadowBoundaryRange can be used to represent this
      // cross-boundary selection, meanwhile we collapse the default range since
      // this is a cross-boundary selection.
      const RangeBoundary& otherSideExistingBoundary =
          aIsSetStart ? crossShadowBoundaryRange->EndRef()
                      : crossShadowBoundaryRange->StartRef();
      const nsINode* otherSideRoot =
          RangeUtils::ComputeRootNode(otherSideExistingBoundary.GetContainer());
      if (aNewRoot == otherSideRoot) {
        return RangeBehaviour::CollapseDefaultRange;
      }
    }

    // Different root, but same document. So we only collapse the
    // default range if boundaries are allowed to cross shadow boundary.
    return aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
               ? RangeBehaviour::CollapseDefaultRange
               : RangeBehaviour::
                     CollapseDefaultRangeAndCrossShadowBoundaryRanges;
  }

  const RangeBoundary& otherSideExistingBoundaryInDOM =
      aIsSetStart ? aRange->EndRef() : aRange->StartRef();

  auto CompareFlatTreeBoundaries = [&aNewBoundaryInFlat, aIsSetStart,
                                    &aRange]() {
    MOZ_ASSERT(aRange->GetCrossShadowBoundaryRange());
    MOZ_ASSERT(aNewBoundaryInFlat.isSome() &&
               aNewBoundaryInFlat->IsSetAndValid());
    const RangeBoundary& otherSideExistingCrossShadowBoundaryBoundaryInFlat =
        aIsSetStart ? aRange->GetCrossShadowBoundaryRange()->EndRef()
                    : aRange->GetCrossShadowBoundaryRange()->StartRef();
    const Maybe<int32_t> withCrossShadowBoundaryOrder =
        aIsSetStart
            ? nsContentUtils::ComparePoints<TreeKind::Flat>(
                  aNewBoundaryInFlat.ref(),
                  otherSideExistingCrossShadowBoundaryBoundaryInFlat.AsRaw())
            : nsContentUtils::ComparePoints<TreeKind::Flat>(
                  otherSideExistingCrossShadowBoundaryBoundaryInFlat.AsRaw(),
                  aNewBoundaryInFlat.ref());
    if (withCrossShadowBoundaryOrder && *withCrossShadowBoundaryOrder != 1) {
      return RangeBehaviour::CollapseDefaultRange;
    }

    // Not valid to both existing boundaries.
    return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
  };

  if (!aNewBoundaryInDOM.IsSetAndValid()) {
    return CompareFlatTreeBoundaries();
  }

  // Both boundaries are in the same root, now check for their position
  const Maybe<int32_t> order =
      aIsSetStart
          ? nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                aNewBoundaryInDOM, otherSideExistingBoundaryInDOM.AsRaw())
          : nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                otherSideExistingBoundaryInDOM.AsRaw(), aNewBoundaryInDOM);

  if (order) {
    if (*order != 1) {
      // aNewBoundary is at a valid position.
      //
      // If aIsSetStart is true, this means
      // aNewBoundary <= otherSideExistingBoundary which is
      // good because aNewBoundary intends to be the start.
      //
      // If aIsSetStart is false, this means
      // otherSideExistingBoundary <= aNewBoundary which is good because
      // aNewBoundary intends to be the end.
      //
      // So no collapse for above cases.
      return RangeBehaviour::KeepDefaultRangeAndCrossShadowBoundaryRanges;
    }

    if (!aRange->MayCrossShadowBoundary() ||
        aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::No) {
      return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
    }

    return CompareFlatTreeBoundaries();
  }

  MOZ_ASSERT_UNREACHABLE();
  return RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges;
}
/******************************************************
 * nsISupports
 ******************************************************/

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsRange)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_INTERRUPTABLE_LAST_RELEASE(
    nsRange, DoSetRange(RawRangeBoundary(), RawRangeBoundary(), nullptr),
    MaybeInterruptLastRelease())

// QueryInterface implementation for nsRange
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsRange)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
NS_INTERFACE_MAP_END_INHERITING(AbstractRange)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsRange)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsRange, AbstractRange)
  // `Reset()` unlinks `mStart`, `mEnd` and `mRoot`.
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCrossShadowBoundaryRange);
  tmp->Reset();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsRange, AbstractRange)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRoot)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCrossShadowBoundaryRange);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(nsRange, AbstractRange)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

bool nsRange::MaybeInterruptLastRelease() {
  bool interrupt = AbstractRange::MaybeCacheToReuse(*this);
  ResetCrossShadowBoundaryRange();
  MOZ_ASSERT(!interrupt || IsCleared());
  return interrupt;
}

void nsRange::AdjustNextRefsOnCharacterDataSplit(
    const nsIContent& aContent, const CharacterDataChangeInfo& aInfo) {
  // If the splitted text node is immediately before a range boundary point
  // that refers to a child index (i.e. its parent is the boundary container)
  // then we need to adjust the corresponding boundary to account for the new
  // text node that will be inserted. However, because the new sibling hasn't
  // been inserted yet, that would result in an invalid boundary. Therefore,
  // we store the new child in mNext*Ref to make sure we adjust the boundary
  // in the next ContentInserted or ContentAppended call.
  nsINode* parentNode = aContent.GetParentNode();
  if (parentNode == mEnd.GetContainer()) {
    if (&aContent == mEnd.Ref()) {
      MOZ_ASSERT(aInfo.mDetails->mNextSibling);
      mNextEndRef = aInfo.mDetails->mNextSibling;
    }
  }

  if (parentNode == mStart.GetContainer()) {
    if (&aContent == mStart.Ref()) {
      MOZ_ASSERT(aInfo.mDetails->mNextSibling);
      mNextStartRef = aInfo.mDetails->mNextSibling;
    }
  }
}

nsRange::RangeBoundariesAndRoot
nsRange::DetermineNewRangeBoundariesAndRootOnCharacterDataMerge(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) const {
  RawRangeBoundary newStart;
  RawRangeBoundary newEnd;
  nsINode* newRoot = nullptr;

  // normalize(), aInfo.mDetails->mNextSibling is the merged text node
  // that will be removed
  nsIContent* removed = aInfo.mDetails->mNextSibling;
  if (removed == mStart.GetContainer()) {
    CheckedUint32 newStartOffset{
        *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets)};
    newStartOffset += aInfo.mChangeStart;

    // newStartOffset.isValid() isn't checked explicitly here, because
    // newStartOffset.value() contains an assertion.
    newStart = {aContent, newStartOffset.value()};
    if (MOZ_UNLIKELY(removed == mRoot)) {
      newRoot = RangeUtils::ComputeRootNode(newStart.GetContainer());
    }
  }
  if (removed == mEnd.GetContainer()) {
    CheckedUint32 newEndOffset{
        *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets)};
    newEndOffset += aInfo.mChangeStart;

    // newEndOffset.isValid() isn't checked explicitly here, because
    // newEndOffset.value() contains an assertion.
    newEnd = {aContent, newEndOffset.value()};
    if (MOZ_UNLIKELY(removed == mRoot)) {
      newRoot = {RangeUtils::ComputeRootNode(newEnd.GetContainer())};
    }
  }
  // When the removed text node's parent is one of our boundary nodes we may
  // need to adjust the offset to account for the removed node. However,
  // there will also be a ContentRemoved notification later so the only cases
  // we need to handle here is when the removed node is the text node after
  // the boundary.  (The m*Offset > 0 check is an optimization - a boundary
  // point before the first child is never affected by normalize().)
  nsINode* parentNode = aContent->GetParentNode();
  if (parentNode == mStart.GetContainer() &&
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) > 0 &&
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) <
          parentNode->GetChildCount() &&
      removed == mStart.GetChildAtOffset()) {
    newStart = {aContent, aInfo.mChangeStart};
  }
  if (parentNode == mEnd.GetContainer() &&
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) > 0 &&
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) <
          parentNode->GetChildCount() &&
      removed == mEnd.GetChildAtOffset()) {
    newEnd = {aContent, aInfo.mChangeEnd};
  }

  return {newStart, newEnd, newRoot};
}

/******************************************************
 * nsIMutationObserver implementation
 ******************************************************/
void nsRange::CharacterDataChanged(nsIContent* aContent,
                                   const CharacterDataChangeInfo& aInfo) {
  MOZ_ASSERT(aContent);
  MOZ_ASSERT(mIsPositioned);
  MOZ_ASSERT(!mNextEndRef);
  MOZ_ASSERT(!mNextStartRef);

  nsINode* newRoot = nullptr;
  RawRangeBoundary newStart;
  RawRangeBoundary newEnd;

  if (aInfo.mDetails &&
      aInfo.mDetails->mType == CharacterDataChangeInfo::Details::eSplit) {
    AdjustNextRefsOnCharacterDataSplit(*aContent, aInfo);
  }

  // If the changed node contains our start boundary and the change starts
  // before the boundary we'll need to adjust the offset.
  if (aContent == mStart.GetContainer() &&
      aInfo.mChangeStart <
          *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets)) {
    if (aInfo.mDetails) {
      // splitText(), aInfo->mDetails->mNextSibling is the new text node
      NS_ASSERTION(
          aInfo.mDetails->mType == CharacterDataChangeInfo::Details::eSplit,
          "only a split can start before the end");
      NS_ASSERTION(
          *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) <=
              aInfo.mChangeEnd + 1,
          "mStart.Offset() is beyond the end of this node");
      const uint32_t newStartOffset =
          *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) -
          aInfo.mChangeStart;
      newStart = {aInfo.mDetails->mNextSibling, newStartOffset};
      if (MOZ_UNLIKELY(aContent == mRoot)) {
        newRoot = RangeUtils::ComputeRootNode(newStart.GetContainer());
      }

      bool isCommonAncestor =
          IsInAnySelection() && mStart.GetContainer() == mEnd.GetContainer();
      if (isCommonAncestor) {
        MOZ_DIAGNOSTIC_ASSERT(mStart.GetContainer() ==
                              mRegisteredClosestCommonInclusiveAncestor);
        UnregisterClosestCommonInclusiveAncestor();
        RegisterClosestCommonInclusiveAncestor(newStart.GetContainer());
      }
      if (mStart.GetContainer()
              ->IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
        newStart.GetContainer()
            ->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      }
    } else {
      newStart = ComputeNewBoundaryWhenBoundaryInsideChangedText(
          aInfo, mStart.AsRaw());
    }
  }

  // Do the same thing for the end boundary, except for splitText of a node
  // with no parent then only switch to the new node if the start boundary
  // did so too (otherwise the range would end up with disconnected nodes).
  if (aContent == mEnd.GetContainer() &&
      aInfo.mChangeStart <
          *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets)) {
    if (aInfo.mDetails &&
        (aContent->GetParentNode() || newStart.GetContainer())) {
      // splitText(), aInfo.mDetails->mNextSibling is the new text node
      NS_ASSERTION(
          aInfo.mDetails->mType == CharacterDataChangeInfo::Details::eSplit,
          "only a split can start before the end");
      MOZ_ASSERT(
          *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) <=
              aInfo.mChangeEnd + 1,
          "mEnd.Offset() is beyond the end of this node");

      const uint32_t newEndOffset{
          *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets) -
          aInfo.mChangeStart};
      newEnd = {aInfo.mDetails->mNextSibling, newEndOffset};

      bool isCommonAncestor =
          IsInAnySelection() && mStart.GetContainer() == mEnd.GetContainer();
      if (isCommonAncestor && !newStart.GetContainer()) {
        MOZ_DIAGNOSTIC_ASSERT(mStart.GetContainer() ==
                              mRegisteredClosestCommonInclusiveAncestor);
        // The split occurs inside the range.
        UnregisterClosestCommonInclusiveAncestor();
        RegisterClosestCommonInclusiveAncestor(
            mStart.GetContainer()->GetParentNode());
        newEnd.GetContainer()
            ->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      } else if (
          mEnd.GetContainer()
              ->IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
        newEnd.GetContainer()
            ->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      }
    } else {
      newEnd =
          ComputeNewBoundaryWhenBoundaryInsideChangedText(aInfo, mEnd.AsRaw());
    }
  }

  if (aInfo.mDetails &&
      aInfo.mDetails->mType == CharacterDataChangeInfo::Details::eMerge) {
    MOZ_ASSERT(!newStart.IsSet());
    MOZ_ASSERT(!newEnd.IsSet());

    RangeBoundariesAndRoot rangeBoundariesAndRoot =
        DetermineNewRangeBoundariesAndRootOnCharacterDataMerge(aContent, aInfo);

    newStart = rangeBoundariesAndRoot.mStart;
    newEnd = rangeBoundariesAndRoot.mEnd;
    newRoot = rangeBoundariesAndRoot.mRoot;
  }

  if (newStart.IsSet() || newEnd.IsSet()) {
    if (!newStart.IsSet()) {
      newStart.CopyFrom(mStart, RangeBoundaryIsMutationObserved::Yes);
    }
    if (!newEnd.IsSet()) {
      newEnd.CopyFrom(mEnd, RangeBoundaryIsMutationObserved::Yes);
    }
    DoSetRange(newStart, newEnd, newRoot ? newRoot : mRoot.get(),
               !newEnd.GetContainer()->GetParentNode() ||
                   !newStart.GetContainer()->GetParentNode());
  } else {
    nsRange::AssertIfMismatchRootAndRangeBoundaries(
        mStart, mEnd, mRoot,
        (mStart.IsSet() && !mStart.GetContainer()->GetParentNode()) ||
            (mEnd.IsSet() && !mEnd.GetContainer()->GetParentNode()));
  }
}

void nsRange::ContentAppended(nsIContent* aFirstNewContent,
                              const ContentAppendInfo&) {
  MOZ_ASSERT(mIsPositioned);

  nsINode* container = aFirstNewContent->GetParentNode();
  MOZ_ASSERT(container);
  if (container->IsMaybeSelected() && IsInAnySelection()) {
    nsINode* child = aFirstNewContent;
    while (child) {
      if (!child
               ->IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
        MarkDescendants(*child);
        child
            ->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      }
      child = child->GetNextSibling();
    }
  }

  if (mNextStartRef || mNextEndRef) {
    // A splitText has occurred, if any mNext*Ref was set, we need to adjust
    // the range boundaries.
    if (mNextStartRef) {
      mStart = {mStart.GetContainer(), mNextStartRef};
      MOZ_ASSERT(mNextStartRef == aFirstNewContent);
      mNextStartRef = nullptr;
    }
    if (mNextEndRef) {
      mEnd = {mEnd.GetContainer(), mNextEndRef};
      MOZ_ASSERT(mNextEndRef == aFirstNewContent);
      mNextEndRef = nullptr;
    }
    DoSetRange(mStart, mEnd, mRoot, true);
  } else {
    nsRange::AssertIfMismatchRootAndRangeBoundaries(mStart, mEnd, mRoot);
  }
}

void nsRange::ContentInserted(nsIContent* aChild, const ContentInsertInfo&) {
  MOZ_ASSERT(mIsPositioned);

  bool updateBoundaries = false;
  nsINode* container = aChild->GetParentNode();
  MOZ_ASSERT(container);
  RawRangeBoundary newStart(mStart, RangeBoundaryIsMutationObserved::Yes);
  RawRangeBoundary newEnd(mEnd, RangeBoundaryIsMutationObserved::Yes);
  MOZ_ASSERT(aChild->GetParentNode() == container);

  // Invalidate boundary offsets if a child that may have moved them was
  // inserted.
  if (container == mStart.GetContainer()) {
    newStart.InvalidateOffset();
    updateBoundaries = true;
  }

  if (container == mEnd.GetContainer()) {
    newEnd.InvalidateOffset();
    updateBoundaries = true;
  }

  if (container->IsMaybeSelected() &&
      !aChild
           ->IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
    MarkDescendants(*aChild);
    aChild->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
  }

  if (mNextStartRef || mNextEndRef) {
    if (mNextStartRef) {
      newStart = {mStart.GetContainer(), mNextStartRef};
      MOZ_ASSERT(mNextStartRef == aChild);
      mNextStartRef = nullptr;
    }
    if (mNextEndRef) {
      newEnd = {mEnd.GetContainer(), mNextEndRef};
      MOZ_ASSERT(mNextEndRef == aChild);
      mNextEndRef = nullptr;
    }

    updateBoundaries = true;
  }

  if (updateBoundaries) {
    DoSetRange(newStart, newEnd, mRoot);
  } else {
    nsRange::AssertIfMismatchRootAndRangeBoundaries(mStart, mEnd, mRoot);
  }
}

void nsRange::ContentWillBeRemoved(nsIContent* aChild,
                                   const ContentRemoveInfo&) {
  MOZ_ASSERT(mIsPositioned);

  nsINode* container = aChild->GetParentNode();
  MOZ_ASSERT(container);

  nsINode* startContainer = mStart.GetContainer();
  nsINode* endContainer = mEnd.GetContainer();

  RawRangeBoundary newStart;
  RawRangeBoundary newEnd;
  Maybe<bool> gravitateStart;
  bool gravitateEnd;

  // Adjust position if a sibling was removed...
  if (container == startContainer) {
    // We're only interested if our boundary reference was removed, otherwise
    // we can just invalidate the offset.
    if (aChild == mStart.Ref()) {
      newStart = {container, aChild->GetPreviousSibling()};
    } else {
      newStart.CopyFrom(mStart, RangeBoundaryIsMutationObserved::Yes);
      newStart.InvalidateOffset();
    }
  } else {
    gravitateStart = Some(startContainer->IsInclusiveDescendantOf(aChild));
    if (gravitateStart.value()) {
      newStart = {container, aChild->GetPreviousSibling()};
    }
  }

  // Do same thing for end boundry.
  if (container == endContainer) {
    if (aChild == mEnd.Ref()) {
      newEnd = {container, aChild->GetPreviousSibling()};
    } else {
      newEnd.CopyFrom(mEnd, RangeBoundaryIsMutationObserved::Yes);
      newEnd.InvalidateOffset();
    }
  } else {
    if (startContainer == endContainer && gravitateStart.isSome()) {
      gravitateEnd = gravitateStart.value();
    } else {
      gravitateEnd = endContainer->IsInclusiveDescendantOf(aChild);
    }
    if (gravitateEnd) {
      newEnd = {container, aChild->GetPreviousSibling()};
    }
  }

  bool newStartIsSet = newStart.IsSet();
  bool newEndIsSet = newEnd.IsSet();
  if (newStartIsSet || newEndIsSet) {
    DoSetRange(
        newStartIsSet ? newStart : mStart.AsRaw(),
        newEndIsSet ? newEnd : mEnd.AsRaw(), mRoot, false,
        // CrossShadowBoundaryRange mutates content
        // removal fot itself, so no need for nsRange to do anything with it.
        RangeBehaviour::KeepDefaultRangeAndCrossShadowBoundaryRanges);
  } else {
    nsRange::AssertIfMismatchRootAndRangeBoundaries(mStart, mEnd, mRoot);
  }

  MOZ_ASSERT(mStart.Ref() != aChild);
  MOZ_ASSERT(mEnd.Ref() != aChild);

  if (container->IsMaybeSelected() &&
      aChild
          ->IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
    aChild
        ->ClearDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
    UnmarkDescendants(*aChild);
  }
}

void nsRange::ParentChainChanged(nsIContent* aContent) {
  NS_ASSERTION(mRoot == aContent, "Wrong ParentChainChanged notification?");
  nsINode* newRoot = RangeUtils::ComputeRootNode(mStart.GetContainer());
  NS_ASSERTION(newRoot, "No valid boundary or root found!");
  if (newRoot != RangeUtils::ComputeRootNode(mEnd.GetContainer())) {
    // Sometimes ordering involved in cycle collection can lead to our
    // start parent and/or end parent being disconnected from our root
    // without our getting a ContentRemoved notification.
    // See bug 846096 for more details.
    NS_ASSERTION(mEnd.GetContainer()->IsInNativeAnonymousSubtree(),
                 "This special case should happen only with "
                 "native-anonymous content");
    // When that happens, bail out and set pointers to null; since we're
    // in cycle collection and unreachable it shouldn't matter.
    Reset();
    return;
  }
  // This is safe without holding a strong ref to self as long as the change
  // of mRoot is the last thing in DoSetRange.
  DoSetRange(mStart, mEnd, newRoot);
}

bool nsRange::IsShadowIncludingInclusiveDescendantOfCrossBoundaryRangeAncestor(
    const nsINode& aContainer) const {
  MOZ_ASSERT(mCrossShadowBoundaryRange &&
             mCrossShadowBoundaryRange->GetCommonAncestor());
  return aContainer.IsShadowIncludingInclusiveDescendantOf(
      mCrossShadowBoundaryRange->GetCommonAncestor());
}

bool nsRange::IsPointComparableToRange(const nsINode& aContainer,
                                       uint32_t aOffset,
                                       bool aAllowCrossShadowBoundary,
                                       ErrorResult& aRv) const {
  // our range is in a good state?
  if (!mIsPositioned) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return false;
  }

  const bool isContainerInRange =
      aContainer.IsInclusiveDescendantOf(mRoot) ||
      (aAllowCrossShadowBoundary && mCrossShadowBoundaryRange &&
       IsShadowIncludingInclusiveDescendantOfCrossBoundaryRangeAncestor(
           aContainer));

  if (!isContainerInRange) {
    // TODO(emilio): Switch to ThrowWrongDocumentError, but IsPointInRange
    // relies on the error code right now in order to suppress the exception.
    aRv.Throw(NS_ERROR_DOM_WRONG_DOCUMENT_ERR);
    return false;
  }

  auto chromeOnlyAccess = mStart.GetContainer()->ChromeOnlyAccess();
  NS_ASSERTION(chromeOnlyAccess == mEnd.GetContainer()->ChromeOnlyAccess(),
               "Start and end of a range must be either both native anonymous "
               "content or not.");
  if (aContainer.ChromeOnlyAccess() != chromeOnlyAccess) {
    aRv.ThrowInvalidNodeTypeError(
        "Trying to compare restricted with unrestricted nodes");
    return false;
  }

  if (aContainer.NodeType() == nsINode::DOCUMENT_TYPE_NODE) {
    aRv.ThrowInvalidNodeTypeError("Trying to compare with a document");
    return false;
  }

  if (aOffset > aContainer.Length()) {
    aRv.ThrowIndexSizeError("Offset is out of bounds");
    return false;
  }

  return true;
}

bool nsRange::IsPointInRange(const nsINode& aContainer, uint32_t aOffset,
                             ErrorResult& aRv,
                             bool aAllowCrossShadowBoundary) const {
  int16_t compareResult =
      ComparePoint(aContainer, aOffset, aRv, aAllowCrossShadowBoundary);
  // If the node isn't in the range's document, it clearly isn't in the range.
  if (aRv.ErrorCodeIs(NS_ERROR_DOM_WRONG_DOCUMENT_ERR)) {
    aRv.SuppressException();
    return false;
  }

  return compareResult == 0;
}

int16_t nsRange::ComparePoint(const nsINode& aContainer, uint32_t aOffset,
                              ErrorResult& aRv,
                              bool aAllowCrossShadowBoundary) const {
  if (!IsPointComparableToRange(aContainer, aOffset, aAllowCrossShadowBoundary,
                                aRv)) {
    return 0;
  }

  const auto& startRef =
      aAllowCrossShadowBoundary ? MayCrossShadowBoundaryStartRef() : StartRef();

  const RawRangeBoundary point{const_cast<nsINode*>(&aContainer), aOffset,
                               RangeBoundaryIsMutationObserved::Yes,
                               startRef.GetTreeKind()};

  MOZ_ASSERT(point.IsSetAndValid());

  if (Maybe<int32_t> order = nsContentUtils::ComparePoints(
          point, aAllowCrossShadowBoundary ? MayCrossShadowBoundaryStartRef()
                                           : StartRef());
      order && *order <= 0) {
    return int16_t(*order);
  }
  if (Maybe<int32_t> order = nsContentUtils::ComparePoints(
          aAllowCrossShadowBoundary ? MayCrossShadowBoundaryEndRef() : EndRef(),
          point);
      order && *order == -1) {
    return 1;
  }
  return 0;
}

bool nsRange::IntersectsNode(nsINode& aNode, ErrorResult& aRv) {
  if (!mIsPositioned) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return false;
  }

  nsINode* parent = aNode.GetParentNode();
  if (!parent) {
    // |parent| is null, so |node|'s root is |node| itself.
    return GetRoot() == &aNode;
  }

  const Maybe<uint32_t> nodeIndex = parent->ComputeIndexOf(&aNode);
  if (nodeIndex.isNothing()) {
    return false;
  }

  if (!IsPointComparableToRange(*parent, *nodeIndex,
                                false /* aAllowCrossShadowBoundary */,
                                IgnoreErrors())) {
    return false;
  }

  const Maybe<int32_t> startOrder = nsContentUtils::ComparePoints(
      mStart, RawRangeBoundary(parent, aNode.AsContent(), *nodeIndex + 1u));
  if (startOrder && (*startOrder < 0)) {
    const Maybe<int32_t> endOrder = nsContentUtils::ComparePoints(
        RawRangeBoundary(parent, aNode.GetPreviousSibling(), *nodeIndex), mEnd);
    return endOrder && (*endOrder < 0);
  }

  return false;
}

void nsRange::NotifySelectionListenersAfterRangeSet() {
  if (mSelections.IsEmpty()) {
    return;
  }

  // Our internal code should not move focus with using this instance while
  // it's calling Selection::NotifySelectionListeners() which may move focus
  // or calls selection listeners.  So, let's set mCalledByJS to false here
  // since non-*JS() methods don't set it to false.
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = false;

  // If this instance is not a proper range for selection, we need to remove
  // this from selections.
  const Document* const docForSelf = mStart.GetComposedDoc();
  const nsFrameSelection* const frameSelection =
      mSelections[0]->GetFrameSelection();
  const Document* const docForSelection =
      frameSelection && frameSelection->GetPresShell()
          ? frameSelection->GetPresShell()->GetDocument()
          : nullptr;
  if (!IsPositioned() || docForSelf != docForSelection) {
    // XXX Why Selection::RemoveRangeAndUnselectFramesAndNotifyListeners() does
    // not set whether the caller is JS or not?
    if (IsPartOfOneSelectionOnly()) {
      RefPtr<Selection> selection = mSelections[0].get();
      selection->RemoveRangeAndUnselectFramesAndNotifyListeners(*this,
                                                                IgnoreErrors());
    } else {
      nsTArray<WeakPtr<Selection>> copiedSelections = mSelections.Clone();
      for (const auto& weakSelection : copiedSelections) {
        RefPtr<Selection> selection = weakSelection.get();
        if (MOZ_LIKELY(selection)) {
          selection->RemoveRangeAndUnselectFramesAndNotifyListeners(
              *this, IgnoreErrors());
        }
      }
    }
    // FYI: NotifySelectionListeners() should be called by
    // RemoveRangeAndUnselectFramesAndNotifyListeners() if it's required.
    // Therefore, we need to do nothing anymore.
    return;
  }

  // Notify all Selections. This may modify the range,
  // remove it from the selection, or the selection itself may have gone after
  // the call. Also, new selections may be added.
  // To ensure that listeners are notified for all *current* selections,
  // create a copy of the list of selections and use that for iterating. This
  // way selections can be added or removed safely during iteration.
  // To save allocation cost, the copy is only created if there is more than
  // one Selection present  (which will barely ever be the case).
  if (IsPartOfOneSelectionOnly()) {
    RefPtr<Selection> selection = mSelections[0].get();
#ifdef ACCESSIBILITY
    a11y::SelectionManager::SelectionRangeChanged(selection->GetType(), *this);
#endif
    selection->NotifySelectionListeners(calledByJSRestorer.SavedValue());
  } else {
    nsTArray<WeakPtr<Selection>> copiedSelections = mSelections.Clone();
    for (const auto& weakSelection : copiedSelections) {
      RefPtr<Selection> selection = weakSelection.get();
      if (MOZ_LIKELY(selection)) {
#ifdef ACCESSIBILITY
        a11y::SelectionManager::SelectionRangeChanged(selection->GetType(),
                                                      *this);
#endif
        selection->NotifySelectionListeners(calledByJSRestorer.SavedValue());
      }
    }
  }
}

/******************************************************
 * Private helper routines
 ******************************************************/

// static
template <typename SPT, typename SRT, typename EPT, typename ERT>
void nsRange::AssertIfMismatchRootAndRangeBoundaries(
    const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
    const RangeBoundaryBase<EPT, ERT>& aEndBoundary, const nsINode* aRootNode,
    bool aNotInsertedYet /* = false */) {
#ifdef DEBUG
  if (!aRootNode) {
    MOZ_ASSERT(!aStartBoundary.IsSet());
    MOZ_ASSERT(!aEndBoundary.IsSet());
    return;
  }

  MOZ_ASSERT(aStartBoundary.IsSet());
  MOZ_ASSERT(aEndBoundary.IsSet());
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == aEndBoundary.GetTreeKind());

  if (!aNotInsertedYet) {
    // Compute temporary root for given range boundaries.  If a range in native
    // anonymous subtree is being removed, tempRoot may return the fragment's
    // root content, but it shouldn't be used for new root node because the node
    // may be bound to the root element again.
    nsINode* tempRoot =
        RangeUtils::ComputeRootNode(aStartBoundary.GetContainer());
    // The new range should be in the temporary root node at least.
    MOZ_ASSERT(tempRoot ==
               RangeUtils::ComputeRootNode(aEndBoundary.GetContainer()));
    MOZ_ASSERT(
        aStartBoundary.GetContainer()->IsInclusiveDescendantOf(tempRoot));
    MOZ_ASSERT(aEndBoundary.GetContainer()->IsInclusiveDescendantOf(tempRoot));
    // If the new range is not disconnected or not in native anonymous subtree,
    // the temporary root must be same as the new root node.  Otherwise,
    // aRootNode should be the parent of root of the NAC (e.g., `<input>` if the
    // range is in NAC under `<input>`), but tempRoot is now root content node
    // of the disconnected subtree (e.g., `<div>` element in `<input>` element).
    const bool tempRootIsDisconnectedNAC =
        tempRoot->IsInNativeAnonymousSubtree() && !tempRoot->GetParentNode();
    MOZ_ASSERT_IF(!tempRootIsDisconnectedNAC, tempRoot == aRootNode);
  }
  MOZ_ASSERT(aRootNode->IsDocument() || aRootNode->IsAttr() ||
             aRootNode->IsDocumentFragment() || aRootNode->IsContent());
#endif  // #ifdef DEBUG
}

// It's important that all setting of the range start/end points
// go through this function, which will do all the right voodoo
// for content notification of range ownership.
// Calling DoSetRange with either parent argument null will collapse
// the range to have both endpoints point to the other node
template <typename SPT, typename SRT, typename EPT, typename ERT>
void nsRange::
    DoSetRange(const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
               const RangeBoundaryBase<EPT, ERT>& aEndBoundary,
               nsINode* aRootNode,
               bool aNotInsertedYet /* = false */, RangeBehaviour aRangeBehaviour /* = CollapseDefaultRangeAndCrossShadowBoundaryRanges */) {
  mIsPositioned = aStartBoundary.IsSetAndValid() &&
                  aEndBoundary.IsSetAndValid() && aRootNode;
  MOZ_ASSERT_IF(!mIsPositioned, !aStartBoundary.IsSet());
  MOZ_ASSERT_IF(!mIsPositioned, !aEndBoundary.IsSet());
  MOZ_ASSERT_IF(!mIsPositioned, !aRootNode);
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == aEndBoundary.GetTreeKind());
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == TreeKind::DOM);

  nsRange::AssertIfMismatchRootAndRangeBoundaries(aStartBoundary, aEndBoundary,
                                                  aRootNode, aNotInsertedYet);

  if (mRoot != aRootNode) {
    if (mRoot) {
      mRoot->RemoveMutationObserver(this);
    }
    if (aRootNode) {
      aRootNode->AddMutationObserver(this);
    }
  }
  bool checkCommonAncestor =
      (mStart.GetContainer() != aStartBoundary.GetContainer() ||
       mEnd.GetContainer() != aEndBoundary.GetContainer()) &&
      IsInAnySelection() && !aNotInsertedYet;

  // GetClosestCommonInclusiveAncestor is unreliable while we're unlinking
  // (could return null if our start/end have already been unlinked), so make
  // sure to not use it here to determine our "old" current ancestor.
  mStart.CopyFrom(aStartBoundary, RangeBoundaryIsMutationObserved::Yes);
  mEnd.CopyFrom(aEndBoundary, RangeBoundaryIsMutationObserved::Yes);

  if (aRangeBehaviour ==
      RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges) {
    ResetCrossShadowBoundaryRange();
  }

  if (checkCommonAncestor) {
    UpdateCommonAncestorIfNecessary();
  }

  // This needs to be the last thing this function does, other than notifying
  // selection listeners. See comment in ParentChainChanged.
  if (mRoot != aRootNode) {
    mRoot = aRootNode;
  }

  // Notify any selection listeners. This has to occur last because otherwise
  // the world could be observed by a selection listener while the range was in
  // an invalid state. So we run it off of a script runner to ensure it runs
  // after the mutation observers have finished running.
  if (!mSelections.IsEmpty()) {
    if (MOZ_LOG_TEST(sSelectionAPILog, LogLevel::Info)) {
      for (const auto& selection : mSelections) {
        if (selection && selection->Type() == SelectionType::eNormal) {
          LogSelectionAPI(selection, __FUNCTION__, "aStartBoundary",
                          aStartBoundary, "aEndBoundary", aEndBoundary,
                          "aNotInsertedYet", aNotInsertedYet);
          LogStackForSelectionAPI();
        }
      }
    }
    nsContentUtils::AddScriptRunner(
        NewRunnableMethod("NotifySelectionListenersAfterRangeSet", this,
                          &nsRange::NotifySelectionListenersAfterRangeSet));
  }
}

void nsRange::Reset() {
  DoSetRange(RawRangeBoundary(), RawRangeBoundary(), nullptr);
}

/******************************************************
 * public functionality
 ******************************************************/

void nsRange::SetStartJS(nsINode& aNode, uint32_t aOffset, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetStart(aNode, aOffset, aErr);
}

bool nsRange::CanAccess(const nsINode& aNode) const {
  if (nsContentUtils::LegacyIsCallerNativeCode()) {
    return true;
  }
  return nsContentUtils::CanCallerAccess(&aNode);
}

void nsRange::SetStart(
    nsINode& aNode, uint32_t aOffset, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  SetStart(RawRangeBoundary(&aNode, aOffset), aRv, aAllowCrossShadowBoundary);
}

void nsRange::SetStart(
    const RawRangeBoundary& aPoint, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  nsINode* newRoot = RangeUtils::ComputeRootNode(aPoint.GetContainer());
  if (!newRoot) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  // PointInFlat is necessary when the aPoint looks like
  // RangeBoundary(<slot>, an arbitary offset), here aPoint
  // is not a valid RangeBoundary in DOM tree (when the <slot>
  // doesn't have light DOM children), however it could be
  // a valid RangeBoundary in Flat tree. SetStart should
  // still work for this case.

  // It also makes more sense to have CrossShadowBoundaryRange
  // always use PointInFlat because this is the composed range
  // that we care about, and we care it in Flat tree.
  // It's error prone if we mix the usage of DOM RangeBoundary
  // versus Flat RangeBoundary.
  auto pointInFlat =
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
          ? Some(aPoint.AsRangeBoundaryInFlatTree())
          : Nothing();

  if (!aPoint.IsSetAndValid() &&
      (!pointInFlat || !pointInFlat->IsSetAndValid())) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  MOZ_ASSERT_IF(pointInFlat, aPoint.IsSet());
  RangeBehaviour behaviour =
      GetRangeBehaviour(this, newRoot, aPoint, pointInFlat,
                        true /* aIsSetStart= */, aAllowCrossShadowBoundary);

  switch (behaviour) {
    case RangeBehaviour::KeepDefaultRangeAndCrossShadowBoundaryRanges:
      // EndRef(..) may be same as mStart or not, depends on
      // the value of mCrossShadowBoundaryRange->mEnd, We need to update
      // mCrossShadowBoundaryRange and the default boundaries separately
      if (aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes) {
        if (MayCrossShadowBoundaryEndRef() != mEnd) {
          CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
              pointInFlat.ref(),
              MayCrossShadowBoundaryEndRef().AsRangeBoundaryInFlatTree());
        }
      }
      if (aPoint.IsSetAndValid()) {
        DoSetRange(aPoint, mEnd, mRoot, false, behaviour);
      }
      break;
    case RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges:
      if (aPoint.IsSetAndValid()) {
        DoSetRange(aPoint, aPoint, newRoot, false, behaviour);
      }
      break;
    case RangeBehaviour::CollapseDefaultRange:
      MOZ_ASSERT(aAllowCrossShadowBoundary ==
                 AllowRangeCrossShadowBoundary::Yes);
      CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
          pointInFlat.ref(),
          MayCrossShadowBoundaryEndRef().AsRangeBoundaryInFlatTree());
      if (aPoint.IsSetAndValid()) {
        DoSetRange(aPoint, aPoint, newRoot, false, behaviour);
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE();
  }
}

void nsRange::SetStartAllowCrossShadowBoundary(nsINode& aNode, uint32_t aOffset,
                                               ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetStart(aNode, aOffset, aErr, AllowRangeCrossShadowBoundary::Yes);
}

void nsRange::SetStartBeforeJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetStartBefore(aNode, aErr);
}

void nsRange::SetStartBefore(
    nsINode& aNode, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  // If the node is being removed from its parent, GetRawRangeBoundaryBefore()
  // returns unset instance.  Then, SetStart() will throw
  // NS_ERROR_DOM_INVALID_NODE_TYPE_ERR.
  SetStart(RangeUtils::GetRawRangeBoundaryBefore(&aNode), aRv,
           aAllowCrossShadowBoundary);
}

void nsRange::SetStartAfterJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetStartAfter(aNode, aErr);
}

void nsRange::SetStartAfter(nsINode& aNode, ErrorResult& aRv) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  // If the node is being removed from its parent, GetRawRangeBoundaryAfter()
  // returns unset instance.  Then, SetStart() will throw
  // NS_ERROR_DOM_INVALID_NODE_TYPE_ERR.
  SetStart(RangeUtils::GetRawRangeBoundaryAfter(&aNode), aRv);
}

void nsRange::SetEndJS(nsINode& aNode, uint32_t aOffset, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetEnd(aNode, aOffset, aErr);
}

void nsRange::SetEnd(nsINode& aNode, uint32_t aOffset, ErrorResult& aRv,
                     AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }
  AutoInvalidateSelection atEndOfBlock(this);
  SetEnd(RawRangeBoundary(&aNode, aOffset), aRv, aAllowCrossShadowBoundary);
}

void nsRange::SetEnd(const RawRangeBoundary& aPoint, ErrorResult& aRv,
                     AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  nsINode* newRoot = RangeUtils::ComputeRootNode(aPoint.GetContainer());
  if (!newRoot) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  // PointInFlat is necessary when the aPoint looks like
  // RangeBoundary(<slot>, an arbitary offset), here aPoint
  // is not a valid RangeBoundary in DOM tree (when the <slot>
  // doesn't have light DOM children), however it could be
  // a valid RangeBoundary in Flat tree. SetEnd should
  // still work for this case.

  // It also makes more sense to have CrossShadowBoundaryRange
  // always use PointInFlat because this is the composed range
  // that we care about, and we care it in Flat tree.
  // It's error prone if we mix the usage of DOM RangeBoundary
  // versus Flat RangeBoundary.
  auto pointInFlat =
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
          ? Some(aPoint.AsRangeBoundaryInFlatTree())
          : Nothing();

  if (!aPoint.IsSetAndValid() &&
      (!pointInFlat || !pointInFlat->IsSetAndValid())) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  MOZ_ASSERT_IF(pointInFlat, aPoint.IsSet());
  RangeBehaviour policy =
      GetRangeBehaviour(this, newRoot, aPoint, pointInFlat,
                        false /* aIsStartStart */, aAllowCrossShadowBoundary);

  switch (policy) {
    case RangeBehaviour::KeepDefaultRangeAndCrossShadowBoundaryRanges:
      // StartRef(..) may be same as mStart or not, depends on
      // the value of mCrossShadowBoundaryRange->mStart, so we need to update
      // mCrossShadowBoundaryRange and the default boundaries separately
      if (aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes) {
        if (MayCrossShadowBoundaryStartRef() != mStart) {
          CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
              MayCrossShadowBoundaryStartRef().AsRangeBoundaryInFlatTree(),
              pointInFlat.ref());
        }
      }
      if (aPoint.IsSetAndValid()) {
        DoSetRange(mStart, aPoint, mRoot, false, policy);
      }
      break;
    case RangeBehaviour::CollapseDefaultRangeAndCrossShadowBoundaryRanges:
      if (aPoint.IsSetAndValid()) {
        DoSetRange(aPoint, aPoint, newRoot, false, policy);
      }
      break;
    case RangeBehaviour::CollapseDefaultRange:
      MOZ_ASSERT(aAllowCrossShadowBoundary ==
                 AllowRangeCrossShadowBoundary::Yes);
      CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
          MayCrossShadowBoundaryStartRef().AsRangeBoundaryInFlatTree(),
          pointInFlat.ref());
      if (aPoint.IsSetAndValid()) {
        DoSetRange(aPoint, aPoint, newRoot, false, policy);
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE();
  }
}

void nsRange::SetEndAllowCrossShadowBoundary(nsINode& aNode, uint32_t aOffset,
                                             ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetEnd(aNode, aOffset, aErr,
         AllowRangeCrossShadowBoundary::Yes /* aAllowCrossShadowBoundary */);
}

void nsRange::SelectNodesInContainer(nsINode* aContainer,
                                     nsIContent* aStartContent,
                                     nsIContent* aEndContent) {
  MOZ_ASSERT(aContainer);
  MOZ_ASSERT(aContainer->ComputeIndexOf(aStartContent).valueOr(0) <=
             aContainer->ComputeIndexOf(aEndContent).valueOr(0));
  MOZ_ASSERT(aStartContent &&
             aContainer->ComputeIndexOf(aStartContent).isSome());
  MOZ_ASSERT(aEndContent && aContainer->ComputeIndexOf(aEndContent).isSome());

  nsINode* newRoot = RangeUtils::ComputeRootNode(aContainer);
  MOZ_ASSERT(newRoot);
  if (!newRoot) {
    return;
  }

  RawRangeBoundary start(aContainer, aStartContent->GetPreviousSibling());
  RawRangeBoundary end(aContainer, aEndContent);
  DoSetRange(start, end, newRoot);
}

void nsRange::SetEndBeforeJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetEndBefore(aNode, aErr);
}

void nsRange::SetEndBefore(
    nsINode& aNode, ErrorResult& aRv,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  // If the node is being removed from its parent, GetRawRangeBoundaryBefore()
  // returns unset instance.  Then, SetEnd() will throw
  // NS_ERROR_DOM_INVALID_NODE_TYPE_ERR.
  SetEnd(RangeUtils::GetRawRangeBoundaryBefore(&aNode), aRv,
         aAllowCrossShadowBoundary);
}

void nsRange::SetEndAfterJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SetEndAfter(aNode, aErr);
}

void nsRange::SetEndAfter(nsINode& aNode, ErrorResult& aRv) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  // If the node is being removed from its parent, GetRawRangeBoundaryAfter()
  // returns unset instance.  Then, SetEnd() will throw
  // NS_ERROR_DOM_INVALID_NODE_TYPE_ERR.
  SetEnd(RangeUtils::GetRawRangeBoundaryAfter(&aNode), aRv);
}

void nsRange::Collapse(bool aToStart) {
  if (!mIsPositioned) return;

  AutoInvalidateSelection atEndOfBlock(this);
  if (aToStart) {
    DoSetRange(mStart, mStart, mRoot);
  } else {
    DoSetRange(mEnd, mEnd, mRoot);
  }
}

void nsRange::CollapseJS(bool aToStart) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  Collapse(aToStart);
}

void nsRange::SelectNodeJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SelectNode(aNode, aErr);
}

void nsRange::SelectNode(nsINode& aNode, ErrorResult& aRv) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsINode* container = aNode.GetParentNode();
  nsINode* newRoot = RangeUtils::ComputeRootNode(container);
  if (!newRoot) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  const Maybe<uint32_t> index = container->ComputeIndexOf(&aNode);
  // MOZ_ASSERT(index.isSome());
  // We need to compute the index here unfortunately, because, while we have
  // support for XBL, |container| may be the node's binding parent without
  // actually containing it.
  if (MOZ_UNLIKELY(NS_WARN_IF(index.isNothing()))) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  DoSetRange(RawRangeBoundary{container, *index},
             RawRangeBoundary{container, *index + 1u}, newRoot);
}

void nsRange::SelectNodeContentsJS(nsINode& aNode, ErrorResult& aErr) {
  AutoCalledByJSRestore calledByJSRestorer(*this);
  mCalledByJS = true;
  SelectNodeContents(aNode, aErr);
}

void nsRange::SelectNodeContents(nsINode& aNode, ErrorResult& aRv) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsINode* newRoot = RangeUtils::ComputeRootNode(&aNode);
  if (!newRoot) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  AutoInvalidateSelection atEndOfBlock(this);
  DoSetRange(RawRangeBoundary(&aNode, 0u),
             RawRangeBoundary(&aNode, aNode.Length()), newRoot);
}

// The Subtree Content Iterator only returns subtrees that are
// completely within a given range. It doesn't return a CharacterData
// node that contains either the start or end point of the range.,
// nor does it return element nodes when nothing in the element is selected.
// We need an iterator that will also include these start/end points
// so that our methods/algorithms aren't cluttered with special
// case code that tries to include these points while iterating.
//
// The RangeSubtreeIterator class mimics the ContentSubtreeIterator
// methods we need, so should the Content Iterator support the
// start/end points in the future, we can switchover relatively
// easy.

class MOZ_STACK_CLASS RangeSubtreeIterator {
 private:
  enum RangeSubtreeIterState { eDone = 0, eUseStart, eUseIterator, eUseEnd };

  Maybe<ContentSubtreeIterator> mSubtreeIter;
  RangeSubtreeIterState mIterState;

  nsCOMPtr<nsINode> mStart;
  nsCOMPtr<nsINode> mEnd;

 public:
  RangeSubtreeIterator() : mIterState(eDone) {}
  ~RangeSubtreeIterator() = default;

  nsresult Init(nsRange* aRange, AllowRangeCrossShadowBoundary =
                                     AllowRangeCrossShadowBoundary::No);
  already_AddRefed<nsINode> GetCurrentNode();
  void First();
  void Last();
  void Next();
  void Prev();

  bool IsDone() { return mIterState == eDone; }
};

nsresult RangeSubtreeIterator::Init(
    nsRange* aRange, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
  mIterState = eDone;
  if (aRange->AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()) {
    return NS_OK;
  }

  // Grab the start point of the range and QI it to
  // a CharacterData pointer. If it is CharacterData store
  // a pointer to the node.

  if (!aRange->IsPositioned()) {
    return NS_ERROR_FAILURE;
  }

  nsINode* node = aRange->GetMayCrossShadowBoundaryStartContainer();
  if (NS_WARN_IF(!node)) {
    return NS_ERROR_FAILURE;
  }

  if (node->IsCharacterData() ||
      (node->IsElement() && node->AsElement()->GetChildCount() ==
                                aRange->MayCrossShadowBoundaryStartOffset())) {
    mStart = node;
  }

  // Grab the end point of the range and QI it to
  // a CharacterData pointer. If it is CharacterData store
  // a pointer to the node.

  node = aRange->GetMayCrossShadowBoundaryEndContainer();
  if (NS_WARN_IF(!node)) {
    return NS_ERROR_FAILURE;
  }

  if (node->IsCharacterData() ||
      (node->IsElement() && aRange->MayCrossShadowBoundaryEndOffset() == 0)) {
    mEnd = node;
  }

  if (mStart && mStart == mEnd) {
    // The range starts and stops in the same CharacterData
    // node. Null out the end pointer so we only visit the
    // node once!

    mEnd = nullptr;
  } else {
    // Now create a Content Subtree Iterator to be used
    // for the subtrees between the end points!

    mSubtreeIter.emplace();

    nsresult res =
        aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
            ? mSubtreeIter->InitWithAllowCrossShadowBoundary(aRange)
            : mSubtreeIter->Init(aRange);
    if (NS_FAILED(res)) return res;

    if (mSubtreeIter->IsDone()) {
      // The subtree iterator thinks there's nothing
      // to iterate over, so just free it up so we
      // don't accidentally call into it.

      mSubtreeIter.reset();
    }
  }

  // Initialize the iterator by calling First().
  // Note that we are ignoring the return value on purpose!

  First();

  return NS_OK;
}

already_AddRefed<nsINode> RangeSubtreeIterator::GetCurrentNode() {
  nsCOMPtr<nsINode> node;

  if (mIterState == eUseStart && mStart) {
    node = mStart;
  } else if (mIterState == eUseEnd && mEnd) {
    node = mEnd;
  } else if (mIterState == eUseIterator && mSubtreeIter) {
    node = mSubtreeIter->GetCurrentNode();
  }

  return node.forget();
}

void RangeSubtreeIterator::First() {
  if (mStart)
    mIterState = eUseStart;
  else if (mSubtreeIter) {
    mSubtreeIter->First();

    mIterState = eUseIterator;
  } else if (mEnd)
    mIterState = eUseEnd;
  else
    mIterState = eDone;
}

void RangeSubtreeIterator::Last() {
  if (mEnd)
    mIterState = eUseEnd;
  else if (mSubtreeIter) {
    mSubtreeIter->Last();

    mIterState = eUseIterator;
  } else if (mStart)
    mIterState = eUseStart;
  else
    mIterState = eDone;
}

void RangeSubtreeIterator::Next() {
  if (mIterState == eUseStart) {
    if (mSubtreeIter) {
      mSubtreeIter->First();

      mIterState = eUseIterator;
    } else if (mEnd)
      mIterState = eUseEnd;
    else
      mIterState = eDone;
  } else if (mIterState == eUseIterator) {
    mSubtreeIter->Next();

    if (mSubtreeIter->IsDone()) {
      if (mEnd)
        mIterState = eUseEnd;
      else
        mIterState = eDone;
    }
  } else
    mIterState = eDone;
}

void RangeSubtreeIterator::Prev() {
  if (mIterState == eUseEnd) {
    if (mSubtreeIter) {
      mSubtreeIter->Last();

      mIterState = eUseIterator;
    } else if (mStart)
      mIterState = eUseStart;
    else
      mIterState = eDone;
  } else if (mIterState == eUseIterator) {
    mSubtreeIter->Prev();

    if (mSubtreeIter->IsDone()) {
      if (mStart)
        mIterState = eUseStart;
      else
        mIterState = eDone;
    }
  } else
    mIterState = eDone;
}

// CollapseRangeAfterDelete() is a utility method that is used by
// DeleteContents() and ExtractContents() to collapse the range
// in the correct place, under the range's root container (the
// range end points common container) as outlined by the Range spec:
//
// http://www.w3.org/TR/2000/REC-DOM-Level-2-Traversal-Range-20001113/ranges.html
// The assumption made by this method is that the delete or extract
// has been done already, and left the range in a state where there is
// no content between the 2 end points.

static nsresult CollapseRangeAfterDelete(nsRange* aRange) {
  NS_ENSURE_ARG_POINTER(aRange);

  // Check if range gravity took care of collapsing the range for us!
  if (aRange->Collapsed()) {
    // aRange is collapsed so there's nothing for us to do.
    //
    // There are 2 possible scenarios here:
    //
    // 1. aRange could've been collapsed prior to the delete/extract,
    //    which would've resulted in nothing being removed, so aRange
    //    is already where it should be.
    //
    // 2. Prior to the delete/extract, aRange's start and end were in
    //    the same container which would mean everything between them
    //    was removed, causing range gravity to collapse the range.

    return NS_OK;
  }

  // aRange isn't collapsed so figure out the appropriate place to collapse!
  // First get both end points and their common ancestor.

  if (!aRange->IsPositioned()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsINode> commonAncestor =
      aRange->GetClosestCommonInclusiveAncestor();

  nsCOMPtr<nsINode> startContainer = aRange->GetStartContainer();
  nsCOMPtr<nsINode> endContainer = aRange->GetEndContainer();

  // Collapse to one of the end points if they are already in the
  // commonAncestor. This should work ok since this method is called
  // immediately after a delete or extract that leaves no content
  // between the 2 end points!

  if (startContainer == commonAncestor) {
    aRange->Collapse(true);
    return NS_OK;
  }
  if (endContainer == commonAncestor) {
    aRange->Collapse(false);
    return NS_OK;
  }

  // End points are at differing levels. We want to collapse to the
  // point that is between the 2 subtrees that contain each point,
  // under the common ancestor.

  nsCOMPtr<nsINode> nodeToSelect(startContainer);

  while (nodeToSelect) {
    nsCOMPtr<nsINode> parent = nodeToSelect->GetParentNode();
    if (parent == commonAncestor) break;  // We found the nodeToSelect!

    nodeToSelect = parent;
  }

  if (!nodeToSelect) return NS_ERROR_FAILURE;  // This should never happen!

  ErrorResult error;
  aRange->SelectNode(*nodeToSelect, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  aRange->Collapse(false);
  return NS_OK;
}

NS_IMETHODIMP
PrependChild(nsINode* aContainer, nsINode* aChild) {
  nsCOMPtr<nsINode> first = aContainer->GetFirstChild();
  ErrorResult rv;
  aContainer->InsertBefore(*aChild, first, rv);
  return rv.StealNSResult();
}

// Helper function for CutContents, making sure that the current node wasn't
// removed by mutation events (bug 766426)
static bool ValidateCurrentNode(nsRange* aRange, RangeSubtreeIterator& aIter) {
  bool before, after;
  nsCOMPtr<nsINode> node = aIter.GetCurrentNode();
  if (!node) {
    // We don't have to worry that the node was removed if it doesn't exist,
    // e.g., the iterator is done.
    return true;
  }

  nsresult rv = RangeUtils::CompareNodeToRange(node, aRange, &before, &after);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  if (before || after) {
    if (node->IsCharacterData()) {
      // If we're dealing with the start/end container which is a character
      // node, pretend that the node is in the range.
      if (before && node == aRange->GetStartContainer()) {
        before = false;
      }
      if (after && node == aRange->GetEndContainer()) {
        after = false;
      }
    }
  }

  return !before && !after;
}

void nsRange::CutContents(DocumentFragment** aFragment,
                          ElementHandler aElementHandler, ErrorResult& aRv) {
  if (aFragment && aElementHandler) {
    // Theoretically no reason it can't be handled, but not plumbed in enough to
    // test.
    MOZ_ASSERT_UNREACHABLE("Not handling both aFragment and aElementHandler");
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }
  if (aFragment) {
    *aFragment = nullptr;
  }

  if (!CanAccess(*GetMayCrossShadowBoundaryStartContainer()) ||
      !CanAccess(*GetMayCrossShadowBoundaryEndContainer())) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsCOMPtr<Document> doc = mStart.GetContainer()->OwnerDoc();

  nsCOMPtr<nsINode> commonAncestor = GetCommonAncestorContainer(
      aRv, StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
               ? AllowRangeCrossShadowBoundary::Yes
               : AllowRangeCrossShadowBoundary::No);
  if (aRv.Failed()) {
    return;
  }

  // If aFragment isn't null, create a temporary fragment to hold our return.
  RefPtr<DocumentFragment> retval;
  if (aFragment) {
    retval =
        new (doc->NodeInfoManager()) DocumentFragment(doc->NodeInfoManager());
  }
  nsCOMPtr<nsINode> commonCloneAncestor = retval.get();

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(mRoot ? mRoot->OwnerDoc() : nullptr, nullptr);

  // Save the range end points locally to avoid interference
  // of Range gravity during our edits!

  nsCOMPtr<nsINode> startContainer = GetMayCrossShadowBoundaryStartContainer();
  // `GetCommonAncestorContainer()` above ensures the range is positioned, hence
  // there have to be valid offsets.

  const uint32_t startOffset = *MayCrossShadowBoundaryStartRef().Offset(
      RangeBoundary::OffsetFilter::kValidOffsets);

  nsCOMPtr<nsINode> endContainer = GetMayCrossShadowBoundaryEndContainer();
  const uint32_t endOffset = *MayCrossShadowBoundaryEndRef().Offset(
      RangeBoundary::OffsetFilter::kValidOffsets);

  if (retval) {
    // For extractContents(), abort early if there's a doctype (bug 719533).
    // This can happen only if the common ancestor is a document, in which case
    // we just need to find its doctype child and check if that's in the range.
    nsCOMPtr<Document> commonAncestorDocument =
        do_QueryInterface(commonAncestor);
    if (commonAncestorDocument) {
      RefPtr<DocumentType> doctype = commonAncestorDocument->GetDoctype();

      // `GetCommonAncestorContainer()` above ensured the range is positioned.
      // Hence, start and end are both set and valid. If available, `doctype`
      // has a common ancestor with start and end, hence both have to be
      // comparable to it.
      if (doctype &&
          *nsContentUtils::ComparePointsWithIndices(startContainer, startOffset,
                                                    doctype, 0) < 0 &&
          *nsContentUtils::ComparePointsWithIndices(doctype, 0, endContainer,
                                                    endOffset) < 0) {
        aRv.ThrowHierarchyRequestError("Start or end position isn't valid.");
        return;
      }
    }
  }

  // Create and initialize a subtree iterator that will give
  // us all the subtrees within the range.

  RangeSubtreeIterator iter;

  aRv = iter.Init(this,
                  StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
                      ? AllowRangeCrossShadowBoundary::Yes
                      : AllowRangeCrossShadowBoundary::No);
  if (aRv.Failed()) {
    return;
  }

  if (iter.IsDone()) {
    // There's nothing for us to delete.
    aRv = CollapseRangeAfterDelete(this);
    if (!aRv.Failed() && aFragment) {
      retval.forget(aFragment);
    }
    return;
  }

  iter.First();

  bool handled = false;

  // With the exception of text nodes that contain one of the range
  // end points, the subtree iterator should only give us back subtrees
  // that are completely contained between the range's end points.

  while (!iter.IsDone()) {
    nsCOMPtr<nsINode> nodeToResult;
    nsCOMPtr<nsINode> node = iter.GetCurrentNode();

    // Before we delete anything, advance the iterator to the next node that's
    // not a descendant of this one.  XXX It's a bit silly to iterate through
    // the descendants only to throw them out, we should use an iterator that
    // skips the descendants to begin with.

    iter.Next();
    nsCOMPtr<nsINode> nextNode = iter.GetCurrentNode();
    while (nextNode && nextNode->IsInclusiveDescendantOf(node)) {
      iter.Next();
      nextNode = iter.GetCurrentNode();
    }

    handled = false;

    // If it's CharacterData, make sure we might need to delete
    // part of the data, instead of removing the whole node.
    //
    // XXX_kin: We need to also handle ProcessingInstruction
    // XXX_kin: according to the spec.

    if (auto charData = CharacterData::FromNode(node)) {
      uint32_t dataLength = 0;

      if (node == startContainer) {
        if (node == endContainer) {
          // This range is completely contained within a single text node.
          // Delete or extract the data between startOffset and endOffset.

          if (endOffset > startOffset) {
            if (retval) {
              nsAutoString cutValue;
              charData->SubstringData(startOffset, endOffset - startOffset,
                                      cutValue, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              nsCOMPtr<nsINode> clone = node->CloneNode(false, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              clone->SetNodeValueInternal(cutValue, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              nodeToResult = clone;
            }

            nsMutationGuard guard;
            charData->DeleteData(startOffset, endOffset - startOffset, aRv);
            if (NS_WARN_IF(aRv.Failed())) {
              return;
            }
            if (guard.Mutated(0) && !ValidateCurrentNode(this, iter)) {
              aRv.Throw(NS_ERROR_UNEXPECTED);
              return;
            }
          }

          handled = true;
        } else {
          // Delete or extract everything after startOffset.

          dataLength = charData->Length();

          if (dataLength >= startOffset) {
            if (retval) {
              nsAutoString cutValue;
              charData->SubstringData(startOffset, dataLength, cutValue, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              nsCOMPtr<nsINode> clone = node->CloneNode(false, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              clone->SetNodeValueInternal(cutValue, aRv);
              if (NS_WARN_IF(aRv.Failed())) {
                return;
              }
              nodeToResult = clone;
            }

            nsMutationGuard guard;
            charData->DeleteData(startOffset, dataLength, aRv);
            if (NS_WARN_IF(aRv.Failed())) {
              return;
            }
            if (guard.Mutated(0) && !ValidateCurrentNode(this, iter)) {
              aRv.Throw(NS_ERROR_UNEXPECTED);
              return;
            }
          }

          handled = true;
        }
      } else if (node == endContainer) {
        // Delete or extract everything before endOffset.
        if (retval) {
          nsAutoString cutValue;
          charData->SubstringData(0, endOffset, cutValue, aRv);
          if (NS_WARN_IF(aRv.Failed())) {
            return;
          }
          nsCOMPtr<nsINode> clone = node->CloneNode(false, aRv);
          if (NS_WARN_IF(aRv.Failed())) {
            return;
          }
          clone->SetNodeValueInternal(cutValue, aRv);
          if (NS_WARN_IF(aRv.Failed())) {
            return;
          }
          nodeToResult = clone;
        }

        nsMutationGuard guard;
        charData->DeleteData(0, endOffset, aRv);
        if (NS_WARN_IF(aRv.Failed())) {
          return;
        }
        if (guard.Mutated(0) && !ValidateCurrentNode(this, iter)) {
          aRv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }
        handled = true;
      }
    }

    if (!handled && (node == endContainer || node == startContainer)) {
      if (node && node->IsElement() &&
          ((node == endContainer && endOffset == 0) ||
           (node == startContainer &&
            node->AsElement()->GetChildCount() == startOffset))) {
        if (retval) {
          nodeToResult = node->CloneNode(false, aRv);
          if (aRv.Failed()) {
            return;
          }
        }
        handled = true;
      }
    }

    if (!handled) {
      // Node was not handled above, so it must be completely contained
      // within the range.
      if (aElementHandler && node->IsElement()) {
        // This is an element, and the caller specified a handler for it, so use
        // it.
        MOZ_ASSERT(!aFragment, "Fragment requested when ElementHandler given?");
        nsMutationGuard guard;
        auto* element = node->AsElement();
        aElementHandler(element);
        // No need to validate - we know this node is an element, so any case
        // that may cause the node to fail to validate is covered by the
        // mutation guard.
        if (guard.Mutated(0)) {
          aRv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }
        handled = true;
      } else {
        // Otherwise, just remove it from the tree.
        nodeToResult = node;
      }
    }

    uint32_t parentCount = 0;
    // Set the result to document fragment if we have 'retval'.
    if (retval) {
      nsCOMPtr<nsINode> oldCommonAncestor = commonAncestor;
      if (!iter.IsDone()) {
        // Setup the parameters for the next iteration of the loop.
        if (!nextNode) {
          aRv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }

        // Get node's and nextNode's common parent. Do this before moving
        // nodes from original DOM to result fragment.
        commonAncestor =
            nsContentUtils::GetClosestCommonInclusiveAncestor(node, nextNode);
        if (!commonAncestor) {
          aRv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }

        nsCOMPtr<nsINode> parentCounterNode = node;
        while (parentCounterNode && parentCounterNode != commonAncestor) {
          ++parentCount;
          parentCounterNode = parentCounterNode->GetParentNode();
          if (!parentCounterNode) {
            aRv.Throw(NS_ERROR_UNEXPECTED);
            return;
          }
        }
      }

      // Clone the parent hierarchy between commonAncestor and node.
      nsCOMPtr<nsINode> closestAncestor, farthestAncestor;
      aRv = CloneParentsBetween(oldCommonAncestor, node,
                                getter_AddRefs(closestAncestor),
                                getter_AddRefs(farthestAncestor));
      if (aRv.Failed()) {
        return;
      }

      if (farthestAncestor) {
        commonCloneAncestor->AppendChild(*farthestAncestor, aRv);
        if (NS_WARN_IF(aRv.Failed())) {
          return;
        }
      }

      nsMutationGuard guard;
      nsCOMPtr<nsINode> parent = nodeToResult->GetParentNode();
      if (closestAncestor) {
        closestAncestor->AppendChild(*nodeToResult, aRv);
      } else {
        commonCloneAncestor->AppendChild(*nodeToResult, aRv);
      }
      if (NS_WARN_IF(aRv.Failed())) {
        return;
      }
      if (guard.Mutated(parent ? 2 : 1) && !ValidateCurrentNode(this, iter)) {
        aRv.Throw(NS_ERROR_UNEXPECTED);
        return;
      }
    } else if (nodeToResult) {
      nsMutationGuard guard;
      nsCOMPtr<nsINode> node = nodeToResult;
      nsCOMPtr<nsINode> parent = node->GetParentNode();
      if (parent) {
        parent->RemoveChild(*node, aRv);
        if (aRv.Failed()) {
          return;
        }
      }
      if (guard.Mutated(1) && !ValidateCurrentNode(this, iter)) {
        aRv.Throw(NS_ERROR_UNEXPECTED);
        return;
      }
    }

    if (!iter.IsDone() && retval) {
      // Find the equivalent of commonAncestor in the cloned tree.
      nsCOMPtr<nsINode> newCloneAncestor = nodeToResult;
      for (uint32_t i = parentCount; i; --i) {
        newCloneAncestor = newCloneAncestor->GetParentNode();
        if (!newCloneAncestor) {
          aRv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }
      }
      commonCloneAncestor = newCloneAncestor;
    }
  }

  aRv = CollapseRangeAfterDelete(this);
  if (!aRv.Failed() && aFragment) {
    retval.forget(aFragment);
  }
}

void nsRange::DeleteContents(ErrorResult& aRv) {
  CutContents(nullptr, nullptr, aRv);
}

already_AddRefed<DocumentFragment> nsRange::ExtractContents(ErrorResult& rv) {
  RefPtr<DocumentFragment> fragment;
  CutContents(getter_AddRefs(fragment), nullptr, rv);
  return fragment.forget();
}

int16_t nsRange::CompareBoundaryPoints(uint16_t aHow,
                                       const nsRange& aOtherRange,
                                       ErrorResult& aRv) {
  if (!mIsPositioned || !aOtherRange.IsPositioned()) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return 0;
  }

  RawRangeBoundary ourBoundary, otherBoundary;
  switch (aHow) {
    case Range_Binding::START_TO_START:
      ourBoundary = mStart.AsRaw();
      otherBoundary = aOtherRange.StartRef().AsRaw();
      break;
    case Range_Binding::START_TO_END:
      ourBoundary = mEnd.AsRaw();
      otherBoundary = aOtherRange.StartRef().AsRaw();
      break;
    case Range_Binding::END_TO_START:
      ourBoundary = mStart.AsRaw();
      otherBoundary = aOtherRange.EndRef().AsRaw();
      break;
    case Range_Binding::END_TO_END:
      ourBoundary = mEnd.AsRaw();
      otherBoundary = aOtherRange.EndRef().AsRaw();
      break;
    default:
      // We were passed an illegal value
      aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return 0;
  }

  if (mRoot != aOtherRange.GetRoot()) {
    aRv.Throw(NS_ERROR_DOM_WRONG_DOCUMENT_ERR);
    return 0;
  }

  const Maybe<int32_t> order =
      nsContentUtils::ComparePoints(ourBoundary, otherBoundary);

  // `this` and `aOtherRange` share the same root and ourBoundary, otherBoundary
  // correspond to some of their boundaries. Hence, ourBoundary and
  // otherBoundary have to be comparable.
  return *order;
}

/* static */
nsresult nsRange::CloneParentsBetween(nsINode* aAncestor, nsINode* aNode,
                                      nsINode** aClosestAncestor,
                                      nsINode** aFarthestAncestor) {
  NS_ENSURE_ARG_POINTER(
      (aAncestor && aNode && aClosestAncestor && aFarthestAncestor));

  *aClosestAncestor = nullptr;
  *aFarthestAncestor = nullptr;

  if (aAncestor == aNode) return NS_OK;

  AutoTArray<nsCOMPtr<nsINode>, 16> parentStack;

  nsCOMPtr<nsINode> parent = aNode->GetParentNode();
  while (parent && parent != aAncestor) {
    parentStack.AppendElement(parent);
    parent = parent->GetParentNode();
  }

  nsCOMPtr<nsINode> firstParent;
  nsCOMPtr<nsINode> lastParent;
  for (int32_t i = parentStack.Length() - 1; i >= 0; i--) {
    ErrorResult rv;
    nsCOMPtr<nsINode> clone = parentStack[i]->CloneNode(false, rv);

    if (rv.Failed()) {
      return rv.StealNSResult();
    }
    if (!clone) {
      return NS_ERROR_FAILURE;
    }

    if (!lastParent) {
      lastParent = clone;
    } else {
      firstParent->AppendChild(*clone, rv);
      if (rv.Failed()) {
        return rv.StealNSResult();
      }
    }

    firstParent = clone;
  }

  firstParent.forget(aClosestAncestor);
  lastParent.forget(aFarthestAncestor);

  return NS_OK;
}

already_AddRefed<DocumentFragment> nsRange::CloneContents(ErrorResult& aRv) {
  nsCOMPtr<nsINode> commonAncestor = GetCommonAncestorContainer(aRv);
  MOZ_ASSERT(!aRv.Failed(), "GetCommonAncestorContainer() shouldn't fail!");

  nsCOMPtr<Document> doc = mStart.GetContainer()->OwnerDoc();
  NS_ASSERTION(doc, "CloneContents needs a document to continue.");
  if (!doc) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Create a new document fragment in the context of this document,
  // which might be null

  RefPtr<DocumentFragment> clonedFrag =
      new (doc->NodeInfoManager()) DocumentFragment(doc->NodeInfoManager());

  if (Collapsed()) {
    return clonedFrag.forget();
  }

  nsCOMPtr<nsINode> commonCloneAncestor = clonedFrag.get();

  // Create and initialize a subtree iterator that will give
  // us all the subtrees within the range.

  RangeSubtreeIterator iter;

  aRv = iter.Init(this);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (iter.IsDone()) {
    // There's nothing to add to the doc frag, we must be done!
    return clonedFrag.forget();
  }

  iter.First();

  // With the exception of text nodes that contain one of the range
  // end points and elements which don't have any content selected the subtree
  // iterator should only give us back subtrees that are completely contained
  // between the range's end points.
  //
  // Unfortunately these subtrees don't contain the parent hierarchy/context
  // that the Range spec requires us to return. This loop clones the
  // parent hierarchy, adds a cloned version of the subtree, to it, then
  // correctly places this new subtree into the doc fragment.

  while (!iter.IsDone()) {
    nsCOMPtr<nsINode> node = iter.GetCurrentNode();
    bool deepClone =
        !node->IsElement() ||
        (!(node == mEnd.GetContainer() &&
           *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets) == 0) &&
         !(node == mStart.GetContainer() &&
           *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets) ==
               node->AsElement()->GetChildCount()));

    // Clone the current subtree!

    nsCOMPtr<nsINode> clone = node->CloneNode(deepClone, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }

    // If it's CharacterData, make sure we only clone what
    // is in the range.
    //
    // XXX_kin: We need to also handle ProcessingInstruction
    // XXX_kin: according to the spec.

    if (auto charData = CharacterData::FromNode(clone)) {
      if (node == mEnd.GetContainer()) {
        // We only need the data before mEndOffset, so get rid of any
        // data after it.

        uint32_t dataLength = charData->Length();
        if (dataLength >
            *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets)) {
          charData->DeleteData(
              *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
              dataLength -
                  *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
              aRv);
          if (aRv.Failed()) {
            return nullptr;
          }
        }
      }

      if (node == mStart.GetContainer()) {
        // We don't need any data before mStartOffset, so just
        // delete it!

        if (*mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets) > 0) {
          charData->DeleteData(
              0, *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
              aRv);
          if (aRv.Failed()) {
            return nullptr;
          }
        }
      }
    }

    // Clone the parent hierarchy between commonAncestor and node.

    nsCOMPtr<nsINode> closestAncestor, farthestAncestor;

    aRv = CloneParentsBetween(commonAncestor, node,
                              getter_AddRefs(closestAncestor),
                              getter_AddRefs(farthestAncestor));

    if (aRv.Failed()) {
      return nullptr;
    }

    // Hook the parent hierarchy/context of the subtree into the clone tree.

    if (farthestAncestor) {
      commonCloneAncestor->AppendChild(*farthestAncestor, aRv);

      if (aRv.Failed()) {
        return nullptr;
      }
    }

    // Place the cloned subtree into the cloned doc frag tree!

    nsCOMPtr<nsINode> cloneNode = clone;
    if (closestAncestor) {
      // Append the subtree under closestAncestor since it is the
      // immediate parent of the subtree.

      closestAncestor->AppendChild(*cloneNode, aRv);
    } else {
      // If we get here, there is no missing parent hierarchy between
      // commonAncestor and node, so just append clone to commonCloneAncestor.

      commonCloneAncestor->AppendChild(*cloneNode, aRv);
    }
    if (aRv.Failed()) {
      return nullptr;
    }

    // Get the next subtree to be processed. The idea here is to setup
    // the parameters for the next iteration of the loop.

    iter.Next();

    if (iter.IsDone()) break;  // We must be done!

    nsCOMPtr<nsINode> nextNode = iter.GetCurrentNode();
    if (!nextNode) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    // Get node and nextNode's common parent.
    commonAncestor =
        nsContentUtils::GetClosestCommonInclusiveAncestor(node, nextNode);

    if (!commonAncestor) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    // Find the equivalent of commonAncestor in the cloned tree!

    while (node && node != commonAncestor) {
      node = node->GetParentNode();
      if (aRv.Failed()) {
        return nullptr;
      }

      if (!node) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      cloneNode = cloneNode->GetParentNode();
      if (!cloneNode) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }
    }

    commonCloneAncestor = cloneNode;
  }

  return clonedFrag.forget();
}

already_AddRefed<nsRange> nsRange::CloneRange() const {
  RefPtr<nsRange> range = nsRange::Create(mOwner);
  range->DoSetRange(mStart, mEnd, mRoot);
  if (mCrossShadowBoundaryRange) {
    range->CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
        mCrossShadowBoundaryRange->StartRef(),
        mCrossShadowBoundaryRange->EndRef());
  }
  return range.forget();
}

void nsRange::InsertNode(nsINode& aNode, ErrorResult& aRv) {
  if (!CanAccess(aNode)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (!IsPositioned()) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return;
  }

  uint32_t tStartOffset = StartOffset();

  nsCOMPtr<nsINode> tStartContainer = GetStartContainer();

  if (!CanAccess(*tStartContainer)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (&aNode == tStartContainer) {
    aRv.ThrowHierarchyRequestError(
        "The inserted node can not be range's start node.");
    return;
  }

  // This is the node we'll be inserting before, and its parent
  nsCOMPtr<nsINode> referenceNode;
  nsCOMPtr<nsINode> referenceParentNode = tStartContainer;

  RefPtr<Text> startTextNode = tStartContainer->GetAsText();
  nsCOMPtr<nsINodeList> tChildList;
  if (startTextNode) {
    referenceParentNode = tStartContainer->GetParentNode();
    if (!referenceParentNode) {
      aRv.ThrowHierarchyRequestError(
          "Can not get range's start node's parent.");
      return;
    }

    referenceParentNode->EnsurePreInsertionValidity(aNode, tStartContainer,
                                                    aRv);
    if (aRv.Failed()) {
      return;
    }

    RefPtr<Text> secondPart = startTextNode->SplitText(tStartOffset, aRv);
    if (aRv.Failed()) {
      return;
    }

    referenceNode = secondPart;
  } else {
    tChildList = tStartContainer->ChildNodes();

    // find the insertion point in the DOM and insert the Node
    referenceNode = tChildList->Item(tStartOffset);

    tStartContainer->EnsurePreInsertionValidity(aNode, referenceNode, aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  // We might need to update the end to include the new node (bug 433662).
  // Ideally we'd only do this if needed, but it's tricky to know when it's
  // needed in advance (bug 765799).
  uint32_t newOffset;

  if (referenceNode) {
    Maybe<uint32_t> indexInParent = referenceNode->ComputeIndexInParentNode();
    if (MOZ_UNLIKELY(NS_WARN_IF(indexInParent.isNothing()))) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }
    newOffset = *indexInParent;
  } else {
    newOffset = tChildList->Length();
  }

  if (aNode.NodeType() == nsINode::DOCUMENT_FRAGMENT_NODE) {
    newOffset += aNode.GetChildCount();
  } else {
    newOffset++;
  }

  // Now actually insert the node
  nsCOMPtr<nsINode> tResultNode;
  tResultNode = referenceParentNode->InsertBefore(aNode, referenceNode, aRv);
  if (aRv.Failed()) {
    return;
  }

  if (Collapsed()) {
    aRv = SetEnd(referenceParentNode, newOffset);
  }
}

void nsRange::SurroundContents(nsINode& aNewParent, ErrorResult& aRv) {
  if (!CanAccess(aNewParent)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (!mRoot) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  // INVALID_STATE_ERROR: Raised if the Range partially selects a non-text
  // node.
  if (mStart.GetContainer() != mEnd.GetContainer()) {
    bool startIsText = mStart.GetContainer()->IsText();
    bool endIsText = mEnd.GetContainer()->IsText();
    nsINode* startGrandParent = mStart.GetContainer()->GetParentNode();
    nsINode* endGrandParent = mEnd.GetContainer()->GetParentNode();
    if (!((startIsText && endIsText && startGrandParent &&
           startGrandParent == endGrandParent) ||
          (startIsText && startGrandParent &&
           startGrandParent == mEnd.GetContainer()) ||
          (endIsText && endGrandParent &&
           endGrandParent == mStart.GetContainer()))) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
  }

  // INVALID_NODE_TYPE_ERROR if aNewParent is something that can't be inserted
  // (Document, DocumentType, DocumentFragment)
  uint16_t nodeType = aNewParent.NodeType();
  if (nodeType == nsINode::DOCUMENT_NODE ||
      nodeType == nsINode::DOCUMENT_TYPE_NODE ||
      nodeType == nsINode::DOCUMENT_FRAGMENT_NODE) {
    aRv.Throw(NS_ERROR_DOM_INVALID_NODE_TYPE_ERR);
    return;
  }

  // Extract the contents within the range.

  RefPtr<DocumentFragment> docFrag = ExtractContents(aRv);

  if (aRv.Failed()) {
    return;
  }

  if (!docFrag) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Spec says we need to remove all of aNewParent's
  // children prior to insertion.

  nsCOMPtr<nsINodeList> children = aNewParent.ChildNodes();
  if (!children) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  uint32_t numChildren = children->Length();

  while (numChildren) {
    nsCOMPtr<nsINode> child = children->Item(--numChildren);
    if (!child) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    aNewParent.RemoveChild(*child, aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  // Insert aNewParent at the range's start point.

  InsertNode(aNewParent, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Append the content we extracted under aNewParent.
  aNewParent.AppendChild(*docFrag, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Select aNewParent, and its contents.

  SelectNode(aNewParent, aRv);
}

void nsRange::ToString(nsAString& aReturn, ErrorResult& aErr) {
  // clear the string
  aReturn.Truncate();

  // If we're unpositioned, return the empty string
  if (!mIsPositioned) {
    return;
  }

#ifdef DEBUG_range
  printf("Range dump: -----------------------\n");
#endif /* DEBUG */

  // effeciency hack for simple case
  if (mStart.GetContainer() == mEnd.GetContainer()) {
    Text* textNode =
        mStart.GetContainer() ? mStart.GetContainer()->GetAsText() : nullptr;

    if (textNode) {
#ifdef DEBUG_range
      // If debug, dump it:
      textNode->List(stdout);
      printf("End Range dump: -----------------------\n");
#endif /* DEBUG */

      // grab the text
      textNode->SubstringData(
          *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
          *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets) -
              *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
          aReturn, aErr);
      return;
    }
  }

  /* complex case: mStart.GetContainer() != mEnd.GetContainer(), or mStartParent
     not a text node revisit - there are potential optimizations here and also
     tradeoffs.
  */

  PostContentIterator postOrderIter;
  nsresult rv = postOrderIter.Init(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aErr.Throw(rv);
    return;
  }

  nsString tempString;

  // loop through the content iterator, which returns nodes in the range in
  // close tag order, and grab the text from any text node
  for (; !postOrderIter.IsDone(); postOrderIter.Next()) {
    nsINode* n = postOrderIter.GetCurrentNode();

#ifdef DEBUG_range
    // If debug, dump it:
    n->List(stdout);
#endif /* DEBUG */
    Text* textNode = n->GetAsText();
    if (textNode)  // if it's a text node, get the text
    {
      if (n == mStart.GetContainer()) {  // only include text past start offset
        uint32_t strLength = textNode->Length();
        textNode->SubstringData(
            *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
            strLength -
                *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
            tempString, IgnoreErrors());
        aReturn += tempString;
      } else if (n ==
                 mEnd.GetContainer()) {  // only include text before end offset
        textNode->SubstringData(
            0, *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
            tempString, IgnoreErrors());
        aReturn += tempString;
      } else {  // grab the whole kit-n-kaboodle
        textNode->GetData(tempString);
        aReturn += tempString;
      }
    }
  }

#ifdef DEBUG_range
  printf("End Range dump: -----------------------\n");
#endif /* DEBUG */
}

void nsRange::Detach() {}

already_AddRefed<DocumentFragment> nsRange::CreateContextualFragment(
    const nsAString& aFragment, ErrorResult& aRv) const {
  if (!mIsPositioned) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return nsContentUtils::CreateContextualFragment(mStart.GetContainer(),
                                                  aFragment, false, aRv);
}

already_AddRefed<DocumentFragment> nsRange::CreateContextualFragment(
    const TrustedHTMLOrString& aFragment, nsIPrincipal* aSubjectPrincipal,
    ErrorResult& aRv) const {
  if (!mIsPositioned) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  MOZ_ASSERT(mStart.GetContainer());

  constexpr nsLiteralString sink = u"Range createContextualFragment"_ns;
  Maybe<nsAutoString> compliantStringHolder;
  nsCOMPtr<nsINode> node = mStart.GetContainer();
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aFragment, sink, kTrustedTypesOnlySinkGroup, *node, aSubjectPrincipal,
          compliantStringHolder, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  return nsContentUtils::CreateContextualFragment(mStart.GetContainer(),
                                                  *compliantString, false, aRv);
}

static void ExtractRectFromOffset(nsIFrame* aFrame, const int32_t aOffset,
                                  nsRect* aR, bool aFlushToOriginEdge,
                                  bool aClampToEdge) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aR);

  nsPoint point;
  aFrame->GetPointFromOffset(aOffset, &point);

  // Determine if aFrame has a vertical writing mode, which will change our math
  // on the output rect.
  bool isVertical = aFrame->GetWritingMode().IsVertical();

  if (!aClampToEdge && !aR->Contains(point)) {
    // If point is outside aR, and we aren't clamping, output an empty rect
    // with origin at the point.
    if (isVertical) {
      aR->SetHeight(0);
      aR->y = point.y;
    } else {
      aR->SetWidth(0);
      aR->x = point.x;
    }
    return;
  }

  if (aClampToEdge) {
    point = aR->ClampPoint(point);
  }

  // point is within aR, and now we'll modify aR to output a rect that has point
  // on one edge. But which edge?
  if (aFlushToOriginEdge) {
    // The output rect should be flush to the edge of aR that contains the
    // origin.
    if (isVertical) {
      aR->SetHeight(point.y - aR->y);
    } else {
      aR->SetWidth(point.x - aR->x);
    }
  } else {
    // The output rect should be flush to the edge of aR opposite the origin.
    if (isVertical) {
      aR->SetHeight(aR->YMost() - point.y);
      aR->y = point.y;
    } else {
      aR->SetWidth(aR->XMost() - point.x);
      aR->x = point.x;
    }
  }
}

static nsTextFrame* GetTextFrameForContent(nsIContent* aContent,
                                           bool aFlushLayout) {
  RefPtr<Document> doc = aContent->OwnerDoc();
  PresShell* presShell = doc->GetPresShell();
  if (!presShell) {
    return nullptr;
  }

  // Try to un-suppress whitespace if needed, but only if we'll be able to flush
  // to immediately see the results of the un-suppression. If we can't flush
  // here, then calling EnsureFrameForTextNodeIsCreatedAfterFlush would be
  // pointless anyway.
  if (aFlushLayout) {
    const bool frameWillBeUnsuppressed =
        presShell->FrameConstructor()
            ->EnsureFrameForTextNodeIsCreatedAfterFlush(
                static_cast<CharacterData*>(aContent));
    if (frameWillBeUnsuppressed) {
      doc->FlushPendingNotifications(FlushType::Layout);
    }
  }

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame || !frame->IsTextFrame()) {
    return nullptr;
  }
  return static_cast<nsTextFrame*>(frame);
}

static nsresult GetPartialTextRect(RectCallback* aCallback,
                                   Sequence<nsString>* aTextList,
                                   nsIContent* aContent, int32_t aStartOffset,
                                   int32_t aEndOffset, bool aClampToEdge,
                                   bool aFlushLayout) {
  nsTextFrame* textFrame = GetTextFrameForContent(aContent, aFlushLayout);
  if (textFrame) {
    nsIFrame* relativeTo =
        nsLayoutUtils::GetContainingBlockForClientRect(textFrame);

    for (nsTextFrame* f = textFrame->FindContinuationForOffset(aStartOffset); f;
         f = static_cast<nsTextFrame*>(f->GetNextContinuation())) {
      int32_t fstart = f->GetContentOffset(), fend = f->GetContentEnd();
      if (fend <= aStartOffset) {
        continue;
      }
      if (fstart >= aEndOffset) {
        break;
      }

      // Calculate the text content offsets we'll need if text is requested.
      int32_t textContentStart = fstart;
      int32_t textContentEnd = fend;

      // overlapping with the offset we want
      f->EnsureTextRun(nsTextFrame::eInflated);
      NS_ENSURE_TRUE(f->GetTextRun(nsTextFrame::eInflated),
                     NS_ERROR_OUT_OF_MEMORY);
      bool topLeftToBottomRight =
          !f->GetTextRun(nsTextFrame::eInflated)->IsInlineReversed();
      nsRect r = f->GetRectRelativeToSelf();
      if (fstart < aStartOffset) {
        // aStartOffset is within this frame
        ExtractRectFromOffset(f, aStartOffset, &r, !topLeftToBottomRight,
                              aClampToEdge);
        textContentStart = aStartOffset;
      }
      if (fend > aEndOffset) {
        // aEndOffset is in the middle of this frame
        ExtractRectFromOffset(f, aEndOffset, &r, topLeftToBottomRight,
                              aClampToEdge);
        textContentEnd = aEndOffset;
      }
      r = nsLayoutUtils::TransformFrameRectToAncestor(f, r, relativeTo);
      aCallback->AddRect(r);

      // Finally capture the text, if requested.
      if (aTextList) {
        nsIFrame::RenderedText renderedText =
            f->GetRenderedText(textContentStart, textContentEnd,
                               nsIFrame::TextOffsetType::OffsetsInContentText,
                               nsIFrame::TrailingWhitespace::DontTrim);

        NS_ENSURE_TRUE(aTextList->AppendElement(renderedText.mString, fallible),
                       NS_ERROR_OUT_OF_MEMORY);
      }
    }
  }
  return NS_OK;
}

static void CollectClientRectsForSubtree(
    nsINode* aNode, RectCallback* aCollector, Sequence<nsString>* aTextList,
    nsINode* aStartContainer, uint32_t aStartOffset, nsINode* aEndContainer,
    uint32_t aEndOffset, bool aClampToEdge, bool aFlushLayout, bool aTextOnly) {
  auto* content = nsIContent::FromNode(aNode);
  if (!content) {
    return;
  }

  const bool isText = content->IsText();
  if (isText) {
    if (aNode == aStartContainer) {
      int32_t offset = aStartContainer == aEndContainer
                           ? static_cast<int32_t>(aEndOffset)
                           : content->AsText()->TextDataLength();
      GetPartialTextRect(aCollector, aTextList, content,
                         static_cast<int32_t>(aStartOffset), offset,
                         aClampToEdge, aFlushLayout);
      return;
    }

    if (aNode == aEndContainer) {
      GetPartialTextRect(aCollector, aTextList, content, 0,
                         static_cast<int32_t>(aEndOffset), aClampToEdge,
                         aFlushLayout);
      return;
    }
  }

  if (nsIFrame* frame = content->GetPrimaryFrame()) {
    if (!aTextOnly || isText) {
      nsLayoutUtils::GetAllInFlowRectsAndTexts(
          frame, nsLayoutUtils::GetContainingBlockForClientRect(frame),
          aCollector, aTextList,
          nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);
      if (isText) {
        return;
      }
      aTextOnly = true;
      // We just get the text when calling GetAllInFlowRectsAndTexts, so we
      // don't need to call it again when visiting the children.
      aTextList = nullptr;
    }
  } else if (!content->IsElement() ||
             !content->AsElement()->IsDisplayContents()) {
    return;
  }

  FlattenedChildIterator childIter(content);
  for (nsIContent* child = childIter.GetNextChild(); child;
       child = childIter.GetNextChild()) {
    CollectClientRectsForSubtree(child, aCollector, aTextList, aStartContainer,
                                 aStartOffset, aEndContainer, aEndOffset,
                                 aClampToEdge, aFlushLayout, aTextOnly);
  }
}

/* static */
void nsRange::CollectClientRectsAndText(
    RectCallback* aCollector, Sequence<nsString>* aTextList, nsRange* aRange,
    nsINode* aStartContainer, uint32_t aStartOffset, nsINode* aEndContainer,
    uint32_t aEndOffset, bool aClampToEdge, bool aFlushLayout) {
  // Currently, this method is called with start of end offset of nsRange.
  // So, they must be between 0 - INT32_MAX.
  MOZ_ASSERT(RangeUtils::IsValidOffset(aStartOffset));
  MOZ_ASSERT(RangeUtils::IsValidOffset(aEndOffset));

  // Hold strong pointers across the flush
  nsCOMPtr<nsINode> startContainer = aStartContainer;
  nsCOMPtr<nsINode> endContainer = aEndContainer;

  // Flush out layout so our frames are up to date.
  if (!aStartContainer->IsInComposedDoc()) {
    return;
  }

  if (aFlushLayout) {
    aStartContainer->OwnerDoc()->FlushPendingNotifications(FlushType::Layout);
    // Recheck whether we're still in the document
    if (!aStartContainer->IsInComposedDoc()) {
      return;
    }
  }

  RangeSubtreeIterator iter;

  nsresult rv = iter.Init(aRange);
  if (NS_FAILED(rv)) return;

  if (iter.IsDone()) {
    // the range is collapsed, only continue if the cursor is in a text node
    if (aStartContainer->IsText()) {
      nsTextFrame* textFrame =
          GetTextFrameForContent(aStartContainer->AsText(), aFlushLayout);
      if (textFrame) {
        int32_t outOffset;
        nsIFrame* outFrame;
        textFrame->GetChildFrameContainingOffset(
            static_cast<int32_t>(aStartOffset), false, &outOffset, &outFrame);
        if (outFrame) {
          nsIFrame* relativeTo =
              nsLayoutUtils::GetContainingBlockForClientRect(outFrame);
          nsRect r = outFrame->GetRectRelativeToSelf();
          ExtractRectFromOffset(outFrame, static_cast<int32_t>(aStartOffset),
                                &r, false, aClampToEdge);
          r.SetWidth(0);
          r = nsLayoutUtils::TransformFrameRectToAncestor(outFrame, r,
                                                          relativeTo);
          aCollector->AddRect(r);
        }
      }
    }
    return;
  }

  do {
    nsCOMPtr<nsINode> node = iter.GetCurrentNode();
    iter.Next();

    CollectClientRectsForSubtree(node, aCollector, aTextList, aStartContainer,
                                 aStartOffset, aEndContainer, aEndOffset,
                                 aClampToEdge, aFlushLayout, false);
  } while (!iter.IsDone());
}

already_AddRefed<DOMRect> nsRange::GetBoundingClientRect(bool aClampToEdge,
                                                         bool aFlushLayout) {
  RefPtr<DOMRect> rect = new DOMRect(ToSupports(mOwner));
  if (!mIsPositioned) {
    return rect.forget();
  }

  nsLayoutUtils::RectAccumulator accumulator;
  CollectClientRectsAndText(
      &accumulator, nullptr, this, mStart.GetContainer(),
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      mEnd.GetContainer(),
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets), aClampToEdge,
      aFlushLayout);

  nsRect r = accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
                                               : accumulator.mResultRect;
  rect->SetLayoutRect(r);
  return rect.forget();
}

already_AddRefed<DOMRectList> nsRange::GetClientRects(bool aClampToEdge,
                                                      bool aFlushLayout) {
  return GetClientRectsInner(AllowRangeCrossShadowBoundary::No, aClampToEdge,
                             aFlushLayout);
}

already_AddRefed<DOMRectList> nsRange::GetAllowCrossShadowBoundaryClientRects(
    bool aClampToEdge, bool aFlushLayout) {
  return GetClientRectsInner(AllowRangeCrossShadowBoundary::Yes, aClampToEdge,
                             aFlushLayout);
}

already_AddRefed<DOMRectList> nsRange::GetClientRectsInner(
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundaryRange,
    bool aClampToEdge, bool aFlushLayout) {
  if (!mIsPositioned) {
    return nullptr;
  }

  RefPtr<DOMRectList> rectList = new DOMRectList(ToSupports(mOwner));

  nsLayoutUtils::RectListBuilder builder(rectList);

  const auto& startRef =
      aAllowCrossShadowBoundaryRange == AllowRangeCrossShadowBoundary::Yes
          ? MayCrossShadowBoundaryStartRef()
          : mStart;
  const auto& endRef =
      aAllowCrossShadowBoundaryRange == AllowRangeCrossShadowBoundary::Yes
          ? MayCrossShadowBoundaryEndRef()
          : mEnd;

  CollectClientRectsAndText(
      &builder, nullptr, this, startRef.GetContainer(),
      *startRef.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      endRef.GetContainer(),
      *endRef.Offset(RangeBoundary::OffsetFilter::kValidOffsets), aClampToEdge,
      aFlushLayout);
  return rectList.forget();
}

void nsRange::GetClientRectsAndTexts(mozilla::dom::ClientRectsAndTexts& aResult,
                                     ErrorResult& aErr) {
  if (!mIsPositioned) {
    return;
  }

  aResult.mRectList = new DOMRectList(ToSupports(mOwner));

  nsLayoutUtils::RectListBuilder builder(aResult.mRectList);

  CollectClientRectsAndText(
      &builder, &aResult.mTextList, this, mStart.GetContainer(),
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      mEnd.GetContainer(),
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets), true, true);
}

nsresult nsRange::GetUsedFontFaces(nsLayoutUtils::UsedFontFaceList& aResult,
                                   uint32_t aMaxRanges,
                                   bool aSkipCollapsedWhitespace) {
  NS_ENSURE_TRUE(mIsPositioned, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsINode> startContainer = mStart.GetContainer();
  nsCOMPtr<nsINode> endContainer = mEnd.GetContainer();

  // Flush out layout so our frames are up to date.
  Document* doc = mStart.GetContainer()->OwnerDoc();
  NS_ENSURE_TRUE(doc, NS_ERROR_UNEXPECTED);
  doc->FlushPendingNotifications(FlushType::Frames);

  // Recheck whether we're still in the document
  NS_ENSURE_TRUE(mStart.IsSetAndInComposedDoc(), NS_ERROR_UNEXPECTED);

  // A table to map gfxFontEntry objects to InspectorFontFace objects.
  // This table does NOT own the InspectorFontFace objects, it only holds
  // raw pointers to them. They are owned by the aResult array.
  nsLayoutUtils::UsedFontFaceTable fontFaces;

  RangeSubtreeIterator iter;
  nsresult rv = iter.Init(this);
  NS_ENSURE_SUCCESS(rv, rv);

  while (!iter.IsDone()) {
    // only collect anything if the range is not collapsed
    nsCOMPtr<nsINode> node = iter.GetCurrentNode();
    iter.Next();

    nsCOMPtr<nsIContent> content = do_QueryInterface(node);
    if (!content) {
      continue;
    }
    nsIFrame* frame = content->GetPrimaryFrame();
    if (!frame) {
      continue;
    }

    if (content->IsText()) {
      if (node == startContainer) {
        int32_t offset =
            startContainer == endContainer
                ? *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets)
                : content->AsText()->TextDataLength();
        nsLayoutUtils::GetFontFacesForText(
            frame, *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
            offset, true, aResult, fontFaces, aMaxRanges,
            aSkipCollapsedWhitespace);
        continue;
      }
      if (node == endContainer) {
        nsLayoutUtils::GetFontFacesForText(
            frame, 0, *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
            true, aResult, fontFaces, aMaxRanges, aSkipCollapsedWhitespace);
        continue;
      }
    }

    nsLayoutUtils::GetFontFacesForFrames(frame, aResult, fontFaces, aMaxRanges,
                                         aSkipCollapsedWhitespace);
  }

  return NS_OK;
}

nsINode* nsRange::GetRegisteredClosestCommonInclusiveAncestor() {
  MOZ_ASSERT(IsInAnySelection(),
             "GetRegisteredClosestCommonInclusiveAncestor only valid for range "
             "in selection");
  MOZ_ASSERT(mRegisteredClosestCommonInclusiveAncestor);
  return mRegisteredClosestCommonInclusiveAncestor;
}

void nsRange::SuppressContentsForPrintSelection(ErrorResult& aRv) {
  CutContents(
      nullptr,
      [](Element* aElement) {
        // Elements need to be left as-is when we're deleting nodes for
        // printing, to preserve the style matches containing tree-structural
        // pseudo-classes, such as :first-child. Partial texts are still deleted
        // since we don't have a good way to suppress partial texts, but that'd
        // preserve e.g. ::first-letter.
        aElement->AddStates(ElementState::SUPPRESS_FOR_PRINT_SELECTION);
      },
      aRv);
}

/* static */
bool nsRange::AutoInvalidateSelection::sIsNested;

nsRange::AutoInvalidateSelection::~AutoInvalidateSelection() {
  if (!mCommonAncestor) {
    return;
  }
  sIsNested = false;
  ::InvalidateAllFrames(mCommonAncestor);

  // Our range might not be in a selection anymore, because one of our selection
  // listeners might have gone ahead and run script of various sorts that messed
  // with selections, ranges, etc.  But if it still is, we should check whether
  // we have a different common ancestor now, and if so invalidate its subtree
  // so it paints the selection it's in now.
  if (mRange->IsInAnySelection()) {
    nsINode* commonAncestor =
        mRange->GetRegisteredClosestCommonInclusiveAncestor();
    // XXXbz can commonAncestor really be null here?  I wouldn't think so!  If
    // it _were_, then in a debug build
    // GetRegisteredClosestCommonInclusiveAncestor() would have fatally
    // asserted.
    if (commonAncestor && commonAncestor != mCommonAncestor) {
      ::InvalidateAllFrames(commonAncestor);
    }
  }
}

/* static */
already_AddRefed<nsRange> nsRange::Constructor(const GlobalObject& aGlobal,
                                               ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!window || !window->GetDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return window->GetDoc()->CreateRange(aRv);
}

static bool ExcludeIfNextToNonSelectable(nsIContent* aContent) {
  return aContent->IsText() &&
         aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE);
}

void nsRange::ExcludeNonSelectableNodes(nsTArray<RefPtr<nsRange>>* aOutRanges) {
  if (!mIsPositioned) {
    MOZ_ASSERT(false);
    return;
  }
  MOZ_ASSERT(mEnd.GetContainer());
  MOZ_ASSERT(mStart.GetContainer());

  nsRange* range = this;
  RefPtr<nsRange> newRange;
  while (range) {
    PreContentIterator preOrderIter;
    nsresult rv = preOrderIter.Init(range);
    if (NS_FAILED(rv)) {
      return;
    }

    bool added = false;
    bool seenSelectable = false;
    // |firstNonSelectableContent| is the first node in a consecutive sequence
    // of non-IsSelectable nodes.  When we find a selectable node after such
    // a sequence we'll end the last nsRange, create a new one and restart
    // the outer loop.
    nsIContent* firstNonSelectableContent = nullptr;
    while (true) {
      nsINode* node = preOrderIter.GetCurrentNode();
      preOrderIter.Next();
      bool selectable = true;
      nsIContent* content = nsIContent::FromNodeOrNull(node);
      if (content) {
        if (firstNonSelectableContent &&
            ExcludeIfNextToNonSelectable(content)) {
          // Ignorable whitespace next to a sequence of non-selectable nodes
          // counts as non-selectable (bug 1216001).
          selectable = false;
        }
        if (selectable) {
          nsIFrame* frame = content->GetPrimaryFrame();
          for (nsIContent* p = content; !frame && (p = p->GetParent());) {
            frame = p->GetPrimaryFrame();
          }
          if (frame) {
            selectable = frame->IsSelectable(nullptr);
          }
        }
      }

      if (!selectable) {
        if (!firstNonSelectableContent) {
          firstNonSelectableContent = content;
        }
        if (preOrderIter.IsDone()) {
          if (seenSelectable) {
            // The tail end of the initial range is non-selectable - truncate
            // the current range before the first non-selectable node.
            range->SetEndBefore(*firstNonSelectableContent, IgnoreErrors());
          }
          return;
        }
        continue;
      }

      if (firstNonSelectableContent) {
        if (range == this && !seenSelectable) {
          // This is the initial range and all its nodes until now are
          // non-selectable so just trim them from the start.
          IgnoredErrorResult err;
          range->SetStartBefore(*node, err, AllowRangeCrossShadowBoundary::Yes);
          if (err.Failed()) {
            return;
          }
          break;  // restart the same range with a new iterator
        }

        // Save the end point before truncating the range.
        nsINode* endContainer = range->mEnd.GetContainer();
        const uint32_t endOffset =
            *range->mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets);

        // Truncate the current range before the first non-selectable node.
        IgnoredErrorResult err;
        range->SetEndBefore(*firstNonSelectableContent, err,
                            AllowRangeCrossShadowBoundary::Yes);

        // Store it in the result (strong ref) - do this before creating
        // a new range in |newRange| below so we don't drop the last ref
        // to the range created in the previous iteration.
        if (!added && !err.Failed()) {
          aOutRanges->AppendElement(range);
        }

        // Create a new range for the remainder.
        nsINode* startContainer = node;
        Maybe<uint32_t> startOffset = Some(0);
        // Don't start *inside* a node with independent selection though
        // (e.g. <input>).
        if (content && content->HasIndependentSelection()) {
          nsINode* parent = startContainer->GetParent();
          if (parent) {
            startOffset = parent->ComputeIndexOf(startContainer);
            startContainer = parent;
          }
        }
        newRange =
            nsRange::Create(startContainer, startOffset.valueOr(UINT32_MAX),
                            endContainer, endOffset, IgnoreErrors());
        if (!newRange || newRange->Collapsed()) {
          newRange = nullptr;
        }
        range = newRange;
        break;  // create a new iterator for the new range, if any
      }

      seenSelectable = true;
      if (!added) {
        added = true;
        aOutRanges->AppendElement(range);
      }
      if (preOrderIter.IsDone()) {
        return;
      }
    }
  }
}

struct InnerTextAccumulator {
  explicit InnerTextAccumulator(mozilla::dom::DOMString& aValue)
      : mString(aValue.AsAString()), mRequiredLineBreakCount(0) {}
  void FlushLineBreaks() {
    while (mRequiredLineBreakCount > 0) {
      // Required line breaks at the start of the text are suppressed.
      if (!mString.IsEmpty()) {
        mString.Append('\n');
      }
      --mRequiredLineBreakCount;
    }
  }
  void Append(char aCh) { Append(nsAutoString(aCh)); }
  void Append(const nsAString& aString) {
    if (aString.IsEmpty()) {
      return;
    }
    FlushLineBreaks();
    mString.Append(aString);
  }
  void AddRequiredLineBreakCount(int8_t aCount) {
    mRequiredLineBreakCount = std::max(mRequiredLineBreakCount, aCount);
  }

  nsAString& mString;
  int8_t mRequiredLineBreakCount;
};

static bool IsVisibleAndNotInReplacedElement(nsIFrame* aFrame) {
  if (!aFrame || !aFrame->StyleVisibility()->IsVisible() ||
      aFrame->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    return false;
  }
  if (aFrame->HidesContent()) {
    return false;
  }
  for (nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    if (f->HidesContent()) {
      return false;
    }
    if (f->IsReplaced() &&
        !f->GetContent()->IsAnyOfHTMLElements(nsGkAtoms::button,
                                              nsGkAtoms::select) &&
        !f->GetContent()->IsSVGElement()) {
      return false;
    }
  }
  return true;
}

static void AppendTransformedText(InnerTextAccumulator& aResult,
                                  nsIContent* aContainer) {
  auto textNode = static_cast<CharacterData*>(aContainer);

  nsIFrame* frame = textNode->GetPrimaryFrame();
  if (!IsVisibleAndNotInReplacedElement(frame)) {
    return;
  }

  nsIFrame::RenderedText text =
      frame->GetRenderedText(0, aContainer->GetChildCount());
  aResult.Append(text.mString);
}

/**
 * States for tree traversal. AT_NODE means that we are about to enter
 * the current DOM node. AFTER_NODE means that we have just finished traversing
 * the children of the current DOM node and are about to apply any
 * "after processing the node's children" steps before we finish visiting
 * the node.
 */
enum TreeTraversalState { AT_NODE, AFTER_NODE };

static int8_t GetRequiredInnerTextLineBreakCount(nsIFrame* aFrame) {
  if (aFrame->GetContent()->IsHTMLElement(nsGkAtoms::p)) {
    return 2;
  }
  const nsStyleDisplay* styleDisplay = aFrame->StyleDisplay();
  if (styleDisplay->IsBlockOutside(aFrame) ||
      styleDisplay->mDisplay == StyleDisplay::TableCaption) {
    return 1;
  }
  return 0;
}

static bool IsLastCellOfRow(nsIFrame* aFrame) {
  LayoutFrameType type = aFrame->Type();
  if (type != LayoutFrameType::TableCell) {
    return true;
  }
  for (nsIFrame* c = aFrame; c; c = c->GetNextContinuation()) {
    if (c->GetNextSibling()) {
      return false;
    }
  }
  return true;
}

static bool IsLastRowOfRowGroup(nsIFrame* aFrame) {
  if (!aFrame->IsTableRowFrame()) {
    return true;
  }
  for (nsIFrame* c = aFrame; c; c = c->GetNextContinuation()) {
    if (c->GetNextSibling()) {
      return false;
    }
  }
  return true;
}

static bool IsLastNonemptyRowGroupOfTable(nsIFrame* aFrame) {
  if (!aFrame->IsTableRowGroupFrame()) {
    return true;
  }
  for (nsIFrame* c = aFrame; c; c = c->GetNextContinuation()) {
    for (nsIFrame* next = c->GetNextSibling(); next;
         next = next->GetNextSibling()) {
      if (next->PrincipalChildList().FirstChild()) {
        return false;
      }
    }
  }
  return true;
}

void nsRange::GetInnerTextNoFlush(DOMString& aValue, ErrorResult& aError,
                                  nsIContent* aContainer) {
  InnerTextAccumulator result(aValue);

  if (aContainer->IsText()) {
    AppendTransformedText(result, aContainer);
    return;
  }

  nsIContent* currentNode = aContainer;
  TreeTraversalState currentState = AFTER_NODE;

  nsIContent* endNode = aContainer;
  TreeTraversalState endState = AFTER_NODE;

  nsIContent* firstChild = aContainer->GetFirstChild();
  if (firstChild) {
    currentNode = firstChild;
    currentState = AT_NODE;
  }

  while (currentNode != endNode || currentState != endState) {
    nsIFrame* f = currentNode->GetPrimaryFrame();
    bool isVisibleAndNotReplaced = IsVisibleAndNotInReplacedElement(f);
    if (currentState == AT_NODE) {
      bool isText = currentNode->IsText();
      if (isVisibleAndNotReplaced) {
        result.AddRequiredLineBreakCount(GetRequiredInnerTextLineBreakCount(f));
        if (isText) {
          nsIFrame::RenderedText text = f->GetRenderedText();
          result.Append(text.mString);
        }
      }
      nsIContent* child = currentNode->GetFirstChild();
      if (child) {
        currentNode = child;
        continue;
      }
      currentState = AFTER_NODE;
    }
    if (currentNode == endNode && currentState == endState) {
      break;
    }
    if (isVisibleAndNotReplaced) {
      if (currentNode->IsHTMLElement(nsGkAtoms::br)) {
        result.Append('\n');
      }
      switch (f->StyleDisplay()->DisplayInside()) {
        case StyleDisplayInside::TableCell:
          if (!IsLastCellOfRow(f)) {
            result.Append('\t');
          }
          break;
        case StyleDisplayInside::TableRow:
          if (!IsLastRowOfRowGroup(f) ||
              !IsLastNonemptyRowGroupOfTable(f->GetParent())) {
            result.Append('\n');
          }
          break;
        default:
          break;  // Do nothing
      }
      result.AddRequiredLineBreakCount(GetRequiredInnerTextLineBreakCount(f));
    }
    nsIContent* next = currentNode->GetNextSibling();
    if (next) {
      currentNode = next;
      currentState = AT_NODE;
    } else {
      currentNode = currentNode->GetParent();
    }
  }

  // Do not flush trailing line breaks! Required breaks at the end of the text
  // are suppressed.
}

template <typename SPT, typename SRT, typename EPT, typename ERT>
void nsRange::CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
    const mozilla::RangeBoundaryBase<SPT, SRT>& aStartBoundary,
    const mozilla::RangeBoundaryBase<EPT, ERT>& aEndBoundary) {
  if (!StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()) {
    return;
  }

  MOZ_ASSERT(aStartBoundary.IsSetAndValid() && aEndBoundary.IsSetAndValid());
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == aEndBoundary.GetTreeKind());
  MOZ_ASSERT(aStartBoundary.GetTreeKind() == TreeKind::Flat);

  nsINode* startNode = aStartBoundary.GetContainer();
  nsINode* endNode = aEndBoundary.GetContainer();

  if (!startNode && !endNode) {
    ResetCrossShadowBoundaryRange();
    return;
  }

  // Nodes at least needs to be in the same document.
  if (startNode && endNode &&
      startNode->GetComposedDoc() != endNode->GetComposedDoc()) {
    return;
  }

  auto CanBecomeCrossShadowBoundaryPoint = [](nsINode* aContainer) -> bool {
    if (!aContainer) {
      return true;
    }

    // Unlike normal ranges, shadow cross ranges don't work
    // when the nodes aren't in document.
    if (!aContainer->IsInComposedDoc()) {
      return false;
    }

    // AbstractRange::GetClosestCommonInclusiveAncestor only supports
    // Document and Content nodes.
    return aContainer->IsDocument() || aContainer->IsContent();
  };

  if (!CanBecomeCrossShadowBoundaryPoint(startNode) ||
      !CanBecomeCrossShadowBoundaryPoint(endNode)) {
    ResetCrossShadowBoundaryRange();
    return;
  }

  if (!mCrossShadowBoundaryRange) {
    mCrossShadowBoundaryRange =
        CrossShadowBoundaryRange::Create(aStartBoundary, aEndBoundary, this);
    return;
  }

  mCrossShadowBoundaryRange->SetStartAndEnd(aStartBoundary, aEndBoundary);
}

RawRangeBoundary nsRange::ComputeNewBoundaryWhenBoundaryInsideChangedText(
    const CharacterDataChangeInfo& aInfo, const RawRangeBoundary& aBoundary) {
  MOZ_ASSERT(aInfo.mChangeStart <
             *aBoundary.Offset(
                 RawRangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
  // If boundary is inside changed text, position it before change
  // else adjust start offset for the change in length.
  CheckedUint32 newOffset{0};
  if (*aBoundary.Offset(
          RawRangeBoundary::OffsetFilter::kValidOrInvalidOffsets) <=
      aInfo.mChangeEnd) {
    newOffset = aInfo.mChangeStart;
  } else {
    newOffset = *aBoundary.Offset(
        RawRangeBoundary::OffsetFilter::kValidOrInvalidOffsets);
    newOffset -= aInfo.LengthOfRemovedText();
    newOffset += aInfo.mReplaceLength;
  }

  // newOffset.isValid() isn't checked explicitly here, because
  // newOffset.value() contains an assertion.
  return {aBoundary.GetContainer(), newOffset.value()};
}
