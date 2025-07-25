/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{AlphaType, ClipMode, ImageBufferKind};
use api::{FontInstanceFlags, YuvColorSpace, YuvFormat, ColorDepth, ColorRange, PremultipliedColorF};
use api::units::*;
use crate::clip::{ClipNodeFlags, ClipNodeRange, ClipItemKind, ClipStore};
use crate::command_buffer::PrimitiveCommand;
use crate::composite::CompositorSurfaceKind;
use crate::pattern::PatternKind;
use crate::spatial_tree::{SpatialTree, SpatialNodeIndex, CoordinateSystemId};
use glyph_rasterizer::{GlyphFormat, SubpixelDirection};
use crate::gpu_cache::{GpuBlockData, GpuCache, GpuCacheAddress};
use crate::gpu_types::{BrushFlags, BrushInstance, PrimitiveHeaders, ZBufferId, ZBufferIdGenerator};
use crate::gpu_types::SplitCompositeInstance;
use crate::gpu_types::{PrimitiveInstanceData, RasterizationSpace, GlyphInstance};
use crate::gpu_types::{PrimitiveHeader, PrimitiveHeaderIndex, TransformPaletteId, TransformPalette};
use crate::gpu_types::{ImageBrushData, get_shader_opacity, BoxShadowData, MaskInstance};
use crate::gpu_types::{ClipMaskInstanceCommon, ClipMaskInstanceRect, ClipMaskInstanceBoxShadow};
use crate::internal_types::{FastHashMap, Filter, FrameAllocator, FrameMemory, FrameVec, Swizzle, TextureSource};
use crate::picture::{Picture3DContext, PictureCompositeMode, calculate_screen_uv};
use crate::prim_store::{PrimitiveInstanceKind, ClipData};
use crate::prim_store::{PrimitiveInstance, PrimitiveOpacity, SegmentInstanceIndex};
use crate::prim_store::{BrushSegment, ClipMaskKind, ClipTaskIndex};
use crate::prim_store::VECS_PER_SEGMENT;
use crate::quad;
use crate::render_target::RenderTargetContext;
use crate::render_task_graph::{RenderTaskId, RenderTaskGraph};
use crate::render_task::{RenderTaskAddress, RenderTaskKind, SubPass};
use crate::renderer::{BlendMode, GpuBufferBuilder, ShaderColorMode};
use crate::renderer::MAX_VERTEX_TEXTURE_WIDTH;
use crate::resource_cache::{GlyphFetchResult, ImageProperties};
use crate::space::SpaceMapper;
use crate::visibility::{PrimitiveVisibilityFlags, VisibilityState};
use smallvec::SmallVec;
use std::{f32, i32, usize};
use crate::util::{project_rect, MaxRect, TransformedRectKind, ScaleOffset};
use crate::segment::EdgeAaSegmentMask;

// Special sentinel value recognized by the shader. It is considered to be
// a dummy task that doesn't mask out anything.
const OPAQUE_TASK_ADDRESS: RenderTaskAddress = RenderTaskAddress(0x7fffffff);

/// Used to signal there are no segments provided with this primitive.
pub const INVALID_SEGMENT_INDEX: i32 = 0xffff;

/// Size in device pixels for tiles that clip masks are drawn in.
const CLIP_RECTANGLE_TILE_SIZE: i32 = 128;

/// The minimum size of a clip mask before trying to draw in tiles.
const CLIP_RECTANGLE_AREA_THRESHOLD: f32 = (CLIP_RECTANGLE_TILE_SIZE * CLIP_RECTANGLE_TILE_SIZE * 4) as f32;

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BrushBatchKind {
    Solid,
    Image(ImageBufferKind),
    Blend,
    MixBlend {
        task_id: RenderTaskId,
        backdrop_id: RenderTaskId,
    },
    YuvImage(ImageBufferKind, YuvFormat, ColorDepth, YuvColorSpace, ColorRange),
    LinearGradient,
    Opacity,
}

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BatchKind {
    SplitComposite,
    TextRun(GlyphFormat),
    Brush(BrushBatchKind),
    Quad(PatternKind),
}

/// Input textures for a primitive, without consideration of clip mask
#[derive(Copy, Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TextureSet {
    pub colors: [TextureSource; 3],
}

impl TextureSet {
    const UNTEXTURED: TextureSet = TextureSet {
        colors: [
            TextureSource::Invalid,
            TextureSource::Invalid,
            TextureSource::Invalid,
        ],
    };

    /// A textured primitive
    fn prim_textured(
        color: TextureSource,
    ) -> Self {
        TextureSet {
            colors: [
                color,
                TextureSource::Invalid,
                TextureSource::Invalid,
            ],
        }
    }

    fn is_compatible_with(&self, other: &TextureSet) -> bool {
        self.colors[0].is_compatible(&other.colors[0]) &&
        self.colors[1].is_compatible(&other.colors[1]) &&
        self.colors[2].is_compatible(&other.colors[2])
    }
}

impl TextureSource {
    fn combine(&self, other: TextureSource) -> TextureSource {
        if other == TextureSource::Invalid {
            *self
        } else {
            other
        }
    }
}

/// Optional textures that can be used as a source in the shaders.
/// Textures that are not used by the batch are equal to TextureId::invalid().
#[derive(Copy, Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BatchTextures {
    pub input: TextureSet,
    pub clip_mask: TextureSource,
}

impl BatchTextures {
    /// An empty batch textures (no binding slots set)
    pub fn empty() -> BatchTextures {
        BatchTextures {
            input: TextureSet::UNTEXTURED,
            clip_mask: TextureSource::Invalid,
        }
    }

    /// A textured primitive with optional clip mask
    pub fn prim_textured(
        color: TextureSource,
        clip_mask: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet::prim_textured(color),
            clip_mask,
        }
    }

    /// An untextured primitive with optional clip mask
    pub fn prim_untextured(
        clip_mask: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet::UNTEXTURED,
            clip_mask,
        }
    }

    /// A composite style effect with single input texture
    pub fn composite_rgb(
        texture: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet {
                colors: [
                    texture,
                    TextureSource::Invalid,
                    TextureSource::Invalid,
                ],
            },
            clip_mask: TextureSource::Invalid,
        }
    }

    /// A composite style effect with up to 3 input textures
    pub fn composite_yuv(
        color0: TextureSource,
        color1: TextureSource,
        color2: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet {
                colors: [color0, color1, color2],
            },
            clip_mask: TextureSource::Invalid,
        }
    }

    pub fn is_compatible_with(&self, other: &BatchTextures) -> bool {
        if !self.clip_mask.is_compatible(&other.clip_mask) {
            return false;
        }

        self.input.is_compatible_with(&other.input)
    }

    pub fn combine_textures(&self, other: BatchTextures) -> Option<BatchTextures> {
        if !self.is_compatible_with(&other) {
            return None;
        }

        let mut new_textures = BatchTextures::empty();

        new_textures.clip_mask = self.clip_mask.combine(other.clip_mask);

        for i in 0 .. 3 {
            new_textures.input.colors[i] = self.input.colors[i].combine(other.input.colors[i]);
        }

        Some(new_textures)
    }

    fn merge(&mut self, other: &BatchTextures) {
        self.clip_mask = self.clip_mask.combine(other.clip_mask);

        for (s, o) in self.input.colors.iter_mut().zip(other.input.colors.iter()) {
            *s = s.combine(*o);
        }
    }
}

#[derive(Copy, Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BatchKey {
    pub kind: BatchKind,
    pub blend_mode: BlendMode,
    pub textures: BatchTextures,
}

impl BatchKey {
    pub fn new(kind: BatchKind, blend_mode: BlendMode, textures: BatchTextures) -> Self {
        BatchKey {
            kind,
            blend_mode,
            textures,
        }
    }

    pub fn is_compatible_with(&self, other: &BatchKey) -> bool {
        self.kind == other.kind && self.blend_mode == other.blend_mode && self.textures.is_compatible_with(&other.textures)
    }
}

pub struct BatchRects {
    /// Union of all of the batch's item rects.
    ///
    /// Very often we can skip iterating over item rects by testing against
    /// this one first.
    batch: PictureRect,
    /// When the batch rectangle above isn't a good enough approximation, we
    /// store per item rects.
    items: Option<FrameVec<PictureRect>>,
    // TODO: batch rects don't need to be part of the frame but they currently
    // are. It may be cleaner to remove them from the frame's final data structure
    // and not use the frame's allocator.
    allocator: FrameAllocator,
}

impl BatchRects {
    fn new(allocator: FrameAllocator) -> Self {
        BatchRects {
            batch: PictureRect::zero(),
            items: None,
            allocator,
        }
    }

    #[inline]
    fn add_rect(&mut self, rect: &PictureRect) {
        let union = self.batch.union(rect);
        // If we have already started storing per-item rects, continue doing so.
        // Otherwise, check whether only storing the batch rect is a good enough
        // approximation.
        if let Some(items) = &mut self.items {
            items.push(*rect);
        } else if self.batch.area() + rect.area() < union.area() {
            let mut items = self.allocator.clone().new_vec_with_capacity(16);
            items.push(self.batch);
            items.push(*rect);
            self.items = Some(items);
        }

        self.batch = union;
    }

    #[inline]
    fn intersects(&mut self, rect: &PictureRect) -> bool {
        if !self.batch.intersects(rect) {
            return false;
        }

        if let Some(items) = &self.items {
            items.iter().any(|item| item.intersects(rect))
        } else {
            // If we don't have per-item rects it means the batch rect is a good
            // enough approximation and we didn't bother storing per-rect items.
            true
        }
    }
}


pub struct AlphaBatchList {
    pub batches: FrameVec<PrimitiveBatch>,
    pub batch_rects: FrameVec<BatchRects>,
    current_batch_index: usize,
    current_z_id: ZBufferId,
    break_advanced_blend_batches: bool,
}

impl AlphaBatchList {
    fn new(break_advanced_blend_batches: bool, preallocate: usize, memory: &FrameMemory) -> Self {
        AlphaBatchList {
            batches: memory.new_vec_with_capacity(preallocate),
            batch_rects: memory.new_vec_with_capacity(preallocate),
            current_z_id: ZBufferId::invalid(),
            current_batch_index: usize::MAX,
            break_advanced_blend_batches,
        }
    }

    /// Clear all current batches in this list. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.current_batch_index = usize::MAX;
        self.current_z_id = ZBufferId::invalid();
        self.batches.clear();
        self.batch_rects.clear();
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        // The bounding box of everything at this Z plane. We expect potentially
        // multiple primitive segments coming with the same `z_id`.
        z_bounding_rect: &PictureRect,
        z_id: ZBufferId,
    ) -> &mut FrameVec<PrimitiveInstanceData> {
        if z_id != self.current_z_id ||
           self.current_batch_index == usize::MAX ||
           !self.batches[self.current_batch_index].key.is_compatible_with(&key)
        {
            let mut selected_batch_index = None;

            match key.blend_mode {
                BlendMode::Advanced(_) if self.break_advanced_blend_batches => {
                    // don't try to find a batch
                }
                _ => {
                    for (batch_index, batch) in self.batches.iter().enumerate().rev() {
                        // For normal batches, we only need to check for overlaps for batches
                        // other than the first batch we consider. If the first batch
                        // is compatible, then we know there isn't any potential overlap
                        // issues to worry about.
                        if batch.key.is_compatible_with(&key) {
                            selected_batch_index = Some(batch_index);
                            break;
                        }

                        // check for intersections
                        if self.batch_rects[batch_index].intersects(z_bounding_rect) {
                            break;
                        }
                    }
                }
            }

            if selected_batch_index.is_none() {
                // Text runs tend to have a lot of instances per batch, causing a lot of reallocation
                // churn as items are added one by one, so we give it a head start. Ideally we'd start
                // with a larger number, closer to 1k but in some bad cases with lots of batch break
                // we would be wasting a lot of memory.
                // Generally it is safe to preallocate small-ish values for other batch kinds because
                // the items are small and there are no zero-sized batches so there will always be
                // at least one allocation.
                let prealloc = match key.kind {
                    BatchKind::TextRun(..) => 128,
                    _ => 16,
                };
                let mut new_batch = PrimitiveBatch::new(key, self.batches.allocator().clone());
                new_batch.instances.reserve(prealloc);
                selected_batch_index = Some(self.batches.len());
                self.batches.push(new_batch);
                self.batch_rects.push(BatchRects::new(self.batches.allocator().clone()));
            }

            self.current_batch_index = selected_batch_index.unwrap();
            self.batch_rects[self.current_batch_index].add_rect(z_bounding_rect);
            self.current_z_id = z_id;
        }

        let batch = &mut self.batches[self.current_batch_index];
        batch.features |= features;
        batch.key.textures.merge(&key.textures);

        &mut batch.instances
    }
}

pub struct OpaqueBatchList {
    pub pixel_area_threshold_for_new_batch: f32,
    pub batches: FrameVec<PrimitiveBatch>,
    pub current_batch_index: usize,
    lookback_count: usize,
}

impl OpaqueBatchList {
    fn new(pixel_area_threshold_for_new_batch: f32, lookback_count: usize, memory: &FrameMemory) -> Self {
        OpaqueBatchList {
            batches: memory.new_vec(),
            pixel_area_threshold_for_new_batch,
            current_batch_index: usize::MAX,
            lookback_count,
        }
    }

