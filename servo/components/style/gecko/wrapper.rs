/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![allow(unsafe_code)]

//! Wrapper definitions on top of Gecko types in order to be used in the style
//! system.
//!
//! This really follows the Servo pattern in
//! `components/script/layout_wrapper.rs`.
//!
//! This theoretically should live in its own crate, but now it lives in the
//! style system it's kind of pointless in the Stylo case, and only Servo forces
//! the separation between the style system implementation and everything else.

use crate::applicable_declarations::ApplicableDeclarationBlock;
use crate::bloom::each_relevant_element_hash;
use crate::context::{QuirksMode, SharedStyleContext, UpdateAnimationsTasks};
use crate::data::ElementData;
use crate::dom::{LayoutIterator, NodeInfo, OpaqueNode, TDocument, TElement, TNode, TShadowRoot};
use crate::gecko::selector_parser::{NonTSPseudoClass, PseudoElement, SelectorImpl};
use crate::gecko::snapshot_helpers;
use crate::gecko_bindings::bindings;
use crate::gecko_bindings::bindings::Gecko_ElementHasAnimations;
use crate::gecko_bindings::bindings::Gecko_ElementHasCSSAnimations;
use crate::gecko_bindings::bindings::Gecko_ElementHasCSSTransitions;
use crate::gecko_bindings::bindings::Gecko_ElementState;
use crate::gecko_bindings::bindings::Gecko_GetActiveLinkAttrDeclarationBlock;
use crate::gecko_bindings::bindings::Gecko_GetAnimationEffectCount;
use crate::gecko_bindings::bindings::Gecko_GetAnimationRule;
use crate::gecko_bindings::bindings::Gecko_GetExtraContentStyleDeclarations;
use crate::gecko_bindings::bindings::Gecko_GetHTMLPresentationAttrDeclarationBlock;
use crate::gecko_bindings::bindings::Gecko_GetStyleAttrDeclarationBlock;
use crate::gecko_bindings::bindings::Gecko_GetUnvisitedLinkAttrDeclarationBlock;
use crate::gecko_bindings::bindings::Gecko_GetVisitedLinkAttrDeclarationBlock;
use crate::gecko_bindings::bindings::Gecko_IsSignificantChild;
use crate::gecko_bindings::bindings::Gecko_MatchLang;
use crate::gecko_bindings::bindings::Gecko_UnsetDirtyStyleAttr;
use crate::gecko_bindings::bindings::Gecko_UpdateAnimations;
use crate::gecko_bindings::structs;
use crate::gecko_bindings::structs::nsChangeHint;
use crate::gecko_bindings::structs::EffectCompositor_CascadeLevel as CascadeLevel;
use crate::gecko_bindings::structs::ELEMENT_HANDLED_SNAPSHOT;
use crate::gecko_bindings::structs::ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO;
use crate::gecko_bindings::structs::ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO;
use crate::gecko_bindings::structs::ELEMENT_HAS_SNAPSHOT;
use crate::gecko_bindings::structs::NODE_DESCENDANTS_NEED_FRAMES;
use crate::gecko_bindings::structs::NODE_NEEDS_FRAME;
use crate::gecko_bindings::structs::{nsAtom, nsIContent, nsINode_BooleanFlag};
use crate::gecko_bindings::structs::{nsINode as RawGeckoNode, Element as RawGeckoElement};
use crate::global_style_data::GLOBAL_STYLE_DATA;
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::media_queries::Device;
use crate::properties::{
    animated_properties::{AnimationValue, AnimationValueMap},
    ComputedValues, Importance, OwnedPropertyDeclarationId, PropertyDeclaration,
    PropertyDeclarationBlock, PropertyDeclarationId, PropertyDeclarationIdSet,
};
use crate::rule_tree::CascadeLevel as ServoCascadeLevel;
use crate::selector_parser::{AttrValue, Lang};
use crate::shared_lock::{Locked, SharedRwLock};
use crate::string_cache::{Atom, Namespace, WeakAtom, WeakNamespace};
use crate::stylesheets::scope_rule::ImplicitScopeRoot;
use crate::stylist::CascadeData;
use crate::values::computed::Display;
use crate::values::{AtomIdent, AtomString};
use crate::CaseSensitivityExt;
use crate::LocalName;
use app_units::Au;
use atomic_refcell::{AtomicRef, AtomicRefCell, AtomicRefMut};
use dom::{DocumentState, ElementState};
use euclid::default::Size2D;
use fxhash::FxHashMap;
use selectors::attr::{AttrSelectorOperation, CaseSensitivity, NamespaceConstraint};
use selectors::bloom::{BloomFilter, BLOOM_HASH_MASK};
use selectors::matching::VisitedHandlingMode;
use selectors::matching::{ElementSelectorFlags, MatchingContext};
use selectors::sink::Push;
use selectors::{Element, OpaqueElement};
use selectors::parser::PseudoElement as ParserPseudoElement;
use servo_arc::{Arc, ArcBorrow};
use std::cell::Cell;
use std::fmt;
use std::hash::{Hash, Hasher};
use std::mem;
use std::ptr;
use std::sync::atomic::{AtomicU32, Ordering};

#[inline]
fn elements_with_id<'a, 'le>(
    array: *const structs::nsTArray<*mut RawGeckoElement>,
) -> &'a [GeckoElement<'le>] {
    unsafe {
        if array.is_null() {
            return &[];
        }

        let elements: &[*mut RawGeckoElement] = &**array;

        // NOTE(emilio): We rely on the in-memory representation of
        // GeckoElement<'ld> and *mut RawGeckoElement being the same.
        #[allow(dead_code)]
        unsafe fn static_assert() {
            mem::transmute::<*mut RawGeckoElement, GeckoElement<'static>>(0xbadc0de as *mut _);
        }

        mem::transmute(elements)
    }
}

/// A simple wrapper over `Document`.
#[derive(Clone, Copy)]
pub struct GeckoDocument<'ld>(pub &'ld structs::Document);

impl<'ld> TDocument for GeckoDocument<'ld> {
    type ConcreteNode = GeckoNode<'ld>;

    #[inline]
    fn as_node(&self) -> Self::ConcreteNode {
        GeckoNode(&self.0._base)
    }

    #[inline]
    fn is_html_document(&self) -> bool {
        self.0.mType == structs::Document_Type::eHTML
    }

    #[inline]
    fn quirks_mode(&self) -> QuirksMode {
        self.0.mCompatMode.into()
    }

    #[inline]
    fn elements_with_id<'a>(&self, id: &AtomIdent) -> Result<&'a [GeckoElement<'ld>], ()>
    where
        Self: 'a,
    {
        Ok(elements_with_id(unsafe {
            bindings::Gecko_Document_GetElementsWithId(self.0, id.as_ptr())
        }))
    }

    fn shared_lock(&self) -> &SharedRwLock {
        &GLOBAL_STYLE_DATA.shared_lock
    }
}

/// A simple wrapper over `ShadowRoot`.
#[derive(Clone, Copy)]
pub struct GeckoShadowRoot<'lr>(pub &'lr structs::ShadowRoot);

impl<'ln> fmt::Debug for GeckoShadowRoot<'ln> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // TODO(emilio): Maybe print the host or something?
        write!(f, "<shadow-root> ({:#x})", self.as_node().opaque().0)
    }
}

impl<'lr> PartialEq for GeckoShadowRoot<'lr> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.0 as *const _ == other.0 as *const _
    }
}

impl<'lr> TShadowRoot for GeckoShadowRoot<'lr> {
    type ConcreteNode = GeckoNode<'lr>;

    #[inline]
    fn as_node(&self) -> Self::ConcreteNode {
        GeckoNode(&self.0._base._base._base._base)
    }

    #[inline]
    fn host(&self) -> GeckoElement<'lr> {
        GeckoElement(unsafe { &*self.0._base.mHost.mRawPtr })
    }

    #[inline]
    fn style_data<'a>(&self) -> Option<&'a CascadeData>
    where
        Self: 'a,
    {
        let author_styles = unsafe { self.0.mServoStyles.mPtr.as_ref()? };
        Some(&author_styles.data)
    }

    #[inline]
    fn elements_with_id<'a>(&self, id: &AtomIdent) -> Result<&'a [GeckoElement<'lr>], ()>
    where
        Self: 'a,
    {
        Ok(elements_with_id(unsafe {
            bindings::Gecko_ShadowRoot_GetElementsWithId(self.0, id.as_ptr())
        }))
    }

    #[inline]
    fn parts<'a>(&self) -> &[<Self::ConcreteNode as TNode>::ConcreteElement]
    where
        Self: 'a,
    {
        let slice: &[*const RawGeckoElement] = &*self.0.mParts;

        #[allow(dead_code)]
        unsafe fn static_assert() {
            mem::transmute::<*const RawGeckoElement, GeckoElement<'static>>(0xbadc0de as *const _);
        }

        unsafe { mem::transmute(slice) }
    }

    #[inline]
    fn implicit_scope_for_sheet(&self, sheet_index: usize) -> Option<ImplicitScopeRoot> {
        use crate::stylesheets::StylesheetInDocument;

        let author_styles = unsafe { self.0.mServoStyles.mPtr.as_ref()? };
        let sheet = author_styles.stylesheets.get(sheet_index)?;
        sheet.implicit_scope_root()
    }
}

/// A simple wrapper over a non-null Gecko node (`nsINode`) pointer.
///
/// Important: We don't currently refcount the DOM, because the wrapper lifetime
/// magic guarantees that our LayoutFoo references won't outlive the root, and
/// we don't mutate any of the references on the Gecko side during restyle.
///
/// We could implement refcounting if need be (at a potentially non-trivial
/// performance cost) by implementing Drop and making LayoutFoo non-Copy.
#[derive(Clone, Copy)]
pub struct GeckoNode<'ln>(pub &'ln RawGeckoNode);

