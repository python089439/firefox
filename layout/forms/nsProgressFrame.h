/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsProgressFrame_h___
#define nsProgressFrame_h___

#include "mozilla/Attributes.h"
#include "nsCOMPtr.h"
#include "nsContainerFrame.h"
#include "nsIAnonymousContentCreator.h"

namespace mozilla {
enum class PseudoStyleType : uint8_t;
}  // namespace mozilla

class nsProgressFrame final : public nsContainerFrame,
                              public nsIAnonymousContentCreator {
  using Element = mozilla::dom::Element;

 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsProgressFrame)

  enum class Type : uint8_t { Progress, Meter };
  nsProgressFrame(ComputedStyle*, nsPresContext*, Type);
  virtual ~nsProgressFrame();

  void Destroy(DestroyContext&) override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  void Reflow(nsPresContext*, ReflowOutput&, const ReflowInput&,
              nsReflowStatus&) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"Progress"_ns, aResult);
  }
#endif

  // nsIAnonymousContentCreator
  nsresult CreateAnonymousContent(nsTArray<ContentInfo>& aElements) override;
  void AppendAnonymousContentTo(nsTArray<nsIContent*>& aElements,
                                uint32_t aFilter) override;

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            int32_t aModType) override;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  bool ShouldUseNativeStyle() const;

 protected:
  mozilla::LogicalSize DefaultSize() const;
  // Helper function to reflow a child frame.
  void ReflowChildFrame(nsIFrame* aChild, nsPresContext* aPresContext,
                        const ReflowInput& aReflowInput,
                        const mozilla::LogicalSize& aParentContentBoxSize,
                        nsReflowStatus& aStatus);

  /**
   * The div used to show the progress bar.
   * @see nsProgressFrame::CreateAnonymousContent
   */
  nsCOMPtr<Element> mBarDiv;

  // Whether we're a progress or a meter element.
  const Type mType;
};

#endif