    /// Clear all current batches in this list. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.current_batch_index = usize::MAX;
        self.batches.clear();
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        // The bounding box of everything at the current Z, whatever it is. We expect potentially
        // multiple primitive segments produced by a primitive, which we allow to check
        // `current_batch_index` instead of iterating the batches.
        z_bounding_rect: &PictureRect,
    ) -> &mut FrameVec<PrimitiveInstanceData> {
        // If the area of this primitive is larger than the given threshold,
        // then it is large enough to warrant breaking a batch for. In this
        // case we just see if it can be added to the existing batch or
        // create a new one.
        let is_large_occluder = z_bounding_rect.area() > self.pixel_area_threshold_for_new_batch;
        // Since primitives of the same kind tend to come in succession, we keep track
        // of the current batch index to skip the search in some cases. We ignore the
        // current batch index in the case of large occluders to make sure they get added
        // at the top of the bach list.
        if is_large_occluder || self.current_batch_index == usize::MAX ||
           !self.batches[self.current_batch_index].key.is_compatible_with(&key) {
            let mut selected_batch_index = None;
            if is_large_occluder {
                if let Some(batch) = self.batches.last() {
                    if batch.key.is_compatible_with(&key) {
                        selected_batch_index = Some(self.batches.len() - 1);
                    }
                }
            } else {
                // Otherwise, look back through a reasonable number of batches.
                for (batch_index, batch) in self.batches.iter().enumerate().rev().take(self.lookback_count) {
                    if batch.key.is_compatible_with(&key) {
                        selected_batch_index = Some(batch_index);
                        break;
                    }
                }
            }

            if selected_batch_index.is_none() {
                let new_batch = PrimitiveBatch::new(key, self.batches.allocator().clone());
                selected_batch_index = Some(self.batches.len());
                self.batches.push(new_batch);
            }

            self.current_batch_index = selected_batch_index.unwrap();
        }

        let batch = &mut self.batches[self.current_batch_index];
        batch.features |= features;
        batch.key.textures.merge(&key.textures);

        &mut batch.instances
    }

    fn finalize(&mut self) {
        // Reverse the instance arrays in the opaque batches
        // to get maximum z-buffer efficiency by drawing
        // front-to-back.
        // TODO(gw): Maybe we can change the batch code to
        //           build these in reverse and avoid having
        //           to reverse the instance array here.
        for batch in &mut self.batches {
            batch.instances.reverse();
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveBatch {
    pub key: BatchKey,
    pub instances: FrameVec<PrimitiveInstanceData>,
    pub features: BatchFeatures,
}

bitflags! {
    /// Features of the batch that, if not requested, may allow a fast-path.
    ///
    /// Rather than breaking batches when primitives request different features,
    /// we always request the minimum amount of features to satisfy all items in
    /// the batch.
    /// The goal is to let the renderer be optionally select more specialized
    /// versions of a shader if the batch doesn't require code certain code paths.
    /// Not all shaders necessarily implement all of these features.
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[cfg_attr(feature = "replay", derive(Deserialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct BatchFeatures: u8 {
        const ALPHA_PASS = 1 << 0;
        const ANTIALIASING = 1 << 1;
        const REPETITION = 1 << 2;
        /// Indicates a primitive in this batch may use a clip mask.
        const CLIP_MASK = 1 << 3;
    }
}

impl PrimitiveBatch {
    fn new(key: BatchKey, allocator: FrameAllocator) -> PrimitiveBatch {
        PrimitiveBatch {
            key,
            instances: FrameVec::new_in(allocator),
            features: BatchFeatures::empty(),
        }
    }

    fn merge(&mut self, other: PrimitiveBatch) {
        self.instances.extend(other.instances);
        self.features |= other.features;
        self.key.textures.merge(&other.key.textures);
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct AlphaBatchContainer {
    pub opaque_batches: FrameVec<PrimitiveBatch>,
    pub alpha_batches: FrameVec<PrimitiveBatch>,
    /// The overall scissor rect for this render task, if one
    /// is required.
    pub task_scissor_rect: Option<DeviceIntRect>,
    /// The rectangle of the owning render target that this
    /// set of batches affects.
    pub task_rect: DeviceIntRect,
}

impl AlphaBatchContainer {
    pub fn new(
        task_scissor_rect: Option<DeviceIntRect>,
        memory: &FrameMemory,
    ) -> AlphaBatchContainer {
        AlphaBatchContainer {
            opaque_batches: memory.new_vec(),
            alpha_batches: memory.new_vec(),
            task_scissor_rect,
            task_rect: DeviceIntRect::zero(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.opaque_batches.is_empty() &&
        self.alpha_batches.is_empty()
    }

    fn merge(&mut self, builder: AlphaBatchBuilder, task_rect: &DeviceIntRect) {
        self.task_rect = self.task_rect.union(task_rect);

        for other_batch in builder.opaque_batch_list.batches {
            let batch_index = self.opaque_batches.iter().position(|batch| {
                batch.key.is_compatible_with(&other_batch.key)
            });

            match batch_index {
                Some(batch_index) => {
                    self.opaque_batches[batch_index].merge(other_batch);
                }
                None => {
                    self.opaque_batches.push(other_batch);
                }
            }
        }

        let mut min_batch_index = 0;

        for other_batch in builder.alpha_batch_list.batches {
            let batch_index = self.alpha_batches.iter().skip(min_batch_index).position(|batch| {
                batch.key.is_compatible_with(&other_batch.key)
            });

            match batch_index {
                Some(batch_index) => {
                    let index = batch_index + min_batch_index;
                    self.alpha_batches[index].merge(other_batch);
                    min_batch_index = index;
                }
                None => {
                    self.alpha_batches.push(other_batch);
                    min_batch_index = self.alpha_batches.len();
                }
            }
        }
    }
}

/// Each segment can optionally specify a per-segment
/// texture set and one user data field.
#[derive(Debug, Copy, Clone)]
struct SegmentInstanceData {
    textures: TextureSet,
    specific_resource_address: i32,
}

/// Encapsulates the logic of building batches for items that are blended.
pub struct AlphaBatchBuilder {
    pub alpha_batch_list: AlphaBatchList,
    pub opaque_batch_list: OpaqueBatchList,
    pub render_task_id: RenderTaskId,
    render_task_address: RenderTaskAddress,
}

impl AlphaBatchBuilder {
    pub fn new(
        screen_size: DeviceIntSize,
        break_advanced_blend_batches: bool,
        lookback_count: usize,
        render_task_id: RenderTaskId,
        render_task_address: RenderTaskAddress,
        memory: &FrameMemory,
    ) -> Self {
        // The threshold for creating a new batch is
        // one quarter the screen size.
        let batch_area_threshold = (screen_size.width * screen_size.height) as f32 / 4.0;

        AlphaBatchBuilder {
            alpha_batch_list: AlphaBatchList::new(break_advanced_blend_batches, 128, memory),
            opaque_batch_list: OpaqueBatchList::new(batch_area_threshold, lookback_count, memory),
            render_task_id,
            render_task_address,
        }
    }

    /// Clear all current batches in this builder. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.alpha_batch_list.clear();
        self.opaque_batch_list.clear();
    }

    pub fn build(
        mut self,
        batch_containers: &mut FrameVec<AlphaBatchContainer>,
        merged_batches: &mut AlphaBatchContainer,
        task_rect: DeviceIntRect,
        task_scissor_rect: Option<DeviceIntRect>,
    ) {
        self.opaque_batch_list.finalize();

        if task_scissor_rect.is_none() {
            merged_batches.merge(self, &task_rect);
        } else {
            batch_containers.push(AlphaBatchContainer {
                alpha_batches: self.alpha_batch_list.batches,
                opaque_batches: self.opaque_batch_list.batches,
                task_scissor_rect,
                task_rect,
            });
        }
    }

    pub fn push_single_instance(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &PictureRect,
        z_id: ZBufferId,
        instance: PrimitiveInstanceData,
    ) {
        self.set_params_and_get_batch(key, features, bounding_rect, z_id)
            .push(instance);
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &PictureRect,
        z_id: ZBufferId,
    ) -> &mut FrameVec<PrimitiveInstanceData> {
        match key.blend_mode {
            BlendMode::None => {
                self.opaque_batch_list
                    .set_params_and_get_batch(key, features, bounding_rect)
            }
            BlendMode::Alpha |
            BlendMode::PremultipliedAlpha |
            BlendMode::PremultipliedDestOut |
            BlendMode::SubpixelDualSource |
            BlendMode::Advanced(_) |
            BlendMode::MultiplyDualSource |
            BlendMode::Screen |
            BlendMode::Exclusion |
            BlendMode::PlusLighter => {
                self.alpha_batch_list
                    .set_params_and_get_batch(key, features, bounding_rect, z_id)
            }
        }
    }
}

/// Supports (recursively) adding a list of primitives and pictures to an alpha batch
/// builder. In future, it will support multiple dirty regions / slices, allowing the
/// contents of a picture to be spliced into multiple batch builders.
pub struct BatchBuilder {
    /// A temporary buffer that is used during glyph fetching, stored here
    /// to reduce memory allocations.
    glyph_fetch_buffer: Vec<GlyphFetchResult>,

    batcher: AlphaBatchBuilder,
}

impl BatchBuilder {
    pub fn new(batcher: AlphaBatchBuilder) -> Self {
        BatchBuilder {
            glyph_fetch_buffer: Vec::new(),
            batcher,
        }
    }

    pub fn finalize(self) -> AlphaBatchBuilder {
        self.batcher
    }

    fn add_brush_instance_to_batches(
        &mut self,
        batch_key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &PictureRect,
        z_id: ZBufferId,
        segment_index: i32,
        edge_flags: EdgeAaSegmentMask,
        clip_task_address: RenderTaskAddress,
        brush_flags: BrushFlags,
        prim_header_index: PrimitiveHeaderIndex,
        resource_address: i32,
    ) {
        assert!(
            !(brush_flags.contains(BrushFlags::NORMALIZED_UVS)
                && features.contains(BatchFeatures::REPETITION)),
            "Normalized UVs are not supported with repetition."
        );
        let instance = BrushInstance {
            segment_index,
            edge_flags,
            clip_task_address,
            brush_flags,
            prim_header_index,
            resource_address,
        };

        self.batcher.push_single_instance(
            batch_key,
            features,
            bounding_rect,
            z_id,
            PrimitiveInstanceData::from(instance),
        );
    }

    fn add_split_composite_instance_to_batches(
        &mut self,
        batch_key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &PictureRect,
        z_id: ZBufferId,
        prim_header_index: PrimitiveHeaderIndex,
        polygons_address: i32,
    ) {
        let render_task_address = self.batcher.render_task_address;

        self.batcher.push_single_instance(
            batch_key,
            features,
            bounding_rect,
            z_id,
            PrimitiveInstanceData::from(SplitCompositeInstance {
                prim_header_index,
                render_task_address,
                polygons_address,
                z: z_id,
            }),
        );
    }

    /// Clear all current batchers. This is typically used when a primitive
    /// is encountered that occludes all previous content in this batch list.
    fn clear_batches(&mut self) {
        self.batcher.clear();
    }

    // Adds a primitive to a batch.
    // It can recursively call itself in some situations, for
    // example if it encounters a picture where the items
    // in that picture are being drawn into the same target.
    pub fn add_prim_to_batch(
        &mut self,
        cmd: &PrimitiveCommand,
        prim_spatial_node_index: SpatialNodeIndex,
        ctx: &RenderTargetContext,
        gpu_cache: &mut GpuCache,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        root_spatial_node_index: SpatialNodeIndex,
        surface_spatial_node_index: SpatialNodeIndex,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        gpu_buffer_builder: &mut GpuBufferBuilder,
        segments: &[RenderTaskId],
    ) {
        let (prim_instance_index, extra_prim_gpu_address) = match cmd {
            PrimitiveCommand::Simple { prim_instance_index } => {
                (prim_instance_index, None)
            }
            PrimitiveCommand::Complex { prim_instance_index, gpu_address } => {
                (prim_instance_index, Some(gpu_address.as_int()))
            }
            PrimitiveCommand::Instance { prim_instance_index, gpu_buffer_address } => {
                (prim_instance_index, Some(gpu_buffer_address.as_int()))
            }
            PrimitiveCommand::Quad { pattern, pattern_input, prim_instance_index, gpu_buffer_address, quad_flags, edge_flags, transform_id, src_color_task_id } => {
                let prim_instance = &prim_instances[prim_instance_index.0 as usize];
                let prim_info = &prim_instance.vis;
                let bounding_rect = &prim_info.clip_chain.pic_coverage_rect;
                let render_task_address = self.batcher.render_task_address;

                if segments.is_empty() {
                    let z_id = z_generator.next();

                    quad::add_to_batch(
                        *pattern,
                        *pattern_input,
                        render_task_address,
                        *transform_id,
                        *gpu_buffer_address,
                        *quad_flags,
                        *edge_flags,
                        INVALID_SEGMENT_INDEX as u8,
                        *src_color_task_id,
                        z_id,
                        render_tasks,
                        gpu_buffer_builder,
                        |key, instance| {
                            let batch = self.batcher.set_params_and_get_batch(
                                key,
                                BatchFeatures::empty(),
                                bounding_rect,
                                z_id,
                            );
                            batch.push(instance);
                        },
                    );
                } else {
                    for (i, task_id) in segments.iter().enumerate() {
                        // TODO(gw): edge_flags should be per-segment, when used for more than composites
                        debug_assert!(edge_flags.is_empty());

                        let z_id = z_generator.next();

                        quad::add_to_batch(
                            *pattern,
                            *pattern_input,
                            render_task_address,
                            *transform_id,
                            *gpu_buffer_address,
                            *quad_flags,
                            *edge_flags,
                            i as u8,
                            *task_id,
                            z_id,
                            render_tasks,
                            gpu_buffer_builder,
                            |key, instance| {
                                let batch = self.batcher.set_params_and_get_batch(
                                    key,
                                    BatchFeatures::empty(),
                                    bounding_rect,
                                    z_id,
                                );
                                batch.push(instance);
                            },
                        );
                    }
                }

                return;
            }
        };

        let prim_instance = &prim_instances[prim_instance_index.0 as usize];
        let is_anti_aliased = ctx.data_stores.prim_has_anti_aliasing(prim_instance);

        let brush_flags = if is_anti_aliased {
            BrushFlags::FORCE_AA
        } else {
            BrushFlags::empty()
        };

        let vis_flags = match prim_instance.vis.state {
            VisibilityState::Culled => {
                return;
            }
            VisibilityState::PassThrough |
            VisibilityState::Unset => {
                panic!("bug: invalid visibility state");
            }
            VisibilityState::Visible { vis_flags, .. } => {
                vis_flags
            }
        };

        // If this primitive is a backdrop, that means that it is known to cover
        // the entire picture cache background. In that case, the renderer will
        // use the backdrop color as a clear color, and so we can drop this
        // primitive and any prior primitives from the batch lists for this
        // picture cache slice.
        if vis_flags.contains(PrimitiveVisibilityFlags::IS_BACKDROP) {
            self.clear_batches();
            return;
        }

        let transform_id = transforms
            .get_id(
                prim_spatial_node_index,
                root_spatial_node_index,
                ctx.spatial_tree,
            );

        // TODO(gw): Calculating this for every primitive is a bit
        //           wasteful. We should probably cache this in
        //           the scroll node...
        let transform_kind = transform_id.transform_kind();
        let prim_info = &prim_instance.vis;
        let bounding_rect = &prim_info.clip_chain.pic_coverage_rect;

        let z_id = z_generator.next();

        let prim_rect = ctx.data_stores.get_local_prim_rect(
            prim_instance,
            &ctx.prim_store.pictures,
            ctx.surfaces,
        );

        let mut batch_features = BatchFeatures::empty();
        if ctx.data_stores.prim_may_need_repetition(prim_instance) {
            batch_features |= BatchFeatures::REPETITION;
        }

        if transform_kind != TransformedRectKind::AxisAligned || is_anti_aliased {
            batch_features |= BatchFeatures::ANTIALIASING;
        }

        // Check if the primitive might require a clip mask.
        if prim_info.clip_task_index != ClipTaskIndex::INVALID {
            batch_features |= BatchFeatures::CLIP_MASK;
        }

        if !bounding_rect.is_empty() {
            debug_assert_eq!(prim_info.clip_chain.pic_spatial_node_index, surface_spatial_node_index,
                "The primitive's bounding box is specified in a different coordinate system from the current batch!");
        }

        if let PrimitiveInstanceKind::Picture { pic_index, .. } = prim_instance.kind {
            let picture = &ctx.prim_store.pictures[pic_index.0];
            if let Some(snapshot) = picture.snapshot {
                if snapshot.detached {
                    return;
                }
            }

            let blend_mode = BlendMode::PremultipliedAlpha;
            let prim_cache_address = gpu_cache.get_address(&ctx.globals.default_image_handle);

            match picture.raster_config {
                Some(ref raster_config) => {
                    // If the child picture was rendered in local space, we can safely
                    // interpolate the UV coordinates with perspective correction.
                    let brush_flags = brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION;

                    let surface = &ctx.surfaces[raster_config.surface_index.0];
                    let mut local_clip_rect = prim_info.clip_chain.local_clip_rect;

                    // If we are drawing with snapping enabled, form a simple transform that just applies
                    // the scale / translation from the raster transform. Otherwise, in edge cases where the
                    // intermediate surface has a non-identity but axis-aligned transform (e.g. a 180 degree
                    // rotation) it can be applied twice.
                    let transform_id = if surface.surface_spatial_node_index == surface.raster_spatial_node_index {
                        transform_id
                    } else {
                        let map_local_to_raster = SpaceMapper::new_with_target(
                            root_spatial_node_index,
                            surface.surface_spatial_node_index,
                            LayoutRect::max_rect(),
                            ctx.spatial_tree,
                        );

                        let raster_rect = map_local_to_raster
                            .map(&prim_rect)
                            .unwrap();

                        let sx = (raster_rect.max.x - raster_rect.min.x) / (prim_rect.max.x - prim_rect.min.x);
                        let sy = (raster_rect.max.y - raster_rect.min.y) / (prim_rect.max.y - prim_rect.min.y);

                        let tx = raster_rect.min.x - sx * prim_rect.min.x;
                        let ty = raster_rect.min.y - sy * prim_rect.min.y;

                        let transform = ScaleOffset::new(sx, sy, tx, ty);

                        let raster_clip_rect = map_local_to_raster
                            .map(&prim_info.clip_chain.local_clip_rect)
                            .unwrap();
                        local_clip_rect = transform.unmap_rect(&raster_clip_rect);

                        transforms.get_custom(transform.to_transform())
                    };

                    let prim_header = PrimitiveHeader {
                        local_rect: prim_rect,
                        local_clip_rect,
                        specific_prim_address: prim_cache_address,
                        transform_id,
                    };

                    let mut is_opaque = prim_info.clip_task_index == ClipTaskIndex::INVALID
                        && surface.is_opaque
                        && transform_kind == TransformedRectKind::AxisAligned
                        && !is_anti_aliased;

                    let pic_task_id = picture.primary_render_task_id.unwrap();

                    let pic_task = &render_tasks[pic_task_id];
                    match pic_task.sub_pass {
                        Some(SubPass::Masks { .. }) => {
                            is_opaque = false;
                        }
                        None => {}
                    }
                    match raster_config.composite_mode {
                        PictureCompositeMode::TileCache { .. } => {
                            // TODO(gw): For now, TileCache is still a composite mode, even though
                            //           it will only exist as a top level primitive and never
                            //           be encountered during batching. Consider making TileCache
                            //           a standalone type, not a picture.
                            return;
                        }
                        PictureCompositeMode::IntermediateSurface { .. } => {
                            // TODO(gw): As an optimization, support making this a pass-through
                            //           and/or drawing directly from here when possible
                            //           (e.g. if not wrapped by filters / different spatial node).
                            return;
                        }
                        _=>{}
                    }

                    let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                        prim_info.clip_task_index,
                        render_tasks,
                    ).unwrap();

                    let (uv_rect_address, texture) = render_tasks.resolve_location(
                        pic_task_id,
                        gpu_cache,
                    ).unwrap();

                    // The set of input textures that most composite modes use,
                    // howevr some override it.
                    let textures = BatchTextures::prim_textured(
                        texture,
                        clip_mask_texture_id,
                    );

                    match raster_config.composite_mode {
                        PictureCompositeMode::TileCache { .. } => {}
                        PictureCompositeMode::IntermediateSurface { .. } => {}
                        PictureCompositeMode::Filter(ref filter) => {
                            assert!(filter.is_visible());
                            match filter {
                                Filter::Blur { .. } => {
                                    let kind = BatchKind::Brush(
                                        BrushBatchKind::Image(ImageBufferKind::Texture2D)
                                    );

                                    let key = BatchKey::new(
                                        kind,
                                        blend_mode,
                                        textures,
                                    );
                                    let prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id,
                                        self.batcher.render_task_address,
                                        ImageBrushData {
                                            color_mode: ShaderColorMode::Image,
                                            alpha_type: AlphaType::PremultipliedAlpha,
                                            raster_space: RasterizationSpace::Screen,
                                            opacity: 1.0,
                                        }.encode(),
                                    );

                                    self.add_brush_instance_to_batches(
                                        key,
                                        batch_features,
                                        bounding_rect,
                                        z_id,
                                        INVALID_SEGMENT_INDEX,
                                        EdgeAaSegmentMask::all(),
                                        clip_task_address,
                                        brush_flags,
                                        prim_header_index,
                                        uv_rect_address.as_int(),
                                    );
                                }
                                Filter::DropShadows(shadows) => {
                                    // Draw an instance per shadow first, following by the content.

                                    // The shadows and the content get drawn as a brush image.
                                    let kind = BatchKind::Brush(
                                        BrushBatchKind::Image(ImageBufferKind::Texture2D),
                                    );

                                    // Gets the saved render task ID of the content, which is
                                    // deeper in the render task graph than the direct child.
                                    let secondary_id = picture.secondary_render_task_id.expect("no secondary!?");
                                    let content_source = {
                                        let secondary_task = &render_tasks[secondary_id];
                                        let texture_id = secondary_task.get_target_texture();
                                        TextureSource::TextureCache(
                                            texture_id,
                                            Swizzle::default(),
                                        )
                                    };

                                    // Retrieve the UV rect addresses for shadow/content.
                                    let shadow_uv_rect_address = uv_rect_address;
                                    let shadow_textures = textures;

                                    let content_uv_rect_address = render_tasks[secondary_id]
                                        .get_texture_address(gpu_cache)
                                        .as_int();

                                    // Build BatchTextures for shadow/content
                                    let content_textures = BatchTextures::prim_textured(
                                        content_source,
                                        clip_mask_texture_id,
                                    );

                                    // Build batch keys for shadow/content
                                    let shadow_key = BatchKey::new(kind, blend_mode, shadow_textures);
                                    let content_key = BatchKey::new(kind, blend_mode, content_textures);

                                    for (shadow, shadow_gpu_data) in shadows.iter().zip(picture.extra_gpu_data_handles.iter()) {
                                        // Get the GPU cache address of the extra data handle.
                                        let shadow_prim_address = gpu_cache.get_address(shadow_gpu_data);

                                        let shadow_rect = prim_header.local_rect.translate(shadow.offset);

                                        let shadow_prim_header = PrimitiveHeader {
                                            local_rect: shadow_rect,
                                            specific_prim_address: shadow_prim_address,
                                            ..prim_header
                                        };

                                        let shadow_prim_header_index = prim_headers.push(
                                            &shadow_prim_header,
                                            z_id,
                                            self.batcher.render_task_address,
                                            ImageBrushData {
                                                color_mode: ShaderColorMode::Alpha,
                                                alpha_type: AlphaType::PremultipliedAlpha,
                                                raster_space: RasterizationSpace::Screen,
                                                opacity: 1.0,
                                            }.encode(),
                                        );

                                        self.add_brush_instance_to_batches(
                                            shadow_key,
                                            batch_features,
                                            bounding_rect,
                                            z_id,
                                            INVALID_SEGMENT_INDEX,
                                            EdgeAaSegmentMask::all(),
                                            clip_task_address,
                                            brush_flags,
                                            shadow_prim_header_index,
                                            shadow_uv_rect_address.as_int(),
                                        );
                                    }
                                    let z_id_content = z_generator.next();

                                    let content_prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id_content,
                                        self.batcher.render_task_address,
                                        ImageBrushData {
                                            color_mode: ShaderColorMode::Image,
                                            alpha_type: AlphaType::PremultipliedAlpha,
                                            raster_space: RasterizationSpace::Screen,
                                            opacity: 1.0,
                                        }.encode(),
                                    );

                                    self.add_brush_instance_to_batches(
                                        content_key,
                                        batch_features,
                                        bounding_rect,
                                        z_id_content,
                                        INVALID_SEGMENT_INDEX,
                                        EdgeAaSegmentMask::all(),
                                        clip_task_address,
                                        brush_flags,
                                        content_prim_header_index,
                                        content_uv_rect_address,
                                    );
                                }
                                Filter::Opacity(_, amount) => {
                                    let amount = (amount * 65536.0) as i32;

                                    let key = BatchKey::new(
                                        BatchKind::Brush(BrushBatchKind::Opacity),
                                        BlendMode::PremultipliedAlpha,
                                        textures,
                                    );

                                    let prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id,
                                        self.batcher.render_task_address,
                                        [
                                            uv_rect_address.as_int(),
                                            amount,
                                            0,
                                            0,
                                        ]
                                    );

                                    self.add_brush_instance_to_batches(
                                        key,
                                        batch_features,
                                        bounding_rect,
                                        z_id,
                                        INVALID_SEGMENT_INDEX,
                                        EdgeAaSegmentMask::all(),
                                        clip_task_address,
                                        brush_flags,
                                        prim_header_index,
                                        0,
                                    );
                                }
                                _ => {
                                    // Must be kept in sync with brush_blend.glsl
                                    let filter_mode = filter.as_int();

                                    let user_data = match filter {
                                        Filter::Identity => 0x10000i32, // matches `Contrast(1)`
                                        Filter::Contrast(amount) |
                                        Filter::Grayscale(amount) |
                                        Filter::Invert(amount) |
                                        Filter::Saturate(amount) |
                                        Filter::Sepia(amount) |
                                        Filter::Brightness(amount) => {
                                            (amount * 65536.0) as i32
                                        }
                                        Filter::SrgbToLinear | Filter::LinearToSrgb => 0,
                                        Filter::HueRotate(angle) => {
                                            (0.01745329251 * angle * 65536.0) as i32
                                        }
                                        Filter::ColorMatrix(_) => {
                                            picture.extra_gpu_data_handles[0].as_int(gpu_cache)
                                        }
                                        Filter::Flood(_) => {
                                            picture.extra_gpu_data_handles[0].as_int(gpu_cache)
                                        }

                                        // These filters are handled via different paths.
                                        Filter::ComponentTransfer |
                                        Filter::Blur { .. } |
                                        Filter::DropShadows(..) |
                                        Filter::Opacity(..) |
                                        Filter::SVGGraphNode(..) => unreachable!(),
                                    };

                                    // Other filters that may introduce opacity are handled via different
                                    // paths.
                                    if let Filter::ColorMatrix(..) = filter {
                                        is_opaque = false;
                                    }

                                    let blend_mode = if is_opaque {
                                        BlendMode::None
                                    } else {
                                        BlendMode::PremultipliedAlpha
                                    };

                                    let key = BatchKey::new(
                                        BatchKind::Brush(BrushBatchKind::Blend),
                                        blend_mode,
                                        textures,
                                    );

                                    let prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id,
                                        self.batcher.render_task_address,
                                        [
                                            uv_rect_address.as_int(),
                                            filter_mode,
                                            user_data,
                                            0,
                                        ]
                                    );

                                    self.add_brush_instance_to_batches(
                                        key,
                                        batch_features,
                                        bounding_rect,
                                        z_id,
                                        INVALID_SEGMENT_INDEX,
                                        EdgeAaSegmentMask::all(),
                                        clip_task_address,
                                        brush_flags,
                                        prim_header_index,
                                        0,
                                    );
                                }
                            }
                        }
                        PictureCompositeMode::ComponentTransferFilter(handle) => {
                            // This is basically the same as the general filter case above
                            // except we store a little more data in the filter mode and
                            // a gpu cache handle in the user data.
                            let filter_data = &ctx.data_stores.filter_data[handle];
                            let filter_mode : i32 = Filter::ComponentTransfer.as_int() |
                                ((filter_data.data.r_func.to_int() << 28 |
                                  filter_data.data.g_func.to_int() << 24 |
                                  filter_data.data.b_func.to_int() << 20 |
                                  filter_data.data.a_func.to_int() << 16) as i32);

                            let user_data = filter_data.gpu_cache_handle.as_int(gpu_cache);

                            let key = BatchKey::new(
                                BatchKind::Brush(BrushBatchKind::Blend),
                                BlendMode::PremultipliedAlpha,
                                textures,
                            );

                            let prim_header_index = prim_headers.push(
                                &prim_header,
                                z_id,
                                self.batcher.render_task_address,
                                [
                                    uv_rect_address.as_int(),
                                    filter_mode,
                                    user_data,
                                    0,
                                ]
                            );

                            self.add_brush_instance_to_batches(
                                key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                INVALID_SEGMENT_INDEX,
                                EdgeAaSegmentMask::all(),
                                clip_task_address,
                                brush_flags,
                                prim_header_index,
                                0,
                            );
                        }
                        PictureCompositeMode::MixBlend(mode) if BlendMode::from_mix_blend_mode(
                            mode,
                            ctx.use_advanced_blending,
                            !ctx.break_advanced_blend_batches,
                            ctx.use_dual_source_blending,
                        ).is_some() => {
                            let key = BatchKey::new(
                                BatchKind::Brush(
                                    BrushBatchKind::Image(ImageBufferKind::Texture2D),
                                ),
                                BlendMode::from_mix_blend_mode(
                                    mode,
                                    ctx.use_advanced_blending,
                                    !ctx.break_advanced_blend_batches,
                                    ctx.use_dual_source_blending,
                                ).unwrap(),
                                textures,
                            );
                            let prim_header_index = prim_headers.push(
                                &prim_header,
                                z_id,
                                self.batcher.render_task_address,
                                ImageBrushData {
                                    color_mode: match key.blend_mode {
                                        BlendMode::MultiplyDualSource => ShaderColorMode::MultiplyDualSource,
                                        _ => ShaderColorMode::Image,
                                    },
                                    alpha_type: AlphaType::PremultipliedAlpha,
                                    raster_space: RasterizationSpace::Screen,
                                    opacity: 1.0,
                                }.encode(),
                            );

                            self.add_brush_instance_to_batches(
                                key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                INVALID_SEGMENT_INDEX,
                                EdgeAaSegmentMask::all(),
                                clip_task_address,
                                brush_flags,
                                prim_header_index,
                                uv_rect_address.as_int(),
                            );
                        }
                        PictureCompositeMode::MixBlend(mode) => {
                            let backdrop_id = picture.secondary_render_task_id.expect("no backdrop!?");

                            let color0 = render_tasks[backdrop_id].get_target_texture();
                            let color1 = render_tasks[pic_task_id].get_target_texture();

                            // Create a separate brush instance for each batcher. For most cases,
                            // there is only one batcher. However, in the case of drawing onto
                            // a picture cache, there is one batcher per tile. Although not
                            // currently used, the implementation of mix-blend-mode now supports
                            // doing partial readbacks per-tile. In future, this will be enabled
                            // and allow mix-blends to operate on picture cache surfaces without
                            // a separate isolated intermediate surface.

                            let batch_key = BatchKey::new(
                                BatchKind::Brush(
                                    BrushBatchKind::MixBlend {
                                        task_id: self.batcher.render_task_id,
                                        backdrop_id,
                                    },
                                ),
                                BlendMode::PremultipliedAlpha,
                                BatchTextures {
                                    input: TextureSet {
                                        colors: [
                                            TextureSource::TextureCache(
                                                color0,
                                                Swizzle::default(),
                                            ),
                                            TextureSource::TextureCache(
                                                color1,
                                                Swizzle::default(),
                                            ),
                                            TextureSource::Invalid,
                                        ],
                                    },
                                    clip_mask: clip_mask_texture_id,
                                },
                            );
                            let src_uv_address = render_tasks[pic_task_id].get_texture_address(gpu_cache);
                            let readback_uv_address = render_tasks[backdrop_id].get_texture_address(gpu_cache);
                            let prim_header_index = prim_headers.push(
                                &prim_header,
                                z_id,
                                self.batcher.render_task_address,
                                [
                                    mode as u32 as i32,
                                    readback_uv_address.as_int(),
                                    src_uv_address.as_int(),
                                    0,
                                ]
                            );

                            let instance = BrushInstance {
                                segment_index: INVALID_SEGMENT_INDEX,
                                edge_flags: EdgeAaSegmentMask::all(),
                                clip_task_address,
                                brush_flags,
                                prim_header_index,
                                resource_address: 0,
                            };

                            self.batcher.push_single_instance(
                                batch_key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                PrimitiveInstanceData::from(instance),
                            );
                        }
                        PictureCompositeMode::Blit(_) => {
                            match picture.context_3d {
                                Picture3DContext::In { root_data: Some(_), .. } => {
                                    unreachable!("bug: should not have a raster_config");
                                }
                                Picture3DContext::In { root_data: None, .. } => {
                                    // TODO(gw): Store this inside the split picture so that we
                                    //           don't need to pass in extra_prim_gpu_address for
                                    //           every prim instance.
                                    // TODO(gw): Ideally we'd skip adding 3d child prims to batches
                                    //           without gpu cache address but it's currently
                                    //           used by the prepare pass. Refactor this!
                                    let extra_prim_gpu_address = match extra_prim_gpu_address {
                                        Some(prim_address) => prim_address,
                                        None => return,
                                    };

                                    let prim_header = PrimitiveHeader {
                                        local_rect: prim_rect,
                                        local_clip_rect: prim_info.clip_chain.local_clip_rect,
                                        specific_prim_address: GpuCacheAddress::INVALID,
                                        transform_id: transforms
                                            .get_id(
                                                prim_spatial_node_index,
                                                root_spatial_node_index,
                                                ctx.spatial_tree,
                                            ),
                                    };

                                    // Need a new z-id for each child preserve-3d context added
                                    // by this inner loop.
                                    let z_id = z_generator.next();

                                    let prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id,
                                        self.batcher.render_task_address,
                                        [
                                            uv_rect_address.as_int(),
                                            BrushFlags::PERSPECTIVE_INTERPOLATION.bits() as i32,
                                            0,
                                            clip_task_address.0 as i32,
                                        ]
                                    );

                                    let key = BatchKey::new(
                                        BatchKind::SplitComposite,
                                        BlendMode::PremultipliedAlpha,
                                        textures,
                                    );

                                    self.add_split_composite_instance_to_batches(
                                        key,
                                        BatchFeatures::CLIP_MASK,
                                        &prim_info.clip_chain.pic_coverage_rect,
                                        z_id,
                                        prim_header_index,
                                        extra_prim_gpu_address,
                                    );
                                }
                                Picture3DContext::Out { .. } => {
                                    let textures = TextureSet {
                                        colors: [
                                            texture,
                                            TextureSource::Invalid,
                                            TextureSource::Invalid,
                                        ],
                                    };
                                    let batch_params = BrushBatchParameters::shared(
                                        BrushBatchKind::Image(ImageBufferKind::Texture2D),
                                        textures,
                                        ImageBrushData {
                                            color_mode: ShaderColorMode::Image,
                                            alpha_type: AlphaType::PremultipliedAlpha,
                                            raster_space: RasterizationSpace::Screen,
                                            opacity: 1.0,
                                        }.encode(),
                                        uv_rect_address.as_int(),
                                    );

                                    let prim_header = PrimitiveHeader {
                                        specific_prim_address: prim_cache_address,
                                        ..prim_header
                                    };

                                    let prim_header_index = prim_headers.push(
                                        &prim_header,
                                        z_id,
                                        self.batcher.render_task_address,
                                        batch_params.prim_user_data,
                                    );

                                    let (opacity, blend_mode) = if is_opaque {
                                        (PrimitiveOpacity::opaque(), BlendMode::None)
                                    } else {
                                        (PrimitiveOpacity::translucent(), BlendMode::PremultipliedAlpha)
                                    };

                                    self.add_segmented_prim_to_batch(
                                        None,
                                        opacity,
                                        &batch_params,
                                        blend_mode,
                                        batch_features,
                                        brush_flags,
                                        EdgeAaSegmentMask::all(),
                                        prim_header_index,
                                        bounding_rect,
                                        transform_kind,
                                        z_id,
                                        prim_info.clip_task_index,
                                        ctx,
                                        render_tasks,
                                    );
                                }
                            }
                        }
                        PictureCompositeMode::SvgFilter(..) => {
                            let kind = BatchKind::Brush(
                                BrushBatchKind::Image(ImageBufferKind::Texture2D)
                            );
                            let key = BatchKey::new(
                                kind,
                                blend_mode,
                                textures,
                            );
                            let prim_header_index = prim_headers.push(
                                &prim_header,
                                z_id,
                                self.batcher.render_task_address,
                                ImageBrushData {
                                    color_mode: ShaderColorMode::Image,
                                    alpha_type: AlphaType::PremultipliedAlpha,
                                    raster_space: RasterizationSpace::Screen,
                                    opacity: 1.0,
                                }.encode(),
                            );

                            self.add_brush_instance_to_batches(
                                key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                INVALID_SEGMENT_INDEX,
                                EdgeAaSegmentMask::all(),
                                clip_task_address,
                                brush_flags,
                                prim_header_index,
                                uv_rect_address.as_int(),
                            );
                        }
                        PictureCompositeMode::SVGFEGraph(..) => {
                            let kind = BatchKind::Brush(
                                BrushBatchKind::Image(ImageBufferKind::Texture2D)
                            );
                            let key = BatchKey::new(
                                kind,
                                blend_mode,
                                textures,
                            );
                            let prim_header_index = prim_headers.push(
                                &prim_header,
                                z_id,
                                self.batcher.render_task_address,
                                ImageBrushData {
                                    color_mode: ShaderColorMode::Image,
                                    alpha_type: AlphaType::PremultipliedAlpha,
                                    raster_space: RasterizationSpace::Screen,
                                    opacity: 1.0,
                                }.encode(),
                            );

                            self.add_brush_instance_to_batches(
                                key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                INVALID_SEGMENT_INDEX,
                                EdgeAaSegmentMask::all(),
                                clip_task_address,
                                brush_flags,
                                prim_header_index,
                                uv_rect_address.as_int(),
                            );
                        }
                    }
                }
                None => {
                    unreachable!();
                }
            }

            return;
        }

        let common_data = ctx.data_stores.as_common_data(prim_instance);

        let needs_blending = !common_data.opacity.is_opaque ||
            prim_info.clip_task_index != ClipTaskIndex::INVALID ||
            transform_kind == TransformedRectKind::Complex ||
            is_anti_aliased;

        let blend_mode = if needs_blending {
            BlendMode::PremultipliedAlpha
        } else {
            BlendMode::None
        };

        let segment_instance_index = match prim_instance.kind {
            PrimitiveInstanceKind::Rectangle { segment_instance_index, .. }
            | PrimitiveInstanceKind::YuvImage { segment_instance_index, .. } => segment_instance_index,
            _ => SegmentInstanceIndex::UNUSED,
        };

        let (prim_cache_address, segments) = if segment_instance_index == SegmentInstanceIndex::UNUSED {
            (gpu_cache.try_get_address(&common_data.gpu_cache_handle), None)
        } else {
            let segment_instance = &ctx.scratch.segment_instances[segment_instance_index];
            let segments = Some(&ctx.scratch.segments[segment_instance.segments_range]);
            (Some(gpu_cache.get_address(&segment_instance.gpu_cache_handle)), segments)
        };

        // The following primitives lower to the image brush shader in the same way.
        let img_brush_data = match prim_instance.kind {
            PrimitiveInstanceKind::CachedLinearGradient { data_handle, ref visible_tiles_range, .. } => {
                let prim_data = &ctx.data_stores.linear_grad[data_handle];
                Some((prim_data.src_color, Some(visible_tiles_range), prim_data.brush_segments.as_slice()))
            }
            PrimitiveInstanceKind::RadialGradient { data_handle, ref visible_tiles_range, .. } => {
                let prim_data = &ctx.data_stores.radial_grad[data_handle];
                Some((prim_data.src_color, Some(visible_tiles_range), prim_data.brush_segments.as_slice()))
            }
            PrimitiveInstanceKind::ConicGradient { data_handle, ref visible_tiles_range, .. } => {
                let prim_data = &ctx.data_stores.conic_grad[data_handle];
                Some((prim_data.src_color, Some(visible_tiles_range), prim_data.brush_segments.as_slice()))
            }
            PrimitiveInstanceKind::ImageBorder { data_handle, .. } => {
                let prim_data = &ctx.data_stores.image_border[data_handle];
                Some((prim_data.kind.src_color, None, prim_data.kind.brush_segments.as_slice()))
            }
            _ => None,
        };

        if let Some((src_color, visible_tiles_range, brush_segments)) = img_brush_data {
            let src_color = render_tasks.resolve_location(src_color, gpu_cache);

            let (uv_rect_address, texture_source) = match src_color {
                Some(src) => src,
                None => {
                    return;
                }
            };

            let textures = TextureSet::prim_textured(texture_source);

            let prim_header = PrimitiveHeader {
                local_rect: prim_rect,
                local_clip_rect: prim_info.clip_chain.local_clip_rect,
                specific_prim_address: gpu_cache.get_address(&common_data.gpu_cache_handle),
                transform_id,
            };

            let prim_user_data = ImageBrushData {
                color_mode: ShaderColorMode::Image,
                alpha_type: AlphaType::PremultipliedAlpha,
                raster_space: RasterizationSpace::Local,
                opacity: 1.0,
            }.encode();

            let batch_kind = BrushBatchKind::Image(texture_source.image_buffer_kind());

            if visible_tiles_range.map_or(true, |r| r.is_empty()) {
                let batch_params = BrushBatchParameters::shared(
                    batch_kind,
                    textures,
                    prim_user_data,
                    uv_rect_address.as_int(),
                );

                let segments = if brush_segments.is_empty() {
                    None
                } else {
                    Some(&brush_segments[..])
                };

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    batch_params.prim_user_data,
                );

                self.add_segmented_prim_to_batch(
                    segments,
                    common_data.opacity,
                    &batch_params,
                    blend_mode,
                    batch_features,
                    brush_flags,
                    common_data.edge_aa_mask,
                    prim_header_index,
                    bounding_rect,
                    transform_kind,
                    z_id,
                    prim_info.clip_task_index,
                    ctx,
                    render_tasks,
                );
            } else {
                let visible_tiles = &ctx.scratch.gradient_tiles[*visible_tiles_range.unwrap()];

                let (clip_task_address, clip_mask) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                let batch_key = BatchKey {
                    blend_mode,
                    kind: BatchKind::Brush(batch_kind),
                    textures: BatchTextures {
                        input: textures,
                        clip_mask,
                    },
                };

                for tile in visible_tiles {
                    let tile_prim_header = PrimitiveHeader {
                        local_rect: tile.local_rect,
                        local_clip_rect: tile.local_clip_rect,
                        ..prim_header
                    };
                    let prim_header_index = prim_headers.push(
                        &tile_prim_header,
                        z_id,
                        self.batcher.render_task_address,
                        prim_user_data,
                    );

                    self.add_brush_instance_to_batches(
                        batch_key,
                        batch_features,
                        bounding_rect,
                        z_id,
                        INVALID_SEGMENT_INDEX,
                        common_data.edge_aa_mask,
                        clip_task_address,
                        brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION,
                        prim_header_index,
                        uv_rect_address.as_int(),
                    );
                }
            }

            return;
        }

        match prim_instance.kind {
            // Handled above.
            PrimitiveInstanceKind::Picture { .. } => {}
            PrimitiveInstanceKind::CachedLinearGradient { .. } => { }
            PrimitiveInstanceKind::RadialGradient { .. } => { }
            PrimitiveInstanceKind::ConicGradient { .. } => { }
            PrimitiveInstanceKind::ImageBorder { .. } => {}
            PrimitiveInstanceKind::BoxShadow { .. } => {
                unreachable!("BUG: Should not hit box-shadow here as they are handled by quad infra");
            }
            PrimitiveInstanceKind::Clear { .. } => {
                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                // TODO(gw): We can abstract some of the common code below into
                //           helper methods, as we port more primitives to make
                //           use of interning.

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    [get_shader_opacity(1.0), 0, 0, 0],
                );

                let batch_key = BatchKey {
                    blend_mode: BlendMode::PremultipliedDestOut,
                    kind: BatchKind::Brush(BrushBatchKind::Solid),
                    textures: BatchTextures::prim_untextured(clip_mask_texture_id),
                };

                self.add_brush_instance_to_batches(
                    batch_key,
                    batch_features,
                    bounding_rect,
                    z_id,
                    INVALID_SEGMENT_INDEX,
                    common_data.edge_aa_mask,
                    clip_task_address,
                    brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION,
                    prim_header_index,
                    0,
                );
            }
            PrimitiveInstanceKind::NormalBorder { data_handle, ref render_task_ids, .. } => {
                let prim_data = &ctx.data_stores.normal_border[data_handle];
                let task_ids = &ctx.scratch.border_cache_handles[*render_task_ids];
                let mut segment_data: SmallVec<[SegmentInstanceData; 8]> = SmallVec::new();

                // Collect the segment instance data from each render
                // task for each valid edge / corner of the border.

                for task_id in task_ids {
                    if let Some((uv_rect_address, texture)) = render_tasks.resolve_location(*task_id, gpu_cache) {
                        segment_data.push(
                            SegmentInstanceData {
                                textures: TextureSet::prim_textured(texture),
                                specific_resource_address: uv_rect_address.as_int(),
                            }
                        );
                    }
                }

                // TODO: it would be less error-prone to get this info from the texture cache.
                let image_buffer_kind = ImageBufferKind::Texture2D;

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let batch_params = BrushBatchParameters::instanced(
                    BrushBatchKind::Image(image_buffer_kind),
                    ImageBrushData {
                        color_mode: ShaderColorMode::Image,
                        alpha_type: AlphaType::PremultipliedAlpha,
                        raster_space: RasterizationSpace::Local,
                        opacity: 1.0,
                    }.encode(),
                    segment_data,
                );

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    batch_params.prim_user_data,
                );

                let border_data = &prim_data.kind;
                self.add_segmented_prim_to_batch(
                    Some(border_data.brush_segments.as_slice()),
                    common_data.opacity,
                    &batch_params,
                    blend_mode,
                    batch_features,
                    brush_flags,
                    common_data.edge_aa_mask,
                    prim_header_index,
                    bounding_rect,
                    transform_kind,
                    z_id,
                    prim_info.clip_task_index,
                    ctx,
                    render_tasks,
                );
            }
            PrimitiveInstanceKind::TextRun { data_handle, run_index, .. } => {
                let run = &ctx.prim_store.text_runs[run_index];
                let subpx_dir = run.used_font.get_subpx_dir();

                // The GPU cache data is stored in the template and reused across
                // frames and display lists.
                let prim_data = &ctx.data_stores.text_run[data_handle];

                // The local prim rect is only informative for text primitives, as
                // thus is not directly necessary for any drawing of the text run.
                // However the glyph offsets are relative to the prim rect origin
                // less the unsnapped reference frame offset. We also want the
                // the snapped reference frame offset, because cannot recalculate
                // it as it ignores the animated components for the transform. As
                // such, we adjust the prim rect origin here, and replace the size
                // with the unsnapped and snapped offsets respectively. This has
                // the added bonus of avoiding quantization effects when storing
                // floats in the extra header integers.
                let prim_header = PrimitiveHeader {
                    local_rect: LayoutRect {
                        min: prim_rect.min - run.reference_frame_relative_offset,
                        max: run.snapped_reference_frame_relative_offset.to_point(),
                    },
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let glyph_keys = &ctx.scratch.glyph_keys[run.glyph_keys_range];
                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    [
                        (run.raster_scale * 65535.0).round() as i32,
                        0,
                        0,
                        0,
                    ],
                );
                let base_instance = GlyphInstance::new(
                    prim_header_index,
                );
                let batcher = &mut self.batcher;

                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                // The run.used_font.clone() is here instead of instead of inline in the `fetch_glyph`
                // function call to work around a miscompilation.
                // https://github.com/rust-lang/rust/issues/80111
                let font = run.used_font.clone();
                ctx.resource_cache.fetch_glyphs(
                    font,
                    &glyph_keys,
                    &mut self.glyph_fetch_buffer,
                    gpu_cache,
                    |texture_id, glyph_format, glyphs| {
                        debug_assert_ne!(texture_id, TextureSource::Invalid);

                        let subpx_dir = subpx_dir.limit_by(glyph_format);

                        let textures = BatchTextures::prim_textured(
                            texture_id,
                            clip_mask_texture_id,
                        );

                        let kind = BatchKind::TextRun(glyph_format);

                        let (blend_mode, color_mode) = match glyph_format {
                            GlyphFormat::Subpixel |
                            GlyphFormat::TransformedSubpixel => {
                                debug_assert!(ctx.use_dual_source_blending);
                                (
                                    BlendMode::SubpixelDualSource,
                                    ShaderColorMode::SubpixelDualSource,
                                )
                            }
                            GlyphFormat::Alpha |
                            GlyphFormat::TransformedAlpha |
                            GlyphFormat::Bitmap => {
                                (
                                    BlendMode::PremultipliedAlpha,
                                    ShaderColorMode::Alpha,
                                )
                            }
                            GlyphFormat::ColorBitmap => {
                                (
                                    BlendMode::PremultipliedAlpha,
                                    if run.shadow {
                                        // Ignore color and only sample alpha when shadowing.
                                        ShaderColorMode::BitmapShadow
                                    } else {
                                        ShaderColorMode::ColorBitmap
                                    },
                                )
                            }
                        };

                        // Calculate a tighter bounding rect of just the glyphs passed to this
                        // callback from request_glyphs(), rather than using the bounds of the
                        // entire text run. This improves batching when glyphs are fragmented
                        // over multiple textures in the texture cache.
                        // This code is taken from the ps_text_run shader.
                        let tight_bounding_rect = {
                            let snap_bias = match subpx_dir {
                                SubpixelDirection::None => DeviceVector2D::new(0.5, 0.5),
                                SubpixelDirection::Horizontal => DeviceVector2D::new(0.125, 0.5),
                                SubpixelDirection::Vertical => DeviceVector2D::new(0.5, 0.125),
                                SubpixelDirection::Mixed => DeviceVector2D::new(0.125, 0.125),
                            };
                            let text_offset = prim_header.local_rect.max.to_vector();

                            let pic_bounding_rect = if run.used_font.flags.contains(FontInstanceFlags::TRANSFORM_GLYPHS) {
                                let mut device_bounding_rect = DeviceRect::default();

                                let glyph_transform = ctx.spatial_tree.get_relative_transform(
                                    prim_spatial_node_index,
                                    root_spatial_node_index,
                                ).into_transform()
                                    .with_destination::<WorldPixel>()
                                    .then(&euclid::Transform3D::from_scale(ctx.global_device_pixel_scale));

                                let glyph_translation = DeviceVector2D::new(glyph_transform.m41, glyph_transform.m42);

                                let mut use_tight_bounding_rect = true;
                                for glyph in glyphs {
                                    let glyph_offset = prim_data.glyphs[glyph.index_in_text_run as usize].point + prim_header.local_rect.min.to_vector();

                                    let transformed_offset = match glyph_transform.transform_point2d(glyph_offset) {
                                        Some(transformed_offset) => transformed_offset,
                                        None => {
                                            use_tight_bounding_rect = false;
                                            break;
                                        }
                                    };
                                    let raster_glyph_offset = (transformed_offset + snap_bias).floor();
                                    let raster_text_offset = (
                                        glyph_transform.transform_vector2d(text_offset) +
                                        glyph_translation +
                                        DeviceVector2D::new(0.5, 0.5)
                                    ).floor() - glyph_translation;

                                    let device_glyph_rect = DeviceRect::from_origin_and_size(
                                        glyph.offset + raster_glyph_offset.to_vector() + raster_text_offset,
                                        glyph.size.to_f32(),
                                    );

                                    device_bounding_rect = device_bounding_rect.union(&device_glyph_rect);
                                }

                                if use_tight_bounding_rect {
                                    let map_device_to_surface: SpaceMapper<PicturePixel, DevicePixel> = SpaceMapper::new_with_target(
                                        root_spatial_node_index,
                                        surface_spatial_node_index,
                                        device_bounding_rect,
                                        ctx.spatial_tree,
                                    );

                                    match map_device_to_surface.unmap(&device_bounding_rect) {
                                        Some(r) => r.intersection(bounding_rect),
                                        None => Some(*bounding_rect),
                                    }
                                } else {
                                    Some(*bounding_rect)
                                }
                            } else {
                                let mut local_bounding_rect = LayoutRect::default();

                                let glyph_raster_scale = run.raster_scale * ctx.global_device_pixel_scale.get();

                                for glyph in glyphs {
                                    let glyph_offset = prim_data.glyphs[glyph.index_in_text_run as usize].point + prim_header.local_rect.min.to_vector();
                                    let glyph_scale = LayoutToDeviceScale::new(glyph_raster_scale / glyph.scale);
                                    let raster_glyph_offset = (glyph_offset * LayoutToDeviceScale::new(glyph_raster_scale) + snap_bias).floor() / glyph.scale;
                                    let local_glyph_rect = LayoutRect::from_origin_and_size(
                                        (glyph.offset + raster_glyph_offset.to_vector()) / glyph_scale + text_offset,
                                        glyph.size.to_f32() / glyph_scale,
                                    );

                                    local_bounding_rect = local_bounding_rect.union(&local_glyph_rect);
                                }

                                let map_prim_to_surface: SpaceMapper<LayoutPixel, PicturePixel> = SpaceMapper::new_with_target(
                                    surface_spatial_node_index,
                                    prim_spatial_node_index,
                                    *bounding_rect,
                                    ctx.spatial_tree,
                                );
                                map_prim_to_surface.map(&local_bounding_rect)
                            };

                            let intersected = match pic_bounding_rect {
                                // The text run may have been clipped, for example if part of it is offscreen.
                                // So intersect our result with the original bounding rect.
                                Some(rect) => rect.intersection(bounding_rect).unwrap_or_else(PictureRect::zero),
                                // If space mapping went off the rails, fall back to the old behavior.
                                //TODO: consider skipping the glyph run completely in this case.
                                None => *bounding_rect,
                            };

                            intersected
                        };

                        let key = BatchKey::new(kind, blend_mode, textures);

                        let batch = batcher.alpha_batch_list.set_params_and_get_batch(
                            key,
                            batch_features,
                            &tight_bounding_rect,
                            z_id,
                        );

                        batch.reserve(glyphs.len());
                        for glyph in glyphs {
                            batch.push(base_instance.build(
                                clip_task_address,
                                subpx_dir,
                                glyph.index_in_text_run,
                                glyph.uv_rect_address,
                                color_mode,
                            ));
                        }
                    },
                );
            }
            PrimitiveInstanceKind::LineDecoration { ref render_task, .. } => {
                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                let (batch_kind, textures, prim_user_data, specific_resource_address) = match render_task {
                    Some(task_id) => {
                        let (uv_rect_address, texture) = render_tasks.resolve_location(*task_id, gpu_cache).unwrap();
                        let textures = BatchTextures::prim_textured(
                            texture,
                            clip_mask_texture_id,
                        );
                        (
                            BrushBatchKind::Image(texture.image_buffer_kind()),
                            textures,
                            ImageBrushData {
                                color_mode: ShaderColorMode::Image,
                                alpha_type: AlphaType::PremultipliedAlpha,
                                raster_space: RasterizationSpace::Local,
                                opacity: 1.0,
                            }.encode(),
                            uv_rect_address.as_int(),
                        )
                    }
                    None => {
                        (
                            BrushBatchKind::Solid,
                            BatchTextures::prim_untextured(clip_mask_texture_id),
                            [get_shader_opacity(1.0), 0, 0, 0],
                            0,
                        )
                    }
                };

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    prim_user_data,
                );

                let batch_key = BatchKey {
                    blend_mode,
                    kind: BatchKind::Brush(batch_kind),
                    textures,
                };

                self.add_brush_instance_to_batches(
                    batch_key,
                    batch_features,
                    bounding_rect,
                    z_id,
                    INVALID_SEGMENT_INDEX,
                    common_data.edge_aa_mask,
                    clip_task_address,
                    brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION,
                    prim_header_index,
                    specific_resource_address,
                );
            }
            PrimitiveInstanceKind::Rectangle { use_legacy_path, .. } => {
                debug_assert!(use_legacy_path);
                let batch_params = BrushBatchParameters::shared(
                    BrushBatchKind::Solid,
                    TextureSet::UNTEXTURED,
                    [get_shader_opacity(1.0), 0, 0, 0],
                    0,
                );

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    batch_params.prim_user_data,
                );

                self.add_segmented_prim_to_batch(
                    segments,
                    common_data.opacity,
                    &batch_params,
                    blend_mode,
                    batch_features,
                    brush_flags,
                    common_data.edge_aa_mask,
                    prim_header_index,
                    bounding_rect,
                    transform_kind,
                    z_id,
                    prim_info.clip_task_index,
                    ctx,
                    render_tasks,
                );
            }
            PrimitiveInstanceKind::YuvImage { data_handle, segment_instance_index, compositor_surface_kind, .. } => {
                if compositor_surface_kind.needs_cutout() {
                    self.add_compositor_surface_cutout(
                        prim_rect,
                        prim_info.clip_chain.local_clip_rect,
                        prim_info.clip_task_index,
                        transform_id,
                        z_id,
                        bounding_rect,
                        ctx,
                        gpu_cache,
                        render_tasks,
                        prim_headers,
                    );

                    return;
                }

                let yuv_image_data = &ctx.data_stores.yuv_image[data_handle].kind;
                let mut textures = TextureSet::UNTEXTURED;
                let mut uv_rect_addresses = [0; 3];

                //yuv channel
                let channel_count = yuv_image_data.format.get_plane_num();
                debug_assert!(channel_count <= 3);
                for channel in 0 .. channel_count {

                    let src_channel = render_tasks.resolve_location(yuv_image_data.src_yuv[channel], gpu_cache);

                    let (uv_rect_address, texture_source) = match src_channel {
                        Some(src) => src,
                        None => {
                            warn!("Warnings: skip a PrimitiveKind::YuvImage");
                            return;
                        }
                    };

                    textures.colors[channel] = texture_source;
                    uv_rect_addresses[channel] = uv_rect_address.as_int();
                }

                // All yuv textures should be the same type.
                let buffer_kind = textures.colors[0].image_buffer_kind();
                assert!(
                    textures.colors[1 .. yuv_image_data.format.get_plane_num()]
                        .iter()
                        .all(|&tid| buffer_kind == tid.image_buffer_kind())
                );

                let kind = BrushBatchKind::YuvImage(
                    buffer_kind,
                    yuv_image_data.format,
                    yuv_image_data.color_depth,
                    yuv_image_data.color_space,
                    yuv_image_data.color_range,
                );

                let batch_params = BrushBatchParameters::shared(
                    kind,
                    textures,
                    [
                        uv_rect_addresses[0],
                        uv_rect_addresses[1],
                        uv_rect_addresses[2],
                        0,
                    ],
                    0,
                );

                debug_assert_ne!(segment_instance_index, SegmentInstanceIndex::INVALID);

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address.unwrap(),
                    transform_id,
                };

                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    batch_params.prim_user_data,
                );

                self.add_segmented_prim_to_batch(
                    segments,
                    common_data.opacity,
                    &batch_params,
                    blend_mode,
                    batch_features,
                    brush_flags,
                    common_data.edge_aa_mask,
                    prim_header_index,
                    bounding_rect,
                    transform_kind,
                    z_id,
                    prim_info.clip_task_index,
                    ctx,
                    render_tasks,
                );
            }
            PrimitiveInstanceKind::Image { data_handle, image_instance_index, compositor_surface_kind, .. } => {
                if compositor_surface_kind.needs_cutout() {
                    self.add_compositor_surface_cutout(
                        prim_rect,
                        prim_info.clip_chain.local_clip_rect,
                        prim_info.clip_task_index,
                        transform_id,
                        z_id,
                        bounding_rect,
                        ctx,
                        gpu_cache,
                        render_tasks,
                        prim_headers,
                    );

                    return;
                }

                let image_data = &ctx.data_stores.image[data_handle].kind;
                let image_instance = &ctx.prim_store.images[image_instance_index];
                let prim_user_data = ImageBrushData {
                    color_mode: ShaderColorMode::Image,
                    alpha_type: image_data.alpha_type,
                    raster_space: RasterizationSpace::Local,
                    opacity: 1.0,
                }.encode();

                let blend_mode = if needs_blending {
                    match image_data.alpha_type {
                        AlphaType::PremultipliedAlpha => BlendMode::PremultipliedAlpha,
                        AlphaType::Alpha => BlendMode::Alpha,
                    }
                } else {
                    BlendMode::None
                };

                if image_instance.visible_tiles.is_empty() {
                    if cfg!(debug_assertions) {
                        match ctx.resource_cache.get_image_properties(image_data.key) {
                            Some(ImageProperties { tiling: None, .. }) | None => (),
                            other => panic!("Non-tiled image with no visible images detected! Properties {:?}", other),
                        }
                    }

                    let src_color = render_tasks.resolve_location(image_instance.src_color, gpu_cache);

                    let (uv_rect_address, texture_source) = match src_color {
                        Some(src) => src,
                        None => {
                            return;
                        }
                    };

                    let batch_params = BrushBatchParameters::shared(
                        BrushBatchKind::Image(texture_source.image_buffer_kind()),
                        TextureSet::prim_textured(texture_source),
                        prim_user_data,
                        uv_rect_address.as_int(),
                    );

                    debug_assert_ne!(image_instance.segment_instance_index, SegmentInstanceIndex::INVALID);
                    let (prim_cache_address, segments) = if image_instance.segment_instance_index == SegmentInstanceIndex::UNUSED {
                        (prim_cache_address.unwrap(), None)
                    } else {
                        let segment_instance = &ctx.scratch.segment_instances[image_instance.segment_instance_index];
                        let segments = Some(&ctx.scratch.segments[segment_instance.segments_range]);
                        (gpu_cache.get_address(&segment_instance.gpu_cache_handle), segments)
                    };

                    let local_rect = image_instance.adjustment.map_local_rect(&prim_rect);
                    let local_clip_rect = image_instance.tight_local_clip_rect
                        .intersection_unchecked(&local_rect);

                    let prim_header = PrimitiveHeader {
                        local_rect,
                        local_clip_rect,
                        specific_prim_address: prim_cache_address,
                        transform_id,
                    };

                    let prim_header_index = prim_headers.push(
                        &prim_header,
                        z_id,
                        self.batcher.render_task_address,
                        batch_params.prim_user_data,
                    );

                    let brush_flags = match image_instance.normalized_uvs {
                        true => brush_flags | BrushFlags::NORMALIZED_UVS,
                        false => brush_flags,
                    };

                    self.add_segmented_prim_to_batch(
                        segments,
                        common_data.opacity,
                        &batch_params,
                        blend_mode,
                        batch_features,
                        brush_flags,
                        common_data.edge_aa_mask,
                        prim_header_index,
                        bounding_rect,
                        transform_kind,
                        z_id,
                        prim_info.clip_task_index,
                        ctx,
                        render_tasks,
                    );
                } else {
                    const VECS_PER_SPECIFIC_BRUSH: usize = 3;
                    let max_tiles_per_header = (MAX_VERTEX_TEXTURE_WIDTH - VECS_PER_SPECIFIC_BRUSH) / VECS_PER_SEGMENT;

                    let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                        prim_info.clip_task_index,
                        render_tasks,
                    ).unwrap();

                    // use temporary block storage since we don't know the number of visible tiles beforehand
                    let mut gpu_blocks = Vec::<GpuBlockData>::with_capacity(3 + max_tiles_per_header * 2);
                    for chunk in image_instance.visible_tiles.chunks(max_tiles_per_header) {
                        gpu_blocks.clear();
                        gpu_blocks.push(image_data.color.premultiplied().into()); //color
                        gpu_blocks.push(PremultipliedColorF::WHITE.into()); //bg color
                        gpu_blocks.push([-1.0, 0.0, 0.0, 0.0].into()); //stretch size
                        // negative first value makes the shader code ignore it and use the local size instead
                        for tile in chunk {
                            let tile_rect = tile.local_rect.translate(-prim_rect.min.to_vector());
                            gpu_blocks.push(tile_rect.into());
                            gpu_blocks.push(GpuBlockData::EMPTY);
                        }

                        let gpu_handle = gpu_cache.push_per_frame_blocks(&gpu_blocks);
                        let prim_header = PrimitiveHeader {
                            local_rect: prim_rect,
                            local_clip_rect: image_instance.tight_local_clip_rect,
                            specific_prim_address: gpu_cache.get_address(&gpu_handle),
                            transform_id,
                        };
                        let prim_header_index = prim_headers.push(
                            &prim_header,
                            z_id,
                            self.batcher.render_task_address,
                            prim_user_data,
                        );

                        for (i, tile) in chunk.iter().enumerate() {
                            let (uv_rect_address, texture) = match render_tasks.resolve_location(tile.src_color, gpu_cache) {
                                Some(result) => result,
                                None => {
                                    return;
                                }
                            };

                            let textures = BatchTextures::prim_textured(
                                texture,
                                clip_mask_texture_id,
                            );

                            let batch_key = BatchKey {
                                blend_mode,
                                kind: BatchKind::Brush(BrushBatchKind::Image(texture.image_buffer_kind())),
                                textures,
                            };

                            self.add_brush_instance_to_batches(
                                batch_key,
                                batch_features,
                                bounding_rect,
                                z_id,
                                i as i32,
                                tile.edge_flags,
                                clip_task_address,
                                brush_flags | BrushFlags::SEGMENT_RELATIVE | BrushFlags::PERSPECTIVE_INTERPOLATION,
                                prim_header_index,
                                uv_rect_address.as_int(),
                            );
                        }
                    }
                }
            }
            PrimitiveInstanceKind::LinearGradient { data_handle, ref visible_tiles_range, .. } => {
                let prim_data = &ctx.data_stores.linear_grad[data_handle];

                let mut prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: GpuCacheAddress::INVALID,
                    transform_id,
                };

                let user_data = [extra_prim_gpu_address.unwrap(), 0, 0, 0];

                if visible_tiles_range.is_empty() {
                    let batch_params = BrushBatchParameters::shared(
                        BrushBatchKind::LinearGradient,
                        TextureSet::UNTEXTURED,
                        user_data,
                        0,
                    );

                    prim_header.specific_prim_address = gpu_cache.get_address(&prim_data.gpu_cache_handle);

                    let prim_header_index = prim_headers.push(
                        &prim_header,
                        z_id,
                        self.batcher.render_task_address,
                        user_data,
                    );

                    let segments = if prim_data.brush_segments.is_empty() {
                        None
                    } else {
                        Some(prim_data.brush_segments.as_slice())
                    };
                    self.add_segmented_prim_to_batch(
                        segments,
                        prim_data.opacity,
                        &batch_params,
                        blend_mode,
                        batch_features,
                        brush_flags,
                        prim_data.edge_aa_mask,
                        prim_header_index,
                        bounding_rect,
                        transform_kind,
                        z_id,
                        prim_info.clip_task_index,
                        ctx,
                        render_tasks,
                    );
                } else {
                    let visible_tiles = &ctx.scratch.gradient_tiles[*visible_tiles_range];

                    let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                        prim_info.clip_task_index,
                        render_tasks,
                    ).unwrap();

                    let key = BatchKey {
                        blend_mode,
                        kind: BatchKind::Brush(BrushBatchKind::LinearGradient),
                        textures: BatchTextures::prim_untextured(clip_mask_texture_id),
                    };

                    for tile in visible_tiles {
                        let tile_prim_header = PrimitiveHeader {
                            specific_prim_address: gpu_cache.get_address(&tile.handle),
                            local_rect: tile.local_rect,
                            local_clip_rect: tile.local_clip_rect,
                            ..prim_header
                        };
                        let prim_header_index = prim_headers.push(
                            &tile_prim_header,
                            z_id,
                            self.batcher.render_task_address,
                            user_data,
                        );

                        self.add_brush_instance_to_batches(
                            key,
                            batch_features,
                            bounding_rect,
                            z_id,
                            INVALID_SEGMENT_INDEX,
                            prim_data.edge_aa_mask,
                            clip_task_address,
                            brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION,
                            prim_header_index,
                            0,
                        );
                    }
                }
            }
            PrimitiveInstanceKind::BackdropCapture { .. } => {}
            PrimitiveInstanceKind::BackdropRender { pic_index, .. } => {
                let prim_cache_address = gpu_cache.get_address(&ctx.globals.default_image_handle);
                let blend_mode = BlendMode::PremultipliedAlpha;
                let pic_task_id = ctx.prim_store.pictures[pic_index.0].primary_render_task_id;

                let prim_header = PrimitiveHeader {
                    local_rect: prim_rect,
                    local_clip_rect: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: prim_cache_address,
                    transform_id,
                };

                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                let kind = BatchKind::Brush(
                    BrushBatchKind::Image(ImageBufferKind::Texture2D)
                );
                let (_, texture) = render_tasks.resolve_location(
                    pic_task_id,
                    gpu_cache,
                ).unwrap();
                let textures = BatchTextures::prim_textured(
                    texture,
                    clip_mask_texture_id,
                );
                let key = BatchKey::new(
                    kind,
                    blend_mode,
                    textures,
                );
                let prim_header_index = prim_headers.push(
                    &prim_header,
                    z_id,
                    self.batcher.render_task_address,
                    ImageBrushData {
                        color_mode: ShaderColorMode::Image,
                        alpha_type: AlphaType::PremultipliedAlpha,
                        raster_space: RasterizationSpace::Screen,
                        opacity: 1.0,
                    }.encode(),
                );

                let pic_task = &render_tasks[pic_task_id.unwrap()];
                let pic_info = match pic_task.kind {
                    RenderTaskKind::Picture(ref info) => info,
                    _ => panic!("bug: not a picture"),
                };
                let target_rect = pic_task.get_target_rect();

                let backdrop_rect = DeviceRect::from_origin_and_size(
                    pic_info.content_origin,
                    target_rect.size().to_f32(),
                );

                let map_prim_to_backdrop = SpaceMapper::new_with_target(
                    pic_info.surface_spatial_node_index,
                    prim_spatial_node_index,
                    WorldRect::max_rect(),
                    ctx.spatial_tree,
                );

                let points = [
                    map_prim_to_backdrop.map_point(prim_rect.top_left()),
                    map_prim_to_backdrop.map_point(prim_rect.top_right()),
                    map_prim_to_backdrop.map_point(prim_rect.bottom_left()),
                    map_prim_to_backdrop.map_point(prim_rect.bottom_right()),
                ];

                if points.iter().any(|p| p.is_none()) {
                    return;
                }

                let uvs = [
                    calculate_screen_uv(points[0].unwrap() * pic_info.device_pixel_scale, backdrop_rect),
                    calculate_screen_uv(points[1].unwrap() * pic_info.device_pixel_scale, backdrop_rect),
                    calculate_screen_uv(points[2].unwrap() * pic_info.device_pixel_scale, backdrop_rect),
                    calculate_screen_uv(points[3].unwrap() * pic_info.device_pixel_scale, backdrop_rect),
                ];

                // TODO (gw): This is a hack that provides the GPU cache blocks for an
                //            ImageSource. We should update the GPU cache interfaces to
                //            allow pushing per-frame blocks via a request interface.
                let gpu_blocks = &[
                    GpuBlockData::from([
                        target_rect.min.x as f32,
                        target_rect.min.y as f32,
                        target_rect.max.x as f32,
                        target_rect.max.y as f32,
                    ]),
                    GpuBlockData::from([0.0; 4]),
                    GpuBlockData::from(uvs[0]),
                    GpuBlockData::from(uvs[1]),
                    GpuBlockData::from(uvs[2]),
                    GpuBlockData::from(uvs[3]),
                ];
                let uv_rect_handle = gpu_cache.push_per_frame_blocks(gpu_blocks);

                self.add_brush_instance_to_batches(
                    key,
                    batch_features,
                    bounding_rect,
                    z_id,
                    INVALID_SEGMENT_INDEX,
                    EdgeAaSegmentMask::all(),
                    clip_task_address,
                    brush_flags,
                    prim_header_index,
                    uv_rect_handle.as_int(gpu_cache),
                );
            }
        }
    }

    /// Draw a (potentially masked) alpha cutout so that a video underlay will be blended
    /// through by the compositor
    fn add_compositor_surface_cutout(
        &mut self,
        prim_rect: LayoutRect,
        local_clip_rect: LayoutRect,
        clip_task_index: ClipTaskIndex,
        transform_id: TransformPaletteId,
        z_id: ZBufferId,
        bounding_rect: &PictureRect,
        ctx: &RenderTargetContext,
        gpu_cache: &mut GpuCache,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
    ) {
        let prim_cache_address = gpu_cache.get_address(&ctx.globals.default_black_rect_handle);

        let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
            clip_task_index,
            render_tasks,
        ).unwrap();

        let prim_header = PrimitiveHeader {
            local_rect: prim_rect,
            local_clip_rect,
            specific_prim_address: prim_cache_address,
            transform_id,
        };

        let prim_header_index = prim_headers.push(
            &prim_header,
            z_id,
            self.batcher.render_task_address,
            [get_shader_opacity(1.0), 0, 0, 0],
        );

        let batch_key = BatchKey {
            blend_mode: BlendMode::PremultipliedDestOut,
            kind: BatchKind::Brush(BrushBatchKind::Solid),
            textures: BatchTextures::prim_untextured(clip_mask_texture_id),
        };

        self.add_brush_instance_to_batches(
            batch_key,
            BatchFeatures::ALPHA_PASS | BatchFeatures::CLIP_MASK,
            bounding_rect,
            z_id,
            INVALID_SEGMENT_INDEX,
            EdgeAaSegmentMask::empty(),
            clip_task_address,
            BrushFlags::empty(),
            prim_header_index,
            0,
        );
    }

    /// Add a single segment instance to a batch.
    ///
    /// `edge_aa_mask` Specifies the edges that are *allowed* to have anti-aliasing, if and only
    /// if the segments enable it.
    /// In other words passing EdgeAaSegmentFlags::all() does not necessarily mean all edges will
    /// be anti-aliased, only that they could be.
    fn add_segment_to_batch(
        &mut self,
        segment: &BrushSegment,
        segment_data: &SegmentInstanceData,
        segment_index: i32,
        batch_kind: BrushBatchKind,
        prim_header_index: PrimitiveHeaderIndex,
        alpha_blend_mode: BlendMode,
        features: BatchFeatures,
        brush_flags: BrushFlags,
        edge_aa_mask: EdgeAaSegmentMask,
        bounding_rect: &PictureRect,
        transform_kind: TransformedRectKind,
        z_id: ZBufferId,
        prim_opacity: PrimitiveOpacity,
        clip_task_index: ClipTaskIndex,
        ctx: &RenderTargetContext,
        render_tasks: &RenderTaskGraph,
    ) {
        debug_assert!(clip_task_index != ClipTaskIndex::INVALID);

        // Get GPU address of clip task for this segment, or None if
        // the entire segment is clipped out.
        if let Some((clip_task_address, clip_mask)) = ctx.get_clip_task_and_texture(
            clip_task_index,
            segment_index,
            render_tasks,
        ) {
            // If a got a valid (or OPAQUE) clip task address, add the segment.
            let is_inner = segment.edge_flags.is_empty();
            let needs_blending = !prim_opacity.is_opaque ||
                                 clip_task_address != OPAQUE_TASK_ADDRESS ||
                                 (!is_inner && transform_kind == TransformedRectKind::Complex) ||
                                 brush_flags.contains(BrushFlags::FORCE_AA);

            let textures = BatchTextures {
                input: segment_data.textures,
                clip_mask,
            };

            let batch_key = BatchKey {
                blend_mode: if needs_blending { alpha_blend_mode } else { BlendMode::None },
                kind: BatchKind::Brush(batch_kind),
                textures,
            };

            self.add_brush_instance_to_batches(
                batch_key,
                features,
                bounding_rect,
                z_id,
                segment_index,
                segment.edge_flags & edge_aa_mask,
                clip_task_address,
                brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION | segment.brush_flags,
                prim_header_index,
                segment_data.specific_resource_address,
            );
        }
    }

    /// Add any segment(s) from a brush to batches.
    ///
    /// `edge_aa_mask` Specifies the edges that are *allowed* to have anti-aliasing, if and only
    /// if the segments enable it.
    /// In other words passing EdgeAaSegmentFlags::all() does not necessarily mean all edges will
    /// be anti-aliased, only that they could be.
    fn add_segmented_prim_to_batch(
        &mut self,
        brush_segments: Option<&[BrushSegment]>,
        prim_opacity: PrimitiveOpacity,
        params: &BrushBatchParameters,
        blend_mode: BlendMode,
        features: BatchFeatures,
        brush_flags: BrushFlags,
        edge_aa_mask: EdgeAaSegmentMask,
        prim_header_index: PrimitiveHeaderIndex,
        bounding_rect: &PictureRect,
        transform_kind: TransformedRectKind,
        z_id: ZBufferId,
        clip_task_index: ClipTaskIndex,
        ctx: &RenderTargetContext,
        render_tasks: &RenderTaskGraph,
    ) {
        match (brush_segments, &params.segment_data) {
            (Some(ref brush_segments), SegmentDataKind::Instanced(ref segment_data)) => {
                // In this case, we have both a list of segments, and a list of
                // per-segment instance data. Zip them together to build batches.
                debug_assert_eq!(brush_segments.len(), segment_data.len());
                for (segment_index, (segment, segment_data)) in brush_segments
                    .iter()
                    .zip(segment_data.iter())
                    .enumerate()
                {
                    self.add_segment_to_batch(
                        segment,
                        segment_data,
                        segment_index as i32,
                        params.batch_kind,
                        prim_header_index,
                        blend_mode,
                        features,
                        brush_flags,
                        edge_aa_mask,
                        bounding_rect,
                        transform_kind,
                        z_id,
                        prim_opacity,
                        clip_task_index,
                        ctx,
                        render_tasks,
                    );
                }
            }
            (Some(ref brush_segments), SegmentDataKind::Shared(ref segment_data)) => {
                // A list of segments, but the per-segment data is common
                // between all segments.
                for (segment_index, segment) in brush_segments
                    .iter()
                    .enumerate()
                {
                    self.add_segment_to_batch(
                        segment,
                        segment_data,
                        segment_index as i32,
                        params.batch_kind,
                        prim_header_index,
                        blend_mode,
                        features,
                        brush_flags,
                        edge_aa_mask,
                        bounding_rect,
                        transform_kind,
                        z_id,
                        prim_opacity,
                        clip_task_index,
                        ctx,
                        render_tasks,
                    );
                }
            }
            (None, SegmentDataKind::Shared(ref segment_data)) => {
                // No segments, and thus no per-segment instance data.
                // Note: the blend mode already takes opacity into account

                let (clip_task_address, clip_mask) = ctx.get_prim_clip_task_and_texture(
                    clip_task_index,
                    render_tasks,
                ).unwrap();

                let textures = BatchTextures {
                    input: segment_data.textures,
                    clip_mask,
                };

                let batch_key = BatchKey {
                    blend_mode,
                    kind: BatchKind::Brush(params.batch_kind),
                    textures,
                };

                self.add_brush_instance_to_batches(
                    batch_key,
                    features,
                    bounding_rect,
                    z_id,
                    INVALID_SEGMENT_INDEX,
                    edge_aa_mask,
                    clip_task_address,
                    brush_flags | BrushFlags::PERSPECTIVE_INTERPOLATION,
                    prim_header_index,
                    segment_data.specific_resource_address,
                );
            }
            (None, SegmentDataKind::Instanced(..)) => {
                // We should never hit the case where there are no segments,
                // but a list of segment instance data.
                unreachable!();
            }
        }
    }
}