impl<'ln> PartialEq for GeckoNode<'ln> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.0 as *const _ == other.0 as *const _
    }
}

impl<'ln> fmt::Debug for GeckoNode<'ln> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if let Some(el) = self.as_element() {
            return el.fmt(f);
        }

        if self.is_text_node() {
            return write!(f, "<text node> ({:#x})", self.opaque().0);
        }

        if self.is_document() {
            return write!(f, "<document> ({:#x})", self.opaque().0);
        }

        if let Some(sr) = self.as_shadow_root() {
            return sr.fmt(f);
        }

        write!(f, "<non-text node> ({:#x})", self.opaque().0)
    }
}

impl<'ln> GeckoNode<'ln> {
    #[inline]
    fn is_document(&self) -> bool {
        // This is a DOM constant that isn't going to change.
        const DOCUMENT_NODE: u16 = 9;
        self.node_info().mInner.mNodeType == DOCUMENT_NODE
    }

    #[inline]
    fn is_shadow_root(&self) -> bool {
        self.is_in_shadow_tree() && self.parent_node().is_none()
    }

    #[inline]
    fn from_content(content: &'ln nsIContent) -> Self {
        GeckoNode(&content._base)
    }

    #[inline]
    fn set_flags(&self, flags: u32) {
        self.flags_atomic().fetch_or(flags, Ordering::Relaxed);
    }

    fn flags_atomic_for(flags: &Cell<u32>) -> &AtomicU32 {
        const_assert!(std::mem::size_of::<Cell<u32>>() == std::mem::size_of::<AtomicU32>());
        const_assert!(std::mem::align_of::<Cell<u32>>() == std::mem::align_of::<AtomicU32>());

        // Rust doesn't provide standalone atomic functions like GCC/clang do
        // (via the atomic intrinsics) or via std::atomic_ref, but it guarantees
        // that the memory representation of u32 and AtomicU32 matches:
        // https://doc.rust-lang.org/std/sync/atomic/struct.AtomicU32.html
        unsafe { std::mem::transmute::<&Cell<u32>, &AtomicU32>(flags) }
    }

    #[inline]
    fn flags_atomic(&self) -> &AtomicU32 {
        Self::flags_atomic_for(&self.0._base._base_1.mFlags)
    }

    #[inline]
    fn flags(&self) -> u32 {
        self.flags_atomic().load(Ordering::Relaxed)
    }

    #[inline]
    fn selector_flags_atomic(&self) -> &AtomicU32 {
        Self::flags_atomic_for(&self.0.mSelectorFlags)
    }

    #[inline]
    fn selector_flags(&self) -> u32 {
        self.selector_flags_atomic().load(Ordering::Relaxed)
    }

    #[inline]
    fn set_selector_flags(&self, flags: u32) {
        self.selector_flags_atomic()
            .fetch_or(flags, Ordering::Relaxed);
    }

    #[inline]
    fn node_info(&self) -> &structs::NodeInfo {
        debug_assert!(!self.0.mNodeInfo.mRawPtr.is_null());
        unsafe { &*self.0.mNodeInfo.mRawPtr }
    }

    // These live in different locations depending on processor architecture.
    #[cfg(target_pointer_width = "64")]
    #[inline]
    fn bool_flags(&self) -> u32 {
        (self.0)._base._base_1.mBoolFlags
    }

    #[cfg(target_pointer_width = "32")]
    #[inline]
    fn bool_flags(&self) -> u32 {
        (self.0).mBoolFlags
    }

    #[inline]
    fn get_bool_flag(&self, flag: nsINode_BooleanFlag) -> bool {
        self.bool_flags() & (1u32 << flag as u32) != 0
    }

    /// This logic is duplicate in Gecko's nsINode::IsInShadowTree().
    #[inline]
    fn is_in_shadow_tree(&self) -> bool {
        use crate::gecko_bindings::structs::NODE_IS_IN_SHADOW_TREE;
        self.flags() & NODE_IS_IN_SHADOW_TREE != 0
    }

    /// Returns true if we know for sure that `flattened_tree_parent` and `parent_node` return the
    /// same thing.
    ///
    /// TODO(emilio): Measure and consider not doing this fast-path, it's only a function call and
    /// from profiles it seems that keeping this fast path makes the compiler not inline
    /// `flattened_tree_parent` as a whole, so we're not gaining much either.
    #[inline]
    fn flattened_tree_parent_is_parent(&self) -> bool {
        use crate::gecko_bindings::structs::*;
        let flags = self.flags();

        let parent = match self.parent_node() {
            Some(p) => p,
            None => return true,
        };

        if parent.is_shadow_root() {
            return false;
        }

        if let Some(parent) = parent.as_element() {
            if flags & NODE_IS_NATIVE_ANONYMOUS_ROOT != 0 && parent.is_root() {
                return false;
            }
            if parent.shadow_root().is_some() || parent.is_html_slot_element() {
                return false;
            }
        }

        true
    }

    #[inline]
    fn flattened_tree_parent(&self) -> Option<Self> {
        if self.flattened_tree_parent_is_parent() {
            debug_assert_eq!(
                unsafe {
                    bindings::Gecko_GetFlattenedTreeParentNode(self.0)
                        .as_ref()
                        .map(GeckoNode)
                },
                self.parent_node(),
                "Fast path stopped holding!"
            );
            return self.parent_node();
        }

        // NOTE(emilio): If this call is too expensive, we could manually inline more aggressively.
        unsafe {
            bindings::Gecko_GetFlattenedTreeParentNode(self.0)
                .as_ref()
                .map(GeckoNode)
        }
    }

    #[inline]
    fn contains_non_whitespace_content(&self) -> bool {
        unsafe { Gecko_IsSignificantChild(self.0, false) }
    }

    /// Returns the previous sibling of this node that is an element.
    #[inline]
    pub fn prev_sibling_element(&self) -> Option<GeckoElement> {
        let mut prev = self.prev_sibling();
        while let Some(p) = prev {
            if let Some(e) = p.as_element() {
                return Some(e);
            }
            prev = p.prev_sibling();
        }
        None
    }

    /// Returns the next sibling of this node that is an element.
    #[inline]
    pub fn next_sibling_element(&self) -> Option<GeckoElement> {
        let mut next = self.next_sibling();
        while let Some(n) = next {
            if let Some(e) = n.as_element() {
                return Some(e);
            }
            next = n.next_sibling();
        }
        None
    }

    /// Returns last child sibling of this node that is an element.
    #[inline]
    pub fn last_child_element(&self) -> Option<GeckoElement<'ln>> {
        let last = match self.last_child() {
            Some(n) => n,
            None => return None,
        };
        if let Some(e) = last.as_element() {
            return Some(e);
        }
        None
    }
}

impl<'ln> NodeInfo for GeckoNode<'ln> {
    #[inline]
    fn is_element(&self) -> bool {
        self.get_bool_flag(nsINode_BooleanFlag::NodeIsElement)
    }

    fn is_text_node(&self) -> bool {
        // This is a DOM constant that isn't going to change.
        const TEXT_NODE: u16 = 3;
        self.node_info().mInner.mNodeType == TEXT_NODE
    }
}

impl<'ln> TNode for GeckoNode<'ln> {
    type ConcreteDocument = GeckoDocument<'ln>;
    type ConcreteShadowRoot = GeckoShadowRoot<'ln>;
    type ConcreteElement = GeckoElement<'ln>;

    #[inline]
    fn parent_node(&self) -> Option<Self> {
        unsafe { self.0.mParent.as_ref().map(GeckoNode) }
    }

    #[inline]
    fn first_child(&self) -> Option<Self> {
        unsafe {
            self.0
                .mFirstChild
                .raw()
                .as_ref()
                .map(GeckoNode::from_content)
        }
    }

    #[inline]
    fn last_child(&self) -> Option<Self> {
        unsafe { bindings::Gecko_GetLastChild(self.0).as_ref().map(GeckoNode) }
    }

    #[inline]
    fn prev_sibling(&self) -> Option<Self> {
        unsafe {
            let prev_or_last = GeckoNode::from_content(self.0.mPreviousOrLastSibling.as_ref()?);
            if prev_or_last.0.mNextSibling.raw().is_null() {
                return None;
            }
            Some(prev_or_last)
        }
    }

    #[inline]
    fn next_sibling(&self) -> Option<Self> {
        unsafe {
            self.0
                .mNextSibling
                .raw()
                .as_ref()
                .map(GeckoNode::from_content)
        }
    }

    #[inline]
    fn owner_doc(&self) -> Self::ConcreteDocument {
        debug_assert!(!self.node_info().mDocument.is_null());
        GeckoDocument(unsafe { &*self.node_info().mDocument })
    }

    #[inline]
    fn is_in_document(&self) -> bool {
        self.get_bool_flag(nsINode_BooleanFlag::IsInDocument)
    }

    fn traversal_parent(&self) -> Option<GeckoElement<'ln>> {
        self.flattened_tree_parent().and_then(|n| n.as_element())
    }

    #[inline]
    fn opaque(&self) -> OpaqueNode {
        let ptr: usize = self.0 as *const _ as usize;
        OpaqueNode(ptr)
    }

    fn debug_id(self) -> usize {
        unimplemented!()
    }

    #[inline]
    fn as_element(&self) -> Option<GeckoElement<'ln>> {
        if !self.is_element() {
            return None;
        }

        Some(GeckoElement(unsafe {
            &*(self.0 as *const _ as *const RawGeckoElement)
        }))
    }

    #[inline]
    fn as_document(&self) -> Option<Self::ConcreteDocument> {
        if !self.is_document() {
            return None;
        }

        debug_assert_eq!(self.owner_doc().as_node(), *self, "How?");
        Some(self.owner_doc())
    }

    #[inline]
    fn as_shadow_root(&self) -> Option<Self::ConcreteShadowRoot> {
        if !self.is_shadow_root() {
            return None;
        }

        Some(GeckoShadowRoot(unsafe {
            &*(self.0 as *const _ as *const structs::ShadowRoot)
        }))
    }
}

