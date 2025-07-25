/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/NumericInputTypes.h"

#include "mozilla/TextControlState.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "ICUUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

bool NumericInputTypeBase::IsRangeOverflow() const {
  Decimal maximum = mInputElement->GetMaximum();
  if (maximum.isNaN()) {
    return false;
  }

  Decimal value = mInputElement->GetValueAsDecimal();
  if (value.isNaN()) {
    return false;
  }

  return value > maximum;
}

bool NumericInputTypeBase::IsRangeUnderflow() const {
  Decimal minimum = mInputElement->GetMinimum();
  if (minimum.isNaN()) {
    return false;
  }

  Decimal value = mInputElement->GetValueAsDecimal();
  if (value.isNaN()) {
    return false;
  }

  return value < minimum;
}

bool NumericInputTypeBase::HasStepMismatch() const {
  Decimal value = mInputElement->GetValueAsDecimal();
  return mInputElement->ValueIsStepMismatch(value);
}

nsresult NumericInputTypeBase::GetRangeOverflowMessage(nsAString& aMessage) {
  // We want to show the value as parsed when it's a number
  Decimal maximum = mInputElement->GetMaximum();
  MOZ_ASSERT(!maximum.isNaN());

  nsAutoString maxStr;
  ConvertNumberToString(maximum, Localized::Yes, maxStr);
  return nsContentUtils::FormatMaybeLocalizedString(
      aMessage, nsContentUtils::eDOM_PROPERTIES,
      "FormValidationNumberRangeOverflow", mInputElement->OwnerDoc(), maxStr);
}

nsresult NumericInputTypeBase::GetRangeUnderflowMessage(nsAString& aMessage) {
  Decimal minimum = mInputElement->GetMinimum();
  MOZ_ASSERT(!minimum.isNaN());

  nsAutoString minStr;
  ConvertNumberToString(minimum, Localized::Yes, minStr);
  return nsContentUtils::FormatMaybeLocalizedString(
      aMessage, nsContentUtils::eDOM_PROPERTIES,
      "FormValidationNumberRangeUnderflow", mInputElement->OwnerDoc(), minStr);
}

auto NumericInputTypeBase::ConvertStringToNumber(const nsAString& aValue) const
    -> StringToNumberResult {
  return {HTMLInputElement::StringToDecimal(aValue)};
}

bool NumericInputTypeBase::ConvertNumberToString(
    Decimal aValue, Localized, nsAString& aResultString) const {
  MOZ_ASSERT(aValue.isFinite(), "aValue must be a valid non-Infinite number.");
  aResultString.Truncate();
  aResultString.AssignASCII(aValue.toString().c_str());
  return true;
}

/* input type=number */

bool NumberInputType::IsValueMissing() const {
  if (!mInputElement->IsRequired()) {
    return false;
  }

  if (!IsMutable()) {
    return false;
  }

  return IsValueEmpty();
}

bool NumberInputType::HasBadInput() const {
  nsAutoString value;
  GetNonFileValueInternal(value);
  return !value.IsEmpty() && mInputElement->GetValueAsDecimal().isNaN();
}

auto NumberInputType::ConvertStringToNumber(const nsAString& aValue) const
    -> StringToNumberResult {
  auto result = NumericInputTypeBase::ConvertStringToNumber(aValue);
  if (result.mResult.isFinite()) {
    return result;
  }
  // Try to read the localized value from the user.
  ICUUtils::LanguageTagIterForContent langTagIter(mInputElement);
  result.mLocalized = true;
  result.mResult =
      Decimal::fromDouble(ICUUtils::ParseNumber(aValue, langTagIter));
  return result;
}

bool NumberInputType::ConvertNumberToString(Decimal aValue,
                                            Localized aLocalized,
                                            nsAString& aResultString) const {
  MOZ_ASSERT(aValue.isFinite(), "aValue must be a valid non-Infinite number.");

  if (aLocalized == Localized::No) {
    return NumericInputTypeBase::ConvertNumberToString(aValue, aLocalized,
                                                       aResultString);
  }
  aResultString.Truncate();
  ICUUtils::LanguageTagIterForContent langTagIter(mInputElement);
  ICUUtils::LocalizeNumber(aValue.toDouble(), langTagIter, aResultString);
  return true;
}

nsresult NumberInputType::GetValueMissingMessage(nsAString& aMessage) {
  return nsContentUtils::GetMaybeLocalizedString(
      nsContentUtils::eDOM_PROPERTIES, "FormValidationBadInputNumber",
      mInputElement->OwnerDoc(), aMessage);
}

nsresult NumberInputType::GetBadInputMessage(nsAString& aMessage) {
  return nsContentUtils::GetMaybeLocalizedString(
      nsContentUtils::eDOM_PROPERTIES, "FormValidationBadInputNumber",
      mInputElement->OwnerDoc(), aMessage);
}

bool NumberInputType::IsMutable() const {
  return !mInputElement->IsDisabledOrReadOnly();
}

/* input type=range */
void RangeInputType::MinMaxStepAttrChanged() {
  // The value may need to change when @min/max/step changes since the value may
  // have been invalid and can now change to a valid value, or vice versa. For
  // example, consider: <input type=range value=-1 max=1 step=3>. The valid
  // range is 0 to 1 while the nearest valid steps are -1 and 2 (the max value
  // having prevented there being a valid step in range). Changing @max to/from
  // 1 and a number greater than on equal to 3 should change whether we have a
  // step mismatch or not.
  // The value may also need to change between a value that results in a step
  // mismatch and a value that results in overflow. For example, if @max in the
  // example above were to change from 1 to -1.
  nsAutoString value;
  GetNonFileValueInternal(value);
  SetValueInternal(value, TextControlState::ValueSetterOption::ByInternalAPI);
}