/// Either a single texture / user data for all segments,
/// or a list of one per segment.
enum SegmentDataKind {
    Shared(SegmentInstanceData),
    Instanced(SmallVec<[SegmentInstanceData; 8]>),
}

/// The parameters that are specific to a kind of brush,
/// used by the common method to add a brush to batches.
struct BrushBatchParameters {
    batch_kind: BrushBatchKind,
    prim_user_data: [i32; 4],
    segment_data: SegmentDataKind,
}

impl BrushBatchParameters {
    /// This brush instance has a list of per-segment
    /// instance data.
    fn instanced(
        batch_kind: BrushBatchKind,
        prim_user_data: [i32; 4],
        segment_data: SmallVec<[SegmentInstanceData; 8]>,
    ) -> Self {
        BrushBatchParameters {
            batch_kind,
            prim_user_data,
            segment_data: SegmentDataKind::Instanced(segment_data),
        }
    }

    /// This brush instance shares the per-segment data
    /// across all segments.
    fn shared(
        batch_kind: BrushBatchKind,
        textures: TextureSet,
        prim_user_data: [i32; 4],
        specific_resource_address: i32,
    ) -> Self {
        BrushBatchParameters {
            batch_kind,
            prim_user_data,
            segment_data: SegmentDataKind::Shared(
                SegmentInstanceData {
                    textures,
                    specific_resource_address,
                }
            ),
        }
    }
}