/// A wrapper on top of two kind of iterators, depending on the parent being
/// iterated.
///
/// We generally iterate children by traversing the light-tree siblings of the
/// first child like Servo does.
///
/// However, for nodes with anonymous children, we use a custom (heavier-weight)
/// Gecko-implemented iterator.
///
/// FIXME(emilio): If we take into account shadow DOM, we're going to need the
/// flat tree pretty much always. We can try to optimize the case where there's
/// no shadow root sibling, probably.
pub enum GeckoChildrenIterator<'a> {
    /// A simple iterator that tracks the current node being iterated and
    /// replaces it with the next sibling when requested.
    Current(Option<GeckoNode<'a>>),
    /// A Gecko-implemented iterator we need to drop appropriately.
    GeckoIterator(std::mem::ManuallyDrop<structs::StyleChildrenIterator>),
}

impl<'a> Drop for GeckoChildrenIterator<'a> {
    fn drop(&mut self) {
        if let GeckoChildrenIterator::GeckoIterator(ref mut it) = *self {
            unsafe {
                bindings::Gecko_DestroyStyleChildrenIterator(&mut **it);
            }
        }
    }
}

impl<'a> Iterator for GeckoChildrenIterator<'a> {
    type Item = GeckoNode<'a>;
    fn next(&mut self) -> Option<GeckoNode<'a>> {
        match *self {
            GeckoChildrenIterator::Current(curr) => {
                let next = curr.and_then(|node| node.next_sibling());
                *self = GeckoChildrenIterator::Current(next);
                curr
            },
            GeckoChildrenIterator::GeckoIterator(ref mut it) => unsafe {
                // We do this unsafe lengthening of the lifetime here because
                // structs::StyleChildrenIterator is actually StyleChildrenIterator<'a>,
                // however we can't express this easily with bindgen, and it would
                // introduce functions with two input lifetimes into bindgen,
                // which would be out of scope for elision.
                bindings::Gecko_GetNextStyleChild(&mut **it)
                    .as_ref()
                    .map(GeckoNode)
            },
        }
    }
}

/// A simple wrapper over a non-null Gecko `Element` pointer.
#[derive(Clone, Copy)]
pub struct GeckoElement<'le>(pub &'le RawGeckoElement);

impl<'le> fmt::Debug for GeckoElement<'le> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use nsstring::nsCString;

        write!(f, "<{}", self.local_name())?;

        let mut attrs = nsCString::new();
        unsafe {
            bindings::Gecko_Element_DebugListAttributes(self.0, &mut attrs);
        }
        write!(f, "{}", attrs)?;
        write!(f, "> ({:#x})", self.as_node().opaque().0)
    }
}

impl<'le> GeckoElement<'le> {
    /// Gets the raw `ElementData` refcell for the element.
    #[inline(always)]
    pub fn get_data(&self) -> Option<&AtomicRefCell<ElementData>> {
        unsafe { self.0.mServoData.get().as_ref() }
    }

    /// Returns whether any animation applies to this element.
    #[inline]
    pub fn has_any_animation(&self) -> bool {
        self.may_have_animations() && unsafe { Gecko_ElementHasAnimations(self.0) }
    }

    #[inline(always)]
    fn attrs(&self) -> &[structs::AttrArray_InternalAttr] {
        unsafe {
            match self.0.mAttrs.mImpl.mPtr.as_ref() {
                Some(attrs) => attrs.mBuffer.as_slice(attrs.mAttrCount as usize),
                None => return &[],
            }
        }
    }

    #[inline(always)]
    fn get_part_attr(&self) -> Option<&structs::nsAttrValue> {
        if !self.has_part_attr() {
            return None;
        }
        snapshot_helpers::find_attr(self.attrs(), &atom!("part"))
    }

    #[inline(always)]
    fn get_class_attr(&self) -> Option<&structs::nsAttrValue> {
        if !self.may_have_class() {
            return None;
        }

        if self.is_svg_element() {
            let svg_class = unsafe { bindings::Gecko_GetSVGAnimatedClass(self.0).as_ref() };
            if let Some(c) = svg_class {
                return Some(c);
            }
        }

        snapshot_helpers::find_attr(self.attrs(), &atom!("class"))
    }

    #[inline]
    fn may_have_anonymous_children(&self) -> bool {
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementMayHaveAnonymousChildren)
    }

    #[inline]
    fn flags(&self) -> u32 {
        self.as_node().flags()
    }

    #[inline]
    fn set_flags(&self, flags: u32) {
        self.as_node().set_flags(flags);
    }

    #[inline]
    unsafe fn unset_flags(&self, flags: u32) {
        self.as_node()
            .flags_atomic()
            .fetch_and(!flags, Ordering::Relaxed);
    }

    /// Returns true if this element has descendants for lazy frame construction.
    #[inline]
    pub fn descendants_need_frames(&self) -> bool {
        self.flags() & NODE_DESCENDANTS_NEED_FRAMES != 0
    }

    /// Returns true if this element needs lazy frame construction.
    #[inline]
    pub fn needs_frame(&self) -> bool {
        self.flags() & NODE_NEEDS_FRAME != 0
    }

    /// Returns a reference to the DOM slots for this Element, if they exist.
    #[inline]
    fn dom_slots(&self) -> Option<&structs::FragmentOrElement_nsDOMSlots> {
        let slots = self.as_node().0.mSlots as *const structs::FragmentOrElement_nsDOMSlots;
        unsafe { slots.as_ref() }
    }

    /// Returns a reference to the extended DOM slots for this Element.
    #[inline]
    fn extended_slots(&self) -> Option<&structs::FragmentOrElement_nsExtendedDOMSlots> {
        self.dom_slots().and_then(|s| unsafe {
            // For the bit usage, see nsContentSlots::GetExtendedSlots.
            let e_slots = s._base.mExtendedSlots &
                !structs::nsIContent_nsContentSlots_sNonOwningExtendedSlotsFlag;
            (e_slots as *const structs::FragmentOrElement_nsExtendedDOMSlots).as_ref()
        })
    }

    #[inline]
    fn namespace_id(&self) -> i32 {
        self.as_node().node_info().mInner.mNamespaceID
    }

    #[inline]
    fn has_id(&self) -> bool {
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementHasID)
    }

    #[inline]
    fn state_internal(&self) -> u64 {
        if !self
            .as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementHasLockedStyleStates)
        {
            return self.0.mState.bits;
        }
        unsafe { Gecko_ElementState(self.0) }
    }

    #[inline]
    fn document_state(&self) -> DocumentState {
        DocumentState::from_bits_retain(self.as_node().owner_doc().0.mState.bits)
    }

    #[inline]
    fn may_have_class(&self) -> bool {
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementMayHaveClass)
    }

    #[inline]
    fn has_properties(&self) -> bool {
        use crate::gecko_bindings::structs::NODE_HAS_PROPERTIES;

        self.flags() & NODE_HAS_PROPERTIES != 0
    }

    #[inline]
    fn may_have_style_attribute(&self) -> bool {
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementMayHaveStyle)
    }

    /// Only safe to call on the main thread, with exclusive access to the
    /// element and its ancestors.
    ///
    /// This function is also called after display property changed for SMIL
    /// animation.
    ///
    /// Also this function schedules style flush.
    pub unsafe fn note_explicit_hints(&self, restyle_hint: RestyleHint, change_hint: nsChangeHint) {
        use crate::gecko::restyle_damage::GeckoRestyleDamage;

        let damage = GeckoRestyleDamage::new(change_hint);
        debug!(
            "note_explicit_hints: {:?}, restyle_hint={:?}, change_hint={:?}",
            self, restyle_hint, change_hint
        );
        debug_assert!(bindings::Gecko_IsMainThread());
        debug_assert!(
            !(restyle_hint.has_animation_hint() && restyle_hint.has_non_animation_hint()),
            "Animation restyle hints should not appear with non-animation restyle hints"
        );

        // See comments on borrow_assert_main_thread and co.
        let data = match self.get_data() {
            Some(d) => d,
            None => {
                debug!("(Element not styled, discarding hints)");
                return;
            },
        };

        // Propagate the bit up the chain.
        if restyle_hint.has_animation_hint() {
            bindings::Gecko_NoteAnimationOnlyDirtyElement(self.0);
        } else {
            bindings::Gecko_NoteDirtyElement(self.0);
        }

        #[cfg(debug_assertions)]
        let mut data = data.borrow_mut();
        #[cfg(not(debug_assertions))]
        let data = &mut *data.as_ptr();

        data.hint.insert(restyle_hint);
        data.damage |= damage;
    }

    /// This logic is duplicated in Gecko's nsIContent::IsRootOfNativeAnonymousSubtree.
    #[inline]
    fn is_root_of_native_anonymous_subtree(&self) -> bool {
        return self.flags() & structs::NODE_IS_NATIVE_ANONYMOUS_ROOT != 0;
    }

    /// Whether the element is in an anonymous subtree. Note that this includes UA widgets!
    #[inline]
    fn in_native_anonymous_subtree(&self) -> bool {
        (self.flags() & structs::NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE) != 0
    }

    /// Whether the element has been in a UA widget
    #[inline]
    fn in_ua_widget(&self) -> bool {
        (self.flags() & structs::NODE_HAS_BEEN_IN_UA_WIDGET) != 0
    }

    fn css_transitions_info(&self) -> FxHashMap<OwnedPropertyDeclarationId, Arc<AnimationValue>> {
        use crate::gecko_bindings::bindings::Gecko_ElementTransitions_EndValueAt;
        use crate::gecko_bindings::bindings::Gecko_ElementTransitions_Length;

        let collection_length = unsafe { Gecko_ElementTransitions_Length(self.0) } as usize;
        let mut map = FxHashMap::with_capacity_and_hasher(collection_length, Default::default());

        for i in 0..collection_length {
            let end_value =
                unsafe { Arc::from_raw_addrefed(Gecko_ElementTransitions_EndValueAt(self.0, i)) };
            let property = end_value.id();
            debug_assert!(!property.is_logical());
            map.insert(property.to_owned(), end_value);
        }
        map
    }

    fn needs_transitions_update_per_property(
        &self,
        property_declaration_id: PropertyDeclarationId,
        combined_duration_seconds: f32,
        before_change_style: &ComputedValues,
        after_change_style: &ComputedValues,
        existing_transitions: &FxHashMap<OwnedPropertyDeclarationId, Arc<AnimationValue>>,
    ) -> bool {
        debug_assert!(!property_declaration_id.is_logical());

        // If there is an existing transition, update only if the end value
        // differs.
        //
        // If the end value has not changed, we should leave the currently
        // running transition as-is since we don't want to interrupt its timing
        // function.
        if let Some(ref existing) = existing_transitions.get(&property_declaration_id.to_owned()) {
            let after_value =
                AnimationValue::from_computed_values(property_declaration_id, after_change_style);
            debug_assert!(
                after_value.is_some() ||
                    matches!(property_declaration_id, PropertyDeclarationId::Custom(..))
            );
            return after_value.is_none() || ***existing != after_value.unwrap();
        }

        if combined_duration_seconds <= 0.0f32 {
            return false;
        }

        let from =
            AnimationValue::from_computed_values(property_declaration_id, before_change_style);
        let to = AnimationValue::from_computed_values(property_declaration_id, after_change_style);
        debug_assert!(
            to.is_some() == from.is_some() ||
                // If the declaration contains a custom property and getComputedValue was previously
                // called before that custom property was defined, `from` will be `None` here.
                matches!(from, Some(AnimationValue::Custom(..))) ||
                // Similarly, if the declaration contains a custom property, getComputedValue was
                // previously called, and the custom property registration is removed, `to` will be
                // `None`.
                matches!(to, Some(AnimationValue::Custom(..)))
        );

        from != to
    }

    /// Get slow selector flags required for nth-of invalidation.
    pub fn slow_selector_flags(&self) -> ElementSelectorFlags {
        slow_selector_flags_from_node_selector_flags(self.as_node().selector_flags())
    }
}

