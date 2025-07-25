/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

<%namespace name="helpers" file="/helpers.mako.rs" />

${helpers.two_properties_shorthand(
    "overflow",
    "overflow-x",
    "overflow-y",
    engines="gecko servo",
    spec="https://drafts.csswg.org/css-overflow/#propdef-overflow",
)}

${helpers.two_properties_shorthand(
    "overflow-clip-box",
    "overflow-clip-box-block",
    "overflow-clip-box-inline",
    engines="gecko",
    enabled_in="chrome",
    gecko_pref="layout.css.overflow-clip-box.enabled",
    spec="Internal, may be standardized in the future "
         "(https://developer.mozilla.org/en-US/docs/Web/CSS/overflow-clip-box)",
)}

${helpers.two_properties_shorthand(
    "overscroll-behavior",
    "overscroll-behavior-x",
    "overscroll-behavior-y",
    engines="gecko",
    spec="https://wicg.github.io/overscroll-behavior/#overscroll-behavior-properties",
)}

<%helpers:shorthand
    engines="gecko"
    name="container"
    sub_properties="container-name container-type"
    spec="https://drafts.csswg.org/css-contain-3/#container-shorthand"
>
    use crate::values::specified::box_::{ContainerName, ContainerType};
    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::parser::Parse;
        // See https://github.com/w3c/csswg-drafts/issues/7180 for why we don't
        // match the spec.
        let container_name = ContainerName::parse(context, input)?;
        let container_type = if input.try_parse(|input| input.expect_delim('/')).is_ok() {
            ContainerType::parse(input)?
        } else {
            ContainerType::Normal
        };
        Ok(expanded! {
            container_name: container_name,
            container_type: container_type,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result where W: fmt::Write {
            self.container_name.to_css(dest)?;
            if !self.container_type.is_normal() {
                dest.write_str(" / ")?;
                self.container_type.to_css(dest)?;
            }
            Ok(())
        }
    }
</%helpers:shorthand>

<%helpers:shorthand
    engines="gecko"
    name="page-break-before"
    flags="IS_LEGACY_SHORTHAND"
    sub_properties="break-before"
    spec="https://drafts.csswg.org/css-break-3/#page-break-properties"
>
    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::values::specified::box_::BreakBetween;
        Ok(expanded! {
            break_before: BreakBetween::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result where W: fmt::Write {
            self.break_before.to_css_legacy(dest)
        }
    }
</%helpers:shorthand>

<%helpers:shorthand
    engines="gecko"
    name="page-break-after"
    flags="IS_LEGACY_SHORTHAND"
    sub_properties="break-after"
    spec="https://drafts.csswg.org/css-break-3/#page-break-properties"
>
    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::values::specified::box_::BreakBetween;
        Ok(expanded! {
            break_after: BreakBetween::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result where W: fmt::Write {
            self.break_after.to_css_legacy(dest)
        }
    }
</%helpers:shorthand>

<%helpers:shorthand
    engines="gecko"
    name="page-break-inside"
    flags="IS_LEGACY_SHORTHAND"
    sub_properties="break-inside"
    spec="https://drafts.csswg.org/css-break-3/#page-break-properties"
>
    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::values::specified::box_::BreakWithin;
        Ok(expanded! {
            break_inside: BreakWithin::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result where W: fmt::Write {
            self.break_inside.to_css_legacy(dest)
        }
    }
</%helpers:shorthand>

<%helpers:shorthand name="offset"
                    engines="gecko"
                    sub_properties="offset-path offset-distance offset-rotate offset-anchor
                                    offset-position"
                    spec="https://drafts.fxtf.org/motion-1/#offset-shorthand">
    use crate::parser::Parse;
    use crate::values::specified::motion::{OffsetPath, OffsetPosition, OffsetRotate};
    use crate::values::specified::{LengthPercentage, PositionOrAuto};
    use crate::Zero;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let offset_position = input.try_parse(|i| OffsetPosition::parse(context, i)).ok();
        let offset_path = input.try_parse(|i| OffsetPath::parse(context, i)).ok();

        // Must have one of [offset-position, offset-path].
        // FIXME: The syntax is out-of-date after the update of the spec.
        // https://github.com/w3c/fxtf-drafts/issues/515
        if offset_position.is_none() && offset_path.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        let mut offset_distance = None;
        let mut offset_rotate = None;
        // offset-distance and offset-rotate are grouped with offset-path.
        if offset_path.is_some() {
            loop {
                if offset_distance.is_none() {
                    if let Ok(value) = input.try_parse(|i| LengthPercentage::parse(context, i)) {
                        offset_distance = Some(value);
                    }
                }

                if offset_rotate.is_none() {
                    if let Ok(value) = input.try_parse(|i| OffsetRotate::parse(context, i)) {
                        offset_rotate = Some(value);
                        continue;
                    }
                }
                break;
            }
        }

        let offset_anchor = input.try_parse(|i| {
            i.expect_delim('/')?;
            PositionOrAuto::parse(context, i)
        }).ok();

        Ok(expanded! {
            offset_position: offset_position.unwrap_or(OffsetPosition::normal()),
            offset_path: offset_path.unwrap_or(OffsetPath::none()),
            offset_distance: offset_distance.unwrap_or(LengthPercentage::zero()),
            offset_rotate: offset_rotate.unwrap_or(OffsetRotate::auto()),
            offset_anchor: offset_anchor.unwrap_or(PositionOrAuto::auto()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a>  {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result where W: fmt::Write {
            // The basic concept is: we must serialize offset-position or offset-path group.
            // offset-path group means "offset-path offset-distance offset-rotate".
            let must_serialize_path = *self.offset_path != OffsetPath::None
                || (!self.offset_distance.is_zero() || !self.offset_rotate.is_auto());
            let position_is_default = matches!(self.offset_position, OffsetPosition::Normal);
            if !position_is_default || !must_serialize_path {
                self.offset_position.to_css(dest)?;
            }

            if must_serialize_path {
                if !position_is_default {
                    dest.write_char(' ')?;
                }
                self.offset_path.to_css(dest)?;
            }

            if !self.offset_distance.is_zero() {
                dest.write_char(' ')?;
                self.offset_distance.to_css(dest)?;
            }

            if !self.offset_rotate.is_auto() {
                dest.write_char(' ')?;
                self.offset_rotate.to_css(dest)?;
            }

            if *self.offset_anchor != PositionOrAuto::auto() {
                dest.write_str(" / ")?;
                self.offset_anchor.to_css(dest)?;
            }
            Ok(())
        }
    }
</%helpers:shorthand>