/// A list of clip instances to be drawn into a target.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipMaskInstanceList {
    pub mask_instances_fast: FrameVec<MaskInstance>,
    pub mask_instances_slow: FrameVec<MaskInstance>,

    pub mask_instances_fast_with_scissor: FastHashMap<DeviceIntRect, FrameVec<MaskInstance>>,
    pub mask_instances_slow_with_scissor: FastHashMap<DeviceIntRect, FrameVec<MaskInstance>>,

    pub image_mask_instances: FastHashMap<TextureSource, FrameVec<PrimitiveInstanceData>>,
    pub image_mask_instances_with_scissor: FastHashMap<(DeviceIntRect, TextureSource), FrameVec<PrimitiveInstanceData>>,
}

impl ClipMaskInstanceList {
    pub fn new(memory: &FrameMemory) -> Self {
        ClipMaskInstanceList {
            mask_instances_fast: memory.new_vec(),
            mask_instances_slow: memory.new_vec(),
            mask_instances_fast_with_scissor: FastHashMap::default(),
            mask_instances_slow_with_scissor: FastHashMap::default(),
            image_mask_instances: FastHashMap::default(),
            image_mask_instances_with_scissor: FastHashMap::default(),
        }
    }

    pub fn is_empty(&self) -> bool {
        // Destructure self to make sure we don't forget to update this method if
        // a new member is added.
        let ClipMaskInstanceList {
            mask_instances_fast,
            mask_instances_slow,
            mask_instances_fast_with_scissor,
            mask_instances_slow_with_scissor,
            image_mask_instances,
            image_mask_instances_with_scissor,
        } = self;

        mask_instances_fast.is_empty()
            && mask_instances_slow.is_empty()
            && mask_instances_fast_with_scissor.is_empty()
            && mask_instances_slow_with_scissor.is_empty()
            && image_mask_instances.is_empty()
            && image_mask_instances_with_scissor.is_empty()
    }
}