/// Convert slow selector flags from the raw `NodeSelectorFlags`.
pub fn slow_selector_flags_from_node_selector_flags(flags: u32) -> ElementSelectorFlags {
    use crate::gecko_bindings::structs::NodeSelectorFlags;
    let mut result = ElementSelectorFlags::empty();
    if flags & NodeSelectorFlags::HasSlowSelector.0 != 0 {
        result.insert(ElementSelectorFlags::HAS_SLOW_SELECTOR);
    }
    if flags & NodeSelectorFlags::HasSlowSelectorLaterSiblings.0 != 0 {
        result.insert(ElementSelectorFlags::HAS_SLOW_SELECTOR_LATER_SIBLINGS);
    }
    result
}

/// Converts flags from the layout used by rust-selectors to the layout used
/// by Gecko. We could align these and then do this without conditionals, but
/// it's probably not worth the trouble.
fn selector_flags_to_node_flags(flags: ElementSelectorFlags) -> u32 {
    use crate::gecko_bindings::structs::NodeSelectorFlags;
    let mut gecko_flags = 0u32;
    if flags.contains(ElementSelectorFlags::HAS_SLOW_SELECTOR) {
        gecko_flags |= NodeSelectorFlags::HasSlowSelector.0;
    }
    if flags.contains(ElementSelectorFlags::HAS_SLOW_SELECTOR_LATER_SIBLINGS) {
        gecko_flags |= NodeSelectorFlags::HasSlowSelectorLaterSiblings.0;
    }
    if flags.contains(ElementSelectorFlags::HAS_SLOW_SELECTOR_NTH) {
        gecko_flags |= NodeSelectorFlags::HasSlowSelectorNth.0;
    }
    if flags.contains(ElementSelectorFlags::HAS_SLOW_SELECTOR_NTH_OF) {
        gecko_flags |= NodeSelectorFlags::HasSlowSelectorNthOf.0;
    }
    if flags.contains(ElementSelectorFlags::HAS_EDGE_CHILD_SELECTOR) {
        gecko_flags |= NodeSelectorFlags::HasEdgeChildSelector.0;
    }
    if flags.contains(ElementSelectorFlags::HAS_EMPTY_SELECTOR) {
        gecko_flags |= NodeSelectorFlags::HasEmptySelector.0;
    }
    if flags.contains(ElementSelectorFlags::ANCHORS_RELATIVE_SELECTOR) {
        gecko_flags |= NodeSelectorFlags::RelativeSelectorAnchor.0;
    }
    if flags.contains(ElementSelectorFlags::ANCHORS_RELATIVE_SELECTOR_NON_SUBJECT) {
        gecko_flags |= NodeSelectorFlags::RelativeSelectorAnchorNonSubject.0;
    }
    if flags.contains(ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR) {
        gecko_flags |= NodeSelectorFlags::RelativeSelectorSearchDirectionAncestor.0;
    }
    if flags.contains(ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING) {
        gecko_flags |= NodeSelectorFlags::RelativeSelectorSearchDirectionSibling.0;
    }

    gecko_flags
}

fn get_animation_rule(
    element: &GeckoElement,
    cascade_level: CascadeLevel,
) -> Option<Arc<Locked<PropertyDeclarationBlock>>> {
    // There's a very rough correlation between the number of effects
    // (animations) on an element and the number of properties it is likely to
    // animate, so we use that as an initial guess for the size of the
    // AnimationValueMap in order to reduce the number of re-allocations needed.
    let effect_count = unsafe { Gecko_GetAnimationEffectCount(element.0) };
    // Also, we should try to reuse the PDB, to avoid creating extra rule nodes.
    let mut animation_values = AnimationValueMap::with_capacity_and_hasher(
        effect_count.min(crate::properties::property_counts::ANIMATABLE),
        Default::default(),
    );
    if unsafe { Gecko_GetAnimationRule(element.0, cascade_level, &mut animation_values) } {
        let shared_lock = &GLOBAL_STYLE_DATA.shared_lock;
        Some(Arc::new(shared_lock.wrap(
            PropertyDeclarationBlock::from_animation_value_map(&animation_values),
        )))
    } else {
        None
    }
}

/// Turns a gecko namespace id into an atom. Might panic if you pass any random thing that isn't a
/// namespace id.
#[inline(always)]
pub unsafe fn namespace_id_to_atom(id: i32) -> *mut nsAtom {
    unsafe {
        let namespace_manager = structs::nsNameSpaceManager_sInstance.mRawPtr;
        (&(*namespace_manager).mURIArray)[id as usize].mRawPtr
    }
}

impl<'le> TElement for GeckoElement<'le> {
    type ConcreteNode = GeckoNode<'le>;
    type TraversalChildrenIterator = GeckoChildrenIterator<'le>;

    fn implicit_scope_for_sheet_in_shadow_root(
        opaque_host: OpaqueElement,
        sheet_index: usize,
    ) -> Option<ImplicitScopeRoot> {
        // As long as this "unopaqued" element does not escape this function, we're not leaking
        // potentially-mutable elements from opaque elements.
        let e = unsafe {
            Self(
                opaque_host
                    .as_const_ptr::<RawGeckoElement>()
                    .as_ref()
                    .unwrap(),
            )
        };
        let shadow_root = match e.shadow_root() {
            None => return None,
            Some(r) => r,
        };
        shadow_root.implicit_scope_for_sheet(sheet_index)
    }

    fn inheritance_parent(&self) -> Option<Self> {
        if let Some(pseudo) = self.implemented_pseudo_element() {
            if !pseudo.is_element_backed() {
                return self.pseudo_element_originating_element();
            }
        }

        self.as_node()
            .flattened_tree_parent()
            .and_then(|n| n.as_element())
    }

    fn traversal_children(&self) -> LayoutIterator<GeckoChildrenIterator<'le>> {
        // This condition is similar to the check that
        // StyleChildrenIterator::IsNeeded does, except that it might return
        // true if we used to (but no longer) have anonymous content from
        // ::before/::after, or nsIAnonymousContentCreators.
        if self.is_html_slot_element() ||
            self.shadow_root().is_some() ||
            self.may_have_anonymous_children()
        {
            unsafe {
                let mut iter = std::mem::MaybeUninit::<structs::StyleChildrenIterator>::uninit();
                bindings::Gecko_ConstructStyleChildrenIterator(self.0, iter.as_mut_ptr());
                return LayoutIterator(GeckoChildrenIterator::GeckoIterator(
                    std::mem::ManuallyDrop::new(iter.assume_init()),
                ));
            }
        }