/// A list of clip instances to be drawn into a target.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipBatchList {
    /// Rectangle draws fill up the rectangles with rounded corners.
    pub slow_rectangles: FrameVec<ClipMaskInstanceRect>,
    pub fast_rectangles: FrameVec<ClipMaskInstanceRect>,
    pub box_shadows: FastHashMap<TextureSource, FrameVec<ClipMaskInstanceBoxShadow>>,
}

impl ClipBatchList {
    fn new(memory: &FrameMemory) -> Self {
        ClipBatchList {
            slow_rectangles: memory.new_vec(),
            fast_rectangles: memory.new_vec(),
            box_shadows: FastHashMap::default(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.slow_rectangles.is_empty()
          && self.fast_rectangles.is_empty()
          && self.box_shadows.is_empty()
    }
}

/// Batcher managing draw calls into the clip mask (in the RT cache).
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipBatcher {
    /// The first clip in each clip task. This will overwrite all pixels
    /// in the clip region, so we can skip doing a clear and write with
    /// blending disabled, which is a big performance win on Intel GPUs.
    pub primary_clips: ClipBatchList,
    /// Any subsequent clip masks (rare) for a clip task get drawn in
    /// a second pass with multiplicative blending enabled.
    pub secondary_clips: ClipBatchList,

    gpu_supports_fast_clears: bool,
}

impl ClipBatcher {
    pub fn new(
        gpu_supports_fast_clears: bool,
        memory: &FrameMemory,
    ) -> Self {
        ClipBatcher {
            primary_clips: ClipBatchList::new(memory),
            secondary_clips: ClipBatchList::new(memory),
            gpu_supports_fast_clears,
        }
    }

    pub fn add_clip_region(
        &mut self,
        local_pos: LayoutPoint,
        sub_rect: DeviceRect,
        clip_data: ClipData,
        task_origin: DevicePoint,
        screen_origin: DevicePoint,
        device_pixel_scale: f32,
    ) {
        let instance = ClipMaskInstanceRect {
            common: ClipMaskInstanceCommon {
                clip_transform_id: TransformPaletteId::IDENTITY,
                prim_transform_id: TransformPaletteId::IDENTITY,
                sub_rect,
                task_origin,
                screen_origin,
                device_pixel_scale,
            },
            local_pos,
            clip_data,
        };

        self.primary_clips.slow_rectangles.push(instance);
    }

    /// Where appropriate, draw a clip rectangle as a small series of tiles,
    /// instead of one large rectangle.
    fn add_tiled_clip_mask(
        &mut self,
        mask_screen_rect: DeviceRect,
        local_clip_rect: LayoutRect,
        clip_spatial_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
        world_rect: &WorldRect,
        global_device_pixel_scale: DevicePixelScale,
        common: &ClipMaskInstanceCommon,
        is_first_clip: bool,
    ) -> bool {
        // Only try to draw in tiles if the clip mark is big enough.
        if mask_screen_rect.area() < CLIP_RECTANGLE_AREA_THRESHOLD {
            return false;
        }

        let mask_screen_rect_size = mask_screen_rect.size().to_i32();
        let clip_spatial_node = spatial_tree.get_spatial_node(clip_spatial_node_index);

        // Only support clips that are axis-aligned to the root coordinate space,
        // for now, to simplify the logic below. This handles the vast majority
        // of real world cases, but could be expanded in future if needed.
        if clip_spatial_node.coordinate_system_id != CoordinateSystemId::root() {
            return false;
        }

        // Get the world rect of the clip rectangle. If we can't transform it due
        // to the matrix, just fall back to drawing the entire clip mask.
        let transform = spatial_tree.get_world_transform(
            clip_spatial_node_index,
        );
        let world_clip_rect = match project_rect(
            &transform.into_transform(),
            &local_clip_rect,
            &world_rect,
        ) {
            Some(rect) => rect,
            None => return false,
        };

        // Work out how many tiles to draw this clip mask in, stretched across the
        // device rect of the primitive clip mask.
        let world_device_rect = world_clip_rect * global_device_pixel_scale;
        let x_tiles = (mask_screen_rect_size.width + CLIP_RECTANGLE_TILE_SIZE-1) / CLIP_RECTANGLE_TILE_SIZE;
        let y_tiles = (mask_screen_rect_size.height + CLIP_RECTANGLE_TILE_SIZE-1) / CLIP_RECTANGLE_TILE_SIZE;

        // Because we only run this code path for axis-aligned rects (the root coord system check above),
        // and only for rectangles (not rounded etc), the world_device_rect is not conservative - we know
        // that there is no inner_rect, and the world_device_rect should be the real, axis-aligned clip rect.
        let mask_origin = mask_screen_rect.min.to_vector();
        let clip_list = self.get_batch_list(is_first_clip);

        for y in 0 .. y_tiles {
            for x in 0 .. x_tiles {
                let p0 = DeviceIntPoint::new(
                    x * CLIP_RECTANGLE_TILE_SIZE,
                    y * CLIP_RECTANGLE_TILE_SIZE,
                );
                let p1 = DeviceIntPoint::new(
                    (p0.x + CLIP_RECTANGLE_TILE_SIZE).min(mask_screen_rect_size.width),
                    (p0.y + CLIP_RECTANGLE_TILE_SIZE).min(mask_screen_rect_size.height),
                );
                let normalized_sub_rect = DeviceIntRect {
                    min: p0,
                    max: p1,
                }.to_f32();
                let world_sub_rect = normalized_sub_rect.translate(mask_origin);

                // If the clip rect completely contains this tile rect, then drawing
                // these pixels would be redundant - since this clip can't possibly
                // affect the pixels in this tile, skip them!
                if !world_device_rect.contains_box(&world_sub_rect) {
                    clip_list.slow_rectangles.push(ClipMaskInstanceRect {
                        common: ClipMaskInstanceCommon {
                            sub_rect: normalized_sub_rect,
                            ..*common
                        },
                        local_pos: local_clip_rect.min,
                        clip_data: ClipData::uniform(local_clip_rect.size(), 0.0, ClipMode::Clip),
                    });
                }
            }
        }

        true
    }

    /// Retrieve the correct clip batch list to append to, depending
    /// on whether this is the first clip mask for a clip task.
    fn get_batch_list(
        &mut self,
        is_first_clip: bool,
    ) -> &mut ClipBatchList {
        if is_first_clip && !self.gpu_supports_fast_clears {
            &mut self.primary_clips
        } else {
            &mut self.secondary_clips
        }
    }

    pub fn add(
        &mut self,
        clip_node_range: ClipNodeRange,
        root_spatial_node_index: SpatialNodeIndex,
        render_tasks: &RenderTaskGraph,
        gpu_cache: &GpuCache,
        clip_store: &ClipStore,
        transforms: &mut TransformPalette,
        actual_rect: DeviceRect,
        surface_device_pixel_scale: DevicePixelScale,
        task_origin: DevicePoint,
        screen_origin: DevicePoint,
        ctx: &RenderTargetContext,
    ) -> bool {
        let mut is_first_clip = true;
        let mut clear_to_one = false;

        for i in 0 .. clip_node_range.count {
            let clip_instance = clip_store.get_instance_from_range(&clip_node_range, i);
            let clip_node = &ctx.data_stores.clip[clip_instance.handle];

            let clip_transform_id = transforms.get_id(
                clip_node.item.spatial_node_index,
                ctx.root_spatial_node_index,
                ctx.spatial_tree,
            );

            let prim_transform_id = transforms.get_id(
                root_spatial_node_index,
                ctx.root_spatial_node_index,
                ctx.spatial_tree,
            );

            let common = ClipMaskInstanceCommon {
                sub_rect: DeviceRect::from_size(actual_rect.size()),
                task_origin,
                screen_origin,
                device_pixel_scale: surface_device_pixel_scale.0,
                clip_transform_id,
                prim_transform_id,
            };

            let added_clip = match clip_node.item.kind {
                ClipItemKind::Image { .. } => {
                    unreachable!();
                }
                ClipItemKind::BoxShadow { ref source }  => {
                    let task_id = source
                        .render_task
                        .expect("bug: render task handle not allocated");
                    let (uv_rect_address, texture) = render_tasks.resolve_location(task_id, gpu_cache).unwrap();

                    self.get_batch_list(is_first_clip)
                        .box_shadows
                        .entry(texture)
                        .or_insert_with(|| ctx.frame_memory.new_vec())
                        .push(ClipMaskInstanceBoxShadow {
                            common,
                            resource_address: uv_rect_address,
                            shadow_data: BoxShadowData {
                                src_rect_size: source.original_alloc_size,
                                clip_mode: source.clip_mode as i32,
                                stretch_mode_x: source.stretch_mode_x as i32,
                                stretch_mode_y: source.stretch_mode_y as i32,
                                dest_rect: source.prim_shadow_rect,
                            },
                        });

                    true
                }
                ClipItemKind::Rectangle { rect, mode: ClipMode::ClipOut } => {
                    self.get_batch_list(is_first_clip)
                        .slow_rectangles
                        .push(ClipMaskInstanceRect {
                            common,
                            local_pos: rect.min,
                            clip_data: ClipData::uniform(rect.size(), 0.0, ClipMode::ClipOut),
                        });

                    true
                }
                ClipItemKind::Rectangle { rect, mode: ClipMode::Clip } => {
                    if clip_instance.flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM) {
                        false
                    } else {
                        if self.add_tiled_clip_mask(
                            actual_rect,
                            rect,
                            clip_node.item.spatial_node_index,
                            ctx.spatial_tree,
                            &ctx.screen_world_rect,
                            ctx.global_device_pixel_scale,
                            &common,
                            is_first_clip,
                        ) {
                            clear_to_one |= is_first_clip;
                        } else {
                            self.get_batch_list(is_first_clip)
                                .slow_rectangles
                                .push(ClipMaskInstanceRect {
                                    common,
                                    local_pos: rect.min,
                                    clip_data: ClipData::uniform(rect.size(), 0.0, ClipMode::Clip),
                                });
                        }

                        true
                    }
                }
                ClipItemKind::RoundedRectangle { rect, ref radius, mode, .. } => {
                    let batch_list = self.get_batch_list(is_first_clip);
                    let instance = ClipMaskInstanceRect {
                        common,
                        local_pos: rect.min,
                        clip_data: ClipData::rounded_rect(rect.size(), radius, mode),
                    };
                    if clip_instance.flags.contains(ClipNodeFlags::USE_FAST_PATH) {
                        batch_list.fast_rectangles.push(instance);
                    } else {
                        batch_list.slow_rectangles.push(instance);
                    }

                    true
                }
            };

            is_first_clip &= !added_clip;
        }

        clear_to_one
    }
}

impl<'a, 'rc> RenderTargetContext<'a, 'rc> {
    /// Retrieve the GPU task address for a given clip task instance.
    /// Returns None if the segment was completely clipped out.
    /// Returns Some(OPAQUE_TASK_ADDRESS) if no clip mask is needed.
    /// Returns Some(task_address) if there was a valid clip mask.
    fn get_clip_task_and_texture(
        &self,
        clip_task_index: ClipTaskIndex,
        offset: i32,
        render_tasks: &RenderTaskGraph,
    ) -> Option<(RenderTaskAddress, TextureSource)> {
        match self.scratch.clip_mask_instances[clip_task_index.0 as usize + offset as usize] {
            ClipMaskKind::Mask(task_id) => {
                Some((
                    task_id.into(),
                    TextureSource::TextureCache(
                        render_tasks[task_id].get_target_texture(),
                        Swizzle::default(),
                    )
                ))
            }
            ClipMaskKind::None => {
                Some((OPAQUE_TASK_ADDRESS, TextureSource::Invalid))
            }
            ClipMaskKind::Clipped => {
                None
            }
        }
    }

    /// Helper function to get the clip task address for a
    /// non-segmented primitive.
    fn get_prim_clip_task_and_texture(
        &self,
        clip_task_index: ClipTaskIndex,
        render_tasks: &RenderTaskGraph,
    ) -> Option<(RenderTaskAddress, TextureSource)> {
        self.get_clip_task_and_texture(
            clip_task_index,
            0,
            render_tasks,
        )
    }
}

impl CompositorSurfaceKind {
    /// Returns true if the type of compositor surface needs an alpha cutout rendered
    fn needs_cutout(&self) -> bool {
        match self {
            CompositorSurfaceKind::Underlay => true,
            CompositorSurfaceKind::Overlay | CompositorSurfaceKind::Blit => false,
        }
    }
}