        LayoutIterator(GeckoChildrenIterator::Current(self.as_node().first_child()))
    }

    #[inline]
    fn is_html_element(&self) -> bool {
        self.namespace_id() == structs::kNameSpaceID_XHTML as i32
    }

    #[inline]
    fn is_mathml_element(&self) -> bool {
        self.namespace_id() == structs::kNameSpaceID_MathML as i32
    }

    #[inline]
    fn is_svg_element(&self) -> bool {
        self.namespace_id() == structs::kNameSpaceID_SVG as i32
    }

    #[inline]
    fn is_xul_element(&self) -> bool {
        self.namespace_id() == structs::root::kNameSpaceID_XUL as i32
    }

    #[inline]
    fn local_name(&self) -> &WeakAtom {
        unsafe { WeakAtom::new(self.as_node().node_info().mInner.mName) }
    }

    #[inline]
    fn namespace(&self) -> &WeakNamespace {
        unsafe { WeakNamespace::new(namespace_id_to_atom(self.namespace_id())) }
    }

    #[inline]
    fn query_container_size(&self, display: &Display) -> Size2D<Option<Au>> {
        // If an element gets 'display: contents' and its nsIFrame has not been removed yet,
        // Gecko_GetQueryContainerSize will not notice that it can't have size containment.
        // Other cases like 'display: inline' will be handled once the new nsIFrame is created.
        if display.is_contents() {
            return Size2D::new(None, None);
        }

        let mut width = -1;
        let mut height = -1;
        unsafe {
            bindings::Gecko_GetQueryContainerSize(self.0, &mut width, &mut height);
        }
        Size2D::new(
            if width >= 0 { Some(Au(width)) } else { None },
            if height >= 0 { Some(Au(height)) } else { None },
        )
    }

    /// Return the list of slotted nodes of this node.
    #[inline]
    fn slotted_nodes(&self) -> &[Self::ConcreteNode] {
        if !self.is_html_slot_element() || !self.as_node().is_in_shadow_tree() {
            return &[];
        }

        let slot: &structs::HTMLSlotElement = unsafe { mem::transmute(self.0) };

        if cfg!(debug_assertions) {
            let base: &RawGeckoElement = &slot._base._base._base;
            assert_eq!(base as *const _, self.0 as *const _, "Bad cast");
        }

        // FIXME(emilio): Workaround a bindgen bug on Android that causes
        // mAssignedNodes to be at the wrong offset. See bug 1466406.
        //
        // Bug 1466580 tracks running the Android layout tests on automation.
        //
        // The actual bindgen bug still needs reduction.
        let assigned_nodes: &[structs::RefPtr<structs::nsINode>] = if !cfg!(target_os = "android") {
            debug_assert_eq!(
                unsafe { bindings::Gecko_GetAssignedNodes(self.0) },
                &slot.mAssignedNodes as *const _,
            );

            &*slot.mAssignedNodes
        } else {
            unsafe { &**bindings::Gecko_GetAssignedNodes(self.0) }
        };

        debug_assert_eq!(
            mem::size_of::<structs::RefPtr<structs::nsINode>>(),
            mem::size_of::<Self::ConcreteNode>(),
            "Bad cast!"
        );

        unsafe { mem::transmute(assigned_nodes) }
    }

    #[inline]
    fn shadow_root(&self) -> Option<GeckoShadowRoot<'le>> {
        let slots = self.extended_slots()?;
        unsafe { slots.mShadowRoot.mRawPtr.as_ref().map(GeckoShadowRoot) }
    }

    #[inline]
    fn containing_shadow(&self) -> Option<GeckoShadowRoot<'le>> {
        let slots = self.extended_slots()?;
        unsafe {
            slots
                ._base
                .mContainingShadow
                .mRawPtr
                .as_ref()
                .map(GeckoShadowRoot)
        }
    }

    fn each_anonymous_content_child<F>(&self, mut f: F)
    where
        F: FnMut(Self),
    {
        if !self.may_have_anonymous_children() {
            return;
        }

        let mut array = thin_vec::ThinVec::<*mut nsIContent>::new();

        unsafe { bindings::Gecko_GetAnonymousContentForElement(self.0, &mut array) };

        for content in &array {
            let node = GeckoNode::from_content(unsafe { &**content });
            let element = match node.as_element() {
                Some(e) => e,
                None => continue,
            };

            f(element);
        }
    }

    #[inline]
    fn as_node(&self) -> Self::ConcreteNode {
        unsafe { GeckoNode(&*(self.0 as *const _ as *const RawGeckoNode)) }
    }

    fn owner_doc_matches_for_testing(&self, device: &Device) -> bool {
        self.as_node().owner_doc().0 as *const structs::Document == device.document() as *const _
    }

    fn style_attribute(&self) -> Option<ArcBorrow<Locked<PropertyDeclarationBlock>>> {
        if !self.may_have_style_attribute() {
            return None;
        }

        unsafe {
            let declarations = Gecko_GetStyleAttrDeclarationBlock(self.0).as_ref()?;
            Some(ArcBorrow::from_ref(declarations))
        }
    }

    fn unset_dirty_style_attribute(&self) {
        if !self.may_have_style_attribute() {
            return;
        }

        unsafe { Gecko_UnsetDirtyStyleAttr(self.0) };
    }

    fn smil_override(&self) -> Option<ArcBorrow<Locked<PropertyDeclarationBlock>>> {
        unsafe {
            let slots = self.extended_slots()?;

            let declaration: &structs::DeclarationBlock =
                slots.mSMILOverrideStyleDeclaration.mRawPtr.as_ref()?;

            let raw: &structs::StyleLockedDeclarationBlock = declaration.mRaw.mRawPtr.as_ref()?;
            Some(ArcBorrow::from_ref(raw))
        }
    }

    fn animation_rule(
        &self,
        _: &SharedStyleContext,
    ) -> Option<Arc<Locked<PropertyDeclarationBlock>>> {
        get_animation_rule(self, CascadeLevel::Animations)
    }

    fn transition_rule(
        &self,
        _: &SharedStyleContext,
    ) -> Option<Arc<Locked<PropertyDeclarationBlock>>> {
        get_animation_rule(self, CascadeLevel::Transitions)
    }

    #[inline]
    fn state(&self) -> ElementState {
        ElementState::from_bits_retain(self.state_internal())
    }

    #[inline]
    fn has_part_attr(&self) -> bool {
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementHasPart)
    }

    #[inline]
    fn exports_any_part(&self) -> bool {
        snapshot_helpers::find_attr(self.attrs(), &atom!("exportparts")).is_some()
    }

    // FIXME(emilio): we should probably just return a reference to the Atom.
    #[inline]
    fn id(&self) -> Option<&WeakAtom> {
        if !self.has_id() {
            return None;
        }
        snapshot_helpers::get_id(self.attrs())
    }

    fn each_attr_name<F>(&self, mut callback: F)
    where
        F: FnMut(&AtomIdent),
    {
        for attr in self.attrs() {
            unsafe { AtomIdent::with(attr.mName.name(), |a| callback(a)) }
        }
    }

    fn each_class<F>(&self, callback: F)
    where
        F: FnMut(&AtomIdent),
    {
        let attr = match self.get_class_attr() {
            Some(c) => c,
            None => return,
        };

        snapshot_helpers::each_class_or_part(attr, callback)
    }

    #[inline]
    fn each_custom_state<F>(&self, mut callback: F)
    where
        F: FnMut(&AtomIdent),
    {
        if let Some(slots) = self.extended_slots() {
            unsafe {
                for atom in slots.mCustomStates.iter() {
                    AtomIdent::with(atom.mRawPtr, &mut callback)
                }
            }
        }
    }

    #[inline]
    fn each_exported_part<F>(&self, name: &AtomIdent, callback: F)
    where
        F: FnMut(&AtomIdent),
    {
        snapshot_helpers::each_exported_part(self.attrs(), name, callback)
    }

    fn each_part<F>(&self, callback: F)
    where
        F: FnMut(&AtomIdent),
    {
        let attr = match self.get_part_attr() {
            Some(c) => c,
            None => return,
        };

        snapshot_helpers::each_class_or_part(attr, callback)
    }

    #[inline]
    fn has_snapshot(&self) -> bool {
        self.flags() & ELEMENT_HAS_SNAPSHOT != 0
    }

    #[inline]
    fn handled_snapshot(&self) -> bool {
        self.flags() & ELEMENT_HANDLED_SNAPSHOT != 0
    }

    unsafe fn set_handled_snapshot(&self) {
        debug_assert!(self.has_data());
        self.set_flags(ELEMENT_HANDLED_SNAPSHOT)
    }

    #[inline]
    fn has_dirty_descendants(&self) -> bool {
        self.flags() & ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO != 0
    }

    unsafe fn set_dirty_descendants(&self) {
        debug_assert!(self.has_data());
        debug!("Setting dirty descendants: {:?}", self);
        self.set_flags(ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO)
    }

    unsafe fn unset_dirty_descendants(&self) {
        self.unset_flags(ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO)
    }

    #[inline]
    fn has_animation_only_dirty_descendants(&self) -> bool {
        self.flags() & ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO != 0
    }

    unsafe fn set_animation_only_dirty_descendants(&self) {
        self.set_flags(ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO)
    }

    unsafe fn unset_animation_only_dirty_descendants(&self) {
        self.unset_flags(ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO)
    }

    unsafe fn clear_descendant_bits(&self) {
        self.unset_flags(
            ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO |
                ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO |
                NODE_DESCENDANTS_NEED_FRAMES,
        )
    }

    fn is_visited_link(&self) -> bool {
        self.state().intersects(ElementState::VISITED)
    }

    /// We want to match rules from the same tree in all cases, except for native anonymous content
    /// that _isn't_ part directly of a UA widget (e.g., such generated by form controls, or
    /// pseudo-elements).
    #[inline]
    fn matches_user_and_content_rules(&self) -> bool {
        !self.in_native_anonymous_subtree() || self.in_ua_widget()
    }

    #[inline]
    fn implemented_pseudo_element(&self) -> Option<PseudoElement> {
        if !self.in_native_anonymous_subtree() {
            return None;
        }

        if !self.has_properties() {
            return None;
        }

        let name = unsafe { bindings::Gecko_GetImplementedPseudoIdentifier(self.0) };
        PseudoElement::from_pseudo_type(
            unsafe { bindings::Gecko_GetImplementedPseudoType(self.0) },
            if name.is_null() {
                None
            } else {
                Some(AtomIdent::new(unsafe { Atom::from_raw(name) }))
            },
        )
    }

    #[inline]
    fn store_children_to_process(&self, _: isize) {
        // This is only used for bottom-up traversal, and is thus a no-op for Gecko.
    }

    fn did_process_child(&self) -> isize {
        panic!("Atomic child count not implemented in Gecko");
    }

    unsafe fn ensure_data(&self) -> AtomicRefMut<ElementData> {
        if !self.has_data() {
            debug!("Creating ElementData for {:?}", self);
            let ptr = Box::into_raw(Box::new(AtomicRefCell::new(ElementData::default())));
            self.0.mServoData.set(ptr);
        }
        self.mutate_data().unwrap()
    }

    unsafe fn clear_data(&self) {
        let ptr = self.0.mServoData.get();
        self.unset_flags(
            ELEMENT_HAS_SNAPSHOT |
                ELEMENT_HANDLED_SNAPSHOT |
                structs::Element_kAllServoDescendantBits |
                NODE_NEEDS_FRAME,
        );
        if !ptr.is_null() {
            debug!("Dropping ElementData for {:?}", self);
            let data = Box::from_raw(self.0.mServoData.get());
            self.0.mServoData.set(ptr::null_mut());

            // Perform a mutable borrow of the data in debug builds. This
            // serves as an assertion that there are no outstanding borrows
            // when we destroy the data.
            debug_assert!({
                let _ = data.borrow_mut();
                true
            });
        }
    }

    #[inline]
    fn skip_item_display_fixup(&self) -> bool {
        debug_assert!(
            !self.is_pseudo_element(),
            "Just don't call me if I'm a pseudo, you should know the answer already"
        );
        self.is_root_of_native_anonymous_subtree()
    }

    #[inline]
    fn may_have_animations(&self) -> bool {
        if let Some(pseudo) = self.implemented_pseudo_element() {
            if pseudo.animations_stored_in_parent() {
                // FIXME(emilio): When would the parent of a ::before / ::after
                // pseudo-element be null?
                return self.parent_element().map_or(false, |p| {
                    p.as_node()
                        .get_bool_flag(nsINode_BooleanFlag::ElementHasAnimations)
                });
            }
        }
        self.as_node()
            .get_bool_flag(nsINode_BooleanFlag::ElementHasAnimations)
    }

    /// Update various animation-related state on a given (pseudo-)element as
    /// results of normal restyle.
    fn update_animations(
        &self,
        before_change_style: Option<Arc<ComputedValues>>,
        tasks: UpdateAnimationsTasks,
    ) {
        // We have to update animations even if the element has no computed
        // style since it means the element is in a display:none subtree, we
        // should destroy all CSS animations in display:none subtree.
        let computed_data = self.borrow_data();
        let computed_values = computed_data.as_ref().map(|d| d.styles.primary());
        let before_change_values = before_change_style
            .as_ref()
            .map_or(ptr::null(), |x| x.as_gecko_computed_style());
        let computed_values_opt = computed_values
            .as_ref()
            .map_or(ptr::null(), |x| x.as_gecko_computed_style());
        unsafe {
            Gecko_UpdateAnimations(
                self.0,
                before_change_values,
                computed_values_opt,
                tasks.bits(),
            );
        }
    }

    #[inline]
    fn has_animations(&self, _: &SharedStyleContext) -> bool {
        self.has_any_animation()
    }

    fn has_css_animations(&self, _: &SharedStyleContext, _: Option<PseudoElement>) -> bool {
        self.may_have_animations() && unsafe { Gecko_ElementHasCSSAnimations(self.0) }
    }

    fn has_css_transitions(&self, _: &SharedStyleContext, _: Option<PseudoElement>) -> bool {
        self.may_have_animations() && unsafe { Gecko_ElementHasCSSTransitions(self.0) }
    }

    // Detect if there are any changes that require us to update transitions.
    //
    // This is used as a more thoroughgoing check than the cheaper
    // might_need_transitions_update check.
    //
    // The following logic shadows the logic used on the Gecko side
    // (nsTransitionManager::DoUpdateTransitions) where we actually perform the
    // update.
    //
    // https://drafts.csswg.org/css-transitions/#starting
    fn needs_transitions_update(
        &self,
        before_change_style: &ComputedValues,
        after_change_style: &ComputedValues,
    ) -> bool {
        let after_change_ui_style = after_change_style.get_ui();
        let existing_transitions = self.css_transitions_info();

        if after_change_style.get_box().clone_display().is_none() {
            // We need to cancel existing transitions.
            return !existing_transitions.is_empty();
        }

        let mut transitions_to_keep = PropertyDeclarationIdSet::default();
        for transition_property in after_change_style.transition_properties() {
            let physical_property = transition_property
                .property
                .as_borrowed()
                .to_physical(after_change_style.writing_mode);
            transitions_to_keep.insert(physical_property);
            if self.needs_transitions_update_per_property(
                physical_property,
                after_change_ui_style
                    .transition_combined_duration_at(transition_property.index)
                    .seconds(),
                before_change_style,
                after_change_style,
                &existing_transitions,
            ) {
                return true;
            }
        }

        // Check if we have to cancel the running transition because this is not
        // a matching transition-property value.
        existing_transitions
            .keys()
            .any(|property| !transitions_to_keep.contains(property.as_borrowed()))
    }

    /// Whether there is an ElementData container.
    #[inline]
    fn has_data(&self) -> bool {
        self.get_data().is_some()
    }

    /// Immutably borrows the ElementData.
    fn borrow_data(&self) -> Option<AtomicRef<ElementData>> {
        self.get_data().map(|x| x.borrow())
    }

    /// Mutably borrows the ElementData.
    fn mutate_data(&self) -> Option<AtomicRefMut<ElementData>> {
        self.get_data().map(|x| x.borrow_mut())
    }

    #[inline]
    fn lang_attr(&self) -> Option<AttrValue> {
        let ptr = unsafe { bindings::Gecko_LangValue(self.0) };
        if ptr.is_null() {
            None
        } else {
            Some(AtomString(unsafe { Atom::from_addrefed(ptr) }))
        }
    }

    fn match_element_lang(&self, override_lang: Option<Option<AttrValue>>, value: &Lang) -> bool {
        // Gecko supports :lang() from CSS Selectors 4, which accepts a list
        // of language tags, and does BCP47-style range matching.
        let override_lang_ptr = match override_lang {
            Some(Some(ref atom)) => atom.as_ptr(),
            _ => ptr::null_mut(),
        };
        value.0.iter().any(|lang| unsafe {
            Gecko_MatchLang(
                self.0,
                override_lang_ptr,
                override_lang.is_some(),
                lang.as_slice().as_ptr(),
            )
        })
    }

    fn is_html_document_body_element(&self) -> bool {
        if self.local_name() != &**local_name!("body") {
            return false;
        }

        if !self.is_html_element() {
            return false;
        }

        unsafe { bindings::Gecko_IsDocumentBody(self.0) }
    }

    fn synthesize_view_transition_dynamic_rules<V>(&self, rules: &mut V)
    where
        V: Push<ApplicableDeclarationBlock>,
    {
        use crate::stylesheets::layer_rule::LayerOrder;
        let declarations =
            unsafe { bindings::Gecko_GetViewTransitionDynamicRule(self.0).as_ref() };
        if let Some(decl) = declarations {
            rules.push(ApplicableDeclarationBlock::from_declarations(
                unsafe { Arc::from_raw_addrefed(decl) },
                ServoCascadeLevel::UANormal,
                LayerOrder::root(),
            ));
        }
    }

    fn synthesize_presentational_hints_for_legacy_attributes<V>(
        &self,
        visited_handling: VisitedHandlingMode,
        hints: &mut V,
    ) where
        V: Push<ApplicableDeclarationBlock>,
    {
        use crate::properties::longhands::_x_lang::SpecifiedValue as SpecifiedLang;
        use crate::properties::longhands::color::SpecifiedValue as SpecifiedColor;
        use crate::stylesheets::layer_rule::LayerOrder;
        use crate::values::specified::{color::Color, font::XTextScale};
        lazy_static! {
            static ref TABLE_COLOR_RULE: ApplicableDeclarationBlock = {
                let global_style_data = &*GLOBAL_STYLE_DATA;
                let pdb = PropertyDeclarationBlock::with_one(
                    PropertyDeclaration::Color(SpecifiedColor(Color::InheritFromBodyQuirk.into())),
                    Importance::Normal,
                );
                let arc = Arc::new_leaked(global_style_data.shared_lock.wrap(pdb));
                ApplicableDeclarationBlock::from_declarations(
                    arc,
                    ServoCascadeLevel::PresHints,
                    LayerOrder::root(),
                )
            };
            static ref MATHML_LANG_RULE: ApplicableDeclarationBlock = {
                let global_style_data = &*GLOBAL_STYLE_DATA;
                let pdb = PropertyDeclarationBlock::with_one(
                    PropertyDeclaration::XLang(SpecifiedLang(atom!("x-math"))),
                    Importance::Normal,
                );
                let arc = Arc::new_leaked(global_style_data.shared_lock.wrap(pdb));
                ApplicableDeclarationBlock::from_declarations(
                    arc,
                    ServoCascadeLevel::PresHints,
                    LayerOrder::root(),
                )
            };
            static ref SVG_TEXT_DISABLE_SCALE_RULE: ApplicableDeclarationBlock = {
                let global_style_data = &*GLOBAL_STYLE_DATA;
                let pdb = PropertyDeclarationBlock::with_one(
                    PropertyDeclaration::XTextScale(XTextScale::None),
                    Importance::Normal,
                );
                let arc = Arc::new_leaked(global_style_data.shared_lock.wrap(pdb));
                ApplicableDeclarationBlock::from_declarations(
                    arc,
                    ServoCascadeLevel::PresHints,
                    LayerOrder::root(),
                )
            };
        };

        let ns = self.namespace_id();
        // <th> elements get a default MozCenterOrInherit which may get overridden
        if ns == structs::kNameSpaceID_XHTML as i32 {
            if self.local_name().as_ptr() == atom!("table").as_ptr() &&
                self.as_node().owner_doc().quirks_mode() == QuirksMode::Quirks
            {
                hints.push(TABLE_COLOR_RULE.clone());
            }
        }
        if ns == structs::kNameSpaceID_SVG as i32 {
            if self.local_name().as_ptr() == atom!("text").as_ptr() {
                hints.push(SVG_TEXT_DISABLE_SCALE_RULE.clone());
            }
        }
        let declarations =
            unsafe { Gecko_GetHTMLPresentationAttrDeclarationBlock(self.0).as_ref() };
        if let Some(decl) = declarations {
            hints.push(ApplicableDeclarationBlock::from_declarations(
                unsafe { Arc::from_raw_addrefed(decl) },
                ServoCascadeLevel::PresHints,
                LayerOrder::root(),
            ));
        }
        let declarations = unsafe { Gecko_GetExtraContentStyleDeclarations(self.0).as_ref() };
        if let Some(decl) = declarations {
            hints.push(ApplicableDeclarationBlock::from_declarations(
                unsafe { Arc::from_raw_addrefed(decl) },
                ServoCascadeLevel::PresHints,
                LayerOrder::root(),
            ));
        }

        // Support for link, vlink, and alink presentation hints on <body>
        if self.is_link() {
            // Unvisited vs. visited styles are computed up-front based on the
            // visited mode (not the element's actual state).
            let declarations = match visited_handling {
                VisitedHandlingMode::AllLinksVisitedAndUnvisited => {
                    unreachable!(
                        "We should never try to selector match with \
                         AllLinksVisitedAndUnvisited"
                    );
                },
                VisitedHandlingMode::AllLinksUnvisited => unsafe {
                    Gecko_GetUnvisitedLinkAttrDeclarationBlock(self.0).as_ref()
                },
                VisitedHandlingMode::RelevantLinkVisited => unsafe {
                    Gecko_GetVisitedLinkAttrDeclarationBlock(self.0).as_ref()
                },
            };
            if let Some(decl) = declarations {
                hints.push(ApplicableDeclarationBlock::from_declarations(
                    unsafe { Arc::from_raw_addrefed(decl) },
                    ServoCascadeLevel::PresHints,
                    LayerOrder::root(),
                ));
            }

            let active = self
                .state()
                .intersects(NonTSPseudoClass::Active.state_flag());
            if active {
                let declarations =
                    unsafe { Gecko_GetActiveLinkAttrDeclarationBlock(self.0).as_ref() };
                if let Some(decl) = declarations {
                    hints.push(ApplicableDeclarationBlock::from_declarations(
                        unsafe { Arc::from_raw_addrefed(decl) },
                        ServoCascadeLevel::PresHints,
                        LayerOrder::root(),
                    ));
                }
            }
        }

        // xml:lang has precedence over lang, which can be
        // set by Gecko_GetHTMLPresentationAttrDeclarationBlock
        //
        // http://www.whatwg.org/specs/web-apps/current-work/multipage/elements.html#language
        let ptr = unsafe { bindings::Gecko_GetXMLLangValue(self.0) };
        if !ptr.is_null() {
            let global_style_data = &*GLOBAL_STYLE_DATA;

            let pdb = PropertyDeclarationBlock::with_one(
                PropertyDeclaration::XLang(SpecifiedLang(unsafe { Atom::from_addrefed(ptr) })),
                Importance::Normal,
            );
            let arc = Arc::new(global_style_data.shared_lock.wrap(pdb));
            hints.push(ApplicableDeclarationBlock::from_declarations(
                arc,
                ServoCascadeLevel::PresHints,
                LayerOrder::root(),
            ))
        }
        // MathML's default lang has precedence over both `lang` and `xml:lang`
        if ns == structs::kNameSpaceID_MathML as i32 {
            if self.local_name().as_ptr() == atom!("math").as_ptr() {
                hints.push(MATHML_LANG_RULE.clone());
            }
        }
    }

    fn has_selector_flags(&self, flags: ElementSelectorFlags) -> bool {
        let node_flags = selector_flags_to_node_flags(flags);
        self.as_node().selector_flags() & node_flags == node_flags
    }

    fn relative_selector_search_direction(&self) -> ElementSelectorFlags {
        use crate::gecko_bindings::structs::NodeSelectorFlags;
        let flags = self.as_node().selector_flags();
        if (flags & NodeSelectorFlags::RelativeSelectorSearchDirectionAncestorSibling.0) != 0 {
            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR_SIBLING
        } else if (flags & NodeSelectorFlags::RelativeSelectorSearchDirectionAncestor.0) != 0 {
            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR
        } else if (flags & NodeSelectorFlags::RelativeSelectorSearchDirectionSibling.0) != 0 {
            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING
        } else {
            ElementSelectorFlags::empty()
        }
    }
}

impl<'le> PartialEq for GeckoElement<'le> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.0 as *const _ == other.0 as *const _
    }
}

impl<'le> Eq for GeckoElement<'le> {}

impl<'le> Hash for GeckoElement<'le> {
    #[inline]
    fn hash<H: Hasher>(&self, state: &mut H) {
        (self.0 as *const RawGeckoElement).hash(state);
    }
}

impl<'le> ::selectors::Element for GeckoElement<'le> {
    type Impl = SelectorImpl;

    #[inline]
    fn opaque(&self) -> OpaqueElement {
        OpaqueElement::new(self.0)
    }

    #[inline]
    fn parent_element(&self) -> Option<Self> {
        let parent_node = self.as_node().parent_node();
        parent_node.and_then(|n| n.as_element())
    }

    #[inline]
    fn parent_node_is_shadow_root(&self) -> bool {
        self.as_node()
            .parent_node()
            .map_or(false, |p| p.is_shadow_root())
    }

    #[inline]
    fn containing_shadow_host(&self) -> Option<Self> {
        let shadow = self.containing_shadow()?;
        Some(shadow.host())
    }

    #[inline]
    fn is_pseudo_element(&self) -> bool {
        self.implemented_pseudo_element().is_some()
    }

    #[inline]
    fn pseudo_element_originating_element(&self) -> Option<Self> {
        debug_assert!(self.is_pseudo_element());
        debug_assert!(self.in_native_anonymous_subtree());
        if self.in_ua_widget() {
            return self.containing_shadow_host();
        }
        let mut current = *self;
        loop {
            let anon_root = current.is_root_of_native_anonymous_subtree();
            current = current.traversal_parent()?;
            if anon_root {
                return Some(current);
            }
        }
    }

    #[inline]
    fn assigned_slot(&self) -> Option<Self> {
        let slot = self.extended_slots()?._base.mAssignedSlot.mRawPtr;

        unsafe { Some(GeckoElement(&slot.as_ref()?._base._base._base)) }
    }

    #[inline]
    fn prev_sibling_element(&self) -> Option<Self> {
        let mut sibling = self.as_node().prev_sibling();
        while let Some(sibling_node) = sibling {
            if let Some(el) = sibling_node.as_element() {
                return Some(el);
            }
            sibling = sibling_node.prev_sibling();
        }
        None
    }

    #[inline]
    fn next_sibling_element(&self) -> Option<Self> {
        let mut sibling = self.as_node().next_sibling();
        while let Some(sibling_node) = sibling {
            if let Some(el) = sibling_node.as_element() {
                return Some(el);
            }
            sibling = sibling_node.next_sibling();
        }
        None
    }

    #[inline]
    fn first_element_child(&self) -> Option<Self> {
        let mut child = self.as_node().first_child();
        while let Some(child_node) = child {
            if let Some(el) = child_node.as_element() {
                return Some(el);
            }
            child = child_node.next_sibling();
        }
        None
    }

    fn apply_selector_flags(&self, flags: ElementSelectorFlags) {
        // Handle flags that apply to the element.
        let self_flags = flags.for_self();
        if !self_flags.is_empty() {
            self.as_node()
                .set_selector_flags(selector_flags_to_node_flags(flags))
        }

        // Handle flags that apply to the parent.
        let parent_flags = flags.for_parent();
        if !parent_flags.is_empty() {
            if let Some(p) = self.as_node().parent_node() {
                if p.is_element() || p.is_shadow_root() {
                    p.set_selector_flags(selector_flags_to_node_flags(parent_flags));
                }
            }
        }
    }

    fn has_attr_in_no_namespace(&self, local_name: &LocalName) -> bool {
        for attr in self.attrs() {
            if attr.mName.mBits == local_name.as_ptr() as usize {
                return true;
            }
        }
        false
    }

    fn attr_matches(
        &self,
        ns: &NamespaceConstraint<&Namespace>,
        local_name: &LocalName,
        operation: &AttrSelectorOperation<&AttrValue>,
    ) -> bool {
        snapshot_helpers::attr_matches(self.attrs(), ns, local_name, operation)
    }

    #[inline]
    fn is_root(&self) -> bool {
        if self
            .as_node()
            .get_bool_flag(nsINode_BooleanFlag::ParentIsContent)
        {
            return false;
        }

        if !self.as_node().is_in_document() {
            return false;
        }

        debug_assert!(self
            .as_node()
            .parent_node()
            .map_or(false, |p| p.is_document()));
        // XXX this should always return true at this point, shouldn't it?
        unsafe { bindings::Gecko_IsRootElement(self.0) }
    }

    fn is_empty(&self) -> bool {
        !self
            .as_node()
            .dom_children()
            .any(|child| unsafe { Gecko_IsSignificantChild(child.0, true) })
    }

    #[inline]
    fn has_local_name(&self, name: &WeakAtom) -> bool {
        self.local_name() == name
    }

    #[inline]
    fn has_namespace(&self, ns: &WeakNamespace) -> bool {
        self.namespace() == ns
    }

    #[inline]
    fn is_same_type(&self, other: &Self) -> bool {
        self.local_name() == other.local_name() && self.namespace() == other.namespace()
    }

    fn match_non_ts_pseudo_class(
        &self,
        pseudo_class: &NonTSPseudoClass,
        context: &mut MatchingContext<Self::Impl>,
    ) -> bool {
        use selectors::matching::*;
        match *pseudo_class {
            NonTSPseudoClass::Autofill |
            NonTSPseudoClass::Defined |
            NonTSPseudoClass::Focus |
            NonTSPseudoClass::Enabled |
            NonTSPseudoClass::Disabled |
            NonTSPseudoClass::Checked |
            NonTSPseudoClass::Fullscreen |
            NonTSPseudoClass::Indeterminate |
            NonTSPseudoClass::MozInert |
            NonTSPseudoClass::PopoverOpen |
            NonTSPseudoClass::PlaceholderShown |
            NonTSPseudoClass::Target |
            NonTSPseudoClass::Valid |
            NonTSPseudoClass::Invalid |
            NonTSPseudoClass::MozBroken |
            NonTSPseudoClass::Required |
            NonTSPseudoClass::Optional |
            NonTSPseudoClass::ReadOnly |
            NonTSPseudoClass::ReadWrite |
            NonTSPseudoClass::FocusWithin |
            NonTSPseudoClass::FocusVisible |
            NonTSPseudoClass::MozDragOver |
            NonTSPseudoClass::MozDevtoolsHighlighted |
            NonTSPseudoClass::MozStyleeditorTransitioning |
            NonTSPseudoClass::MozMathIncrementScriptLevel |
            NonTSPseudoClass::InRange |
            NonTSPseudoClass::OutOfRange |
            NonTSPseudoClass::Default |
            NonTSPseudoClass::UserValid |
            NonTSPseudoClass::UserInvalid |
            NonTSPseudoClass::MozMeterOptimum |
            NonTSPseudoClass::MozMeterSubOptimum |
            NonTSPseudoClass::MozMeterSubSubOptimum |
            NonTSPseudoClass::MozHasDirAttr |
            NonTSPseudoClass::MozDirAttrLTR |
            NonTSPseudoClass::MozDirAttrRTL |
            NonTSPseudoClass::MozDirAttrLikeAuto |
            NonTSPseudoClass::Modal |
            NonTSPseudoClass::MozTopmostModal |
            NonTSPseudoClass::Open |
            NonTSPseudoClass::Active |
            NonTSPseudoClass::Hover |
            NonTSPseudoClass::HasSlotted |
            NonTSPseudoClass::MozAutofillPreview |
            NonTSPseudoClass::MozRevealed |
            NonTSPseudoClass::ActiveViewTransition |
            NonTSPseudoClass::MozValueEmpty |
            NonTSPseudoClass::MozSuppressForPrintSelection => self.state().intersects(pseudo_class.state_flag()),
            NonTSPseudoClass::Dir(ref dir) => self.state().intersects(dir.element_state()),
            NonTSPseudoClass::AnyLink => self.is_link(),
            NonTSPseudoClass::Link => {
                self.is_link() && context.visited_handling().matches_unvisited()
            },
            NonTSPseudoClass::CustomState(ref state) => self.has_custom_state(&state.0),
            NonTSPseudoClass::Visited => {
                self.is_link() && context.visited_handling().matches_visited()
            },
            NonTSPseudoClass::MozFirstNode => {
                if context.needs_selector_flags() {
                    self.apply_selector_flags(ElementSelectorFlags::HAS_EDGE_CHILD_SELECTOR);
                }
                let mut elem = self.as_node();
                while let Some(prev) = elem.prev_sibling() {
                    if prev.contains_non_whitespace_content() {
                        return false;
                    }
                    elem = prev;
                }
                true
            },
            NonTSPseudoClass::MozLastNode => {
                if context.needs_selector_flags() {
                    self.apply_selector_flags(ElementSelectorFlags::HAS_EDGE_CHILD_SELECTOR);
                }
                let mut elem = self.as_node();
                while let Some(next) = elem.next_sibling() {
                    if next.contains_non_whitespace_content() {
                        return false;
                    }
                    elem = next;
                }
                true
            },
            NonTSPseudoClass::MozOnlyWhitespace => {
                if context.needs_selector_flags() {
                    self.apply_selector_flags(ElementSelectorFlags::HAS_EMPTY_SELECTOR);
                }
                if self
                    .as_node()
                    .dom_children()
                    .any(|c| c.contains_non_whitespace_content())
                {
                    return false;
                }
                true
            },
            NonTSPseudoClass::MozNativeAnonymous => !self.matches_user_and_content_rules(),
            NonTSPseudoClass::MozTableBorderNonzero => unsafe {
                bindings::Gecko_IsTableBorderNonzero(self.0)
            },
            NonTSPseudoClass::MozSelectListBox => unsafe {
                bindings::Gecko_IsSelectListBox(self.0)
            },
            NonTSPseudoClass::MozIsHTML => self.as_node().owner_doc().is_html_document(),
            NonTSPseudoClass::MozLocaleDir(..) | NonTSPseudoClass::MozWindowInactive => {
                let state_bit = pseudo_class.document_state_flag();
                if state_bit.is_empty() {
                    debug_assert!(
                        matches!(pseudo_class, NonTSPseudoClass::MozLocaleDir(..)),
                        "Only moz-locale-dir should ever return an empty state"
                    );
                    return false;
                }
                if context
                    .extra_data
                    .invalidation_data
                    .document_state
                    .intersects(state_bit)
                {
                    return !context.in_negation();
                }
                self.document_state().contains(state_bit)
            },
            NonTSPseudoClass::MozPlaceholder => false,
            NonTSPseudoClass::Lang(ref lang_arg) => self.match_element_lang(None, lang_arg),
            NonTSPseudoClass::Heading(ref levels) => levels.matches_state(self.state()),
        }
    }

    fn match_pseudo_element(
        &self,
        pseudo_selector: &PseudoElement,
        _context: &mut MatchingContext<Self::Impl>,
    ) -> bool {
        // TODO(emilio): I believe we could assert we are a pseudo-element and
        // match the proper pseudo-element, given how we rulehash the stuff
        // based on the pseudo.
        let pseudo = match self.implemented_pseudo_element() {
            Some(pseudo) => pseudo,
            None => return false,
        };

        pseudo.matches(pseudo_selector, self)
    }

    #[inline]
    fn is_link(&self) -> bool {
        self.state().intersects(ElementState::VISITED_OR_UNVISITED)
    }

    #[inline]
    fn has_id(&self, id: &AtomIdent, case_sensitivity: CaseSensitivity) -> bool {
        if !self.has_id() {
            return false;
        }

        let element_id = match snapshot_helpers::get_id(self.attrs()) {
            Some(id) => id,
            None => return false,
        };

        case_sensitivity.eq_atom(element_id, id)
    }

    #[inline]
    fn is_part(&self, name: &AtomIdent) -> bool {
        let attr = match self.get_part_attr() {
            Some(c) => c,
            None => return false,
        };

        snapshot_helpers::has_class_or_part(name, CaseSensitivity::CaseSensitive, attr)
    }

    #[inline]
    fn imported_part(&self, name: &AtomIdent) -> Option<AtomIdent> {
        snapshot_helpers::imported_part(self.attrs(), name)
    }

    #[inline(always)]
    fn has_class(&self, name: &AtomIdent, case_sensitivity: CaseSensitivity) -> bool {
        let attr = match self.get_class_attr() {
            Some(c) => c,
            None => return false,
        };

        snapshot_helpers::has_class_or_part(name, case_sensitivity, attr)
    }

    #[inline]
    fn has_custom_state(&self, state: &AtomIdent) -> bool {
        if !self.is_html_element() {
            return false;
        }
        let check_state_ptr: *const nsAtom = state.as_ptr();
        self.extended_slots().map_or(false, |slot| {
            (&slot.mCustomStates).iter().any(|setstate| {
                let setstate_ptr: *const nsAtom = setstate.mRawPtr;
                setstate_ptr == check_state_ptr
            })
        })
    }

    #[inline]
    fn is_html_element_in_html_document(&self) -> bool {
        self.is_html_element() && self.as_node().owner_doc().is_html_document()
    }

    #[inline]
    fn is_html_slot_element(&self) -> bool {
        self.is_html_element() && self.local_name().as_ptr() == local_name!("slot").as_ptr()
    }

    #[inline]
    fn ignores_nth_child_selectors(&self) -> bool {
        self.is_root_of_native_anonymous_subtree()
    }

    fn add_element_unique_hashes(&self, filter: &mut BloomFilter) -> bool {
        each_relevant_element_hash(*self, |hash| filter.insert_hash(hash & BLOOM_HASH_MASK));
        true
    }
}
