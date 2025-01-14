// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_PHYSICAL_BOX_FRAGMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_PHYSICAL_BOX_FRAGMENT_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/geometry/box_sides.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_box_strut.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_fragment_items.h"
#include "third_party/blink/renderer/core/layout/ng/mathml/ng_mathml_paint_info.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_container_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/table/ng_table_borders.h"
#include "third_party/blink/renderer/core/layout/ng/table/ng_table_fragment_data.h"
#include "third_party/blink/renderer/platform/graphics/overlay_scrollbar_clip_behavior.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class NGBlockBreakToken;
class NGBoxFragmentBuilder;
enum class NGOutlineType;

class CORE_EXPORT NGPhysicalBoxFragment final
    : public NGPhysicalContainerFragment {
 public:
  static scoped_refptr<const NGPhysicalBoxFragment> Create(
      NGBoxFragmentBuilder* builder,
      WritingMode block_or_line_writing_mode);
  // Creates a copy of |other| but uses the "post-layout" fragments to ensure
  // fragment-tree consistency.
  static scoped_refptr<const NGPhysicalBoxFragment>
  CloneWithPostLayoutFragments(const NGPhysicalBoxFragment& other,
                               const base::Optional<PhysicalRect>
                                   updated_layout_overflow = base::nullopt);

  using MulticolCollection = NGContainerFragmentBuilder::MulticolCollection;
  using PassKey = base::PassKey<NGPhysicalBoxFragment>;
  NGPhysicalBoxFragment(PassKey,
                        NGBoxFragmentBuilder* builder,
                        bool has_layout_overflow,
                        const PhysicalRect& layout_overflow,
                        bool has_borders,
                        const NGPhysicalBoxStrut& borders,
                        bool has_padding,
                        const NGPhysicalBoxStrut& padding,
                        const base::Optional<PhysicalRect>& inflow_bounds,
                        bool has_rare_data,
                        WritingMode block_or_line_writing_mode);

  NGPhysicalBoxFragment(PassKey,
                        const NGPhysicalBoxFragment& other,
                        bool has_layout_overflow,
                        const PhysicalRect& layout_overflow,
                        bool recalculate_layout_overflow);

  scoped_refptr<const NGLayoutResult> CloneAsHiddenForPaint() const;

  ~NGPhysicalBoxFragment() {
    ink_overflow_.Reset(InkOverflowType());
    if (has_fragment_items_)
      ComputeItemsAddress()->~NGFragmentItems();
    if (has_rare_data_)
      ComputeRareDataAddress()->~RareData();
    for (const NGLink& child : Children()) {
      if (child.fragment)
        child.fragment->Release();
    }
  }

  const NGPhysicalBoxFragment* PostLayout() const;

  // Returns |NGFragmentItems| if this fragment has one.
  bool HasItems() const { return has_fragment_items_; }
  const NGFragmentItems* Items() const {
    return has_fragment_items_ ? ComputeItemsAddress() : nullptr;
  }

  base::Optional<LayoutUnit> Baseline() const {
    if (has_baseline_)
      return baseline_;
    return base::nullopt;
  }

  base::Optional<LayoutUnit> LastBaseline() const {
    if (has_last_baseline_)
      return last_baseline_;
    return base::nullopt;
  }

  PhysicalRect TableGridRect() const {
    return ComputeRareDataAddress()->table_grid_rect;
  }

  const NGTableFragmentData::ColumnGeometries* TableColumnGeometries() const {
    return &ComputeRareDataAddress()->table_column_geometries;
  }

  const NGTableBorders* TableCollapsedBorders() const {
    return ComputeRareDataAddress()->table_collapsed_borders.get();
  }

  const NGTableFragmentData::CollapsedBordersGeometry*
  TableCollapsedBordersGeometry() const {
    return ComputeRareDataAddress()->table_collapsed_borders_geometry.get();
  }

  wtf_size_t TableCellColumnIndex() const {
    return ComputeRareDataAddress()->table_cell_column_index;
  }

  // Returns the layout-overflow for this fragment.
  const PhysicalRect LayoutOverflow() const {
    if (is_legacy_layout_root_)
      return To<LayoutBox>(GetLayoutObject())->PhysicalLayoutOverflowRect();
    if (!has_layout_overflow_)
      return {{}, Size()};
    return *ComputeLayoutOverflowAddress();
  }

  const NGPhysicalBoxStrut Borders() const {
    if (!has_borders_)
      return NGPhysicalBoxStrut();
    return *ComputeBordersAddress();
  }

  const NGPhysicalBoxStrut Padding() const {
    if (!has_padding_)
      return NGPhysicalBoxStrut();
    return *ComputePaddingAddress();
  }

  // Returns the bounds of any inflow children for this fragment (specifically
  // no out-of-flow positioned objects). This will return |base::nullopt| if:
  //  - The fragment is *not* a scroll container.
  //  - The scroll container contains no inflow children.
  // This is normally the union of all inflow children's border-box rects
  // (without relative positioning applied), however for grid layout it is the
  // size and position of the grid instead.
  // This is used for scrollable overflow calculations.
  const base::Optional<PhysicalRect> InflowBounds() const {
    if (!has_inflow_bounds_)
      return base::nullopt;
    return *ComputeInflowBoundsAddress();
  }

  bool HasOutOfFlowPositionedFragmentainerDescendants() const {
    if (!has_rare_data_)
      return false;

    return !ComputeRareDataAddress()
                ->oof_positioned_fragmentainer_descendants.IsEmpty();
  }

  base::span<NGPhysicalOutOfFlowPositionedNode>
  OutOfFlowPositionedFragmentainerDescendants() const {
    if (!has_rare_data_)
      return base::span<NGPhysicalOutOfFlowPositionedNode>();
    Vector<NGPhysicalOutOfFlowPositionedNode>& descendants =
        const_cast<Vector<NGPhysicalOutOfFlowPositionedNode>&>(
            ComputeRareDataAddress()->oof_positioned_fragmentainer_descendants);
    return {descendants.data(), descendants.size()};
  }

  bool HasMulticolsWithPendingOOFs() const {
    if (!has_rare_data_)
      return false;

    return !ComputeRareDataAddress()->multicols_with_pending_oofs.IsEmpty();
  }

  MulticolCollection MulticolsWithPendingOOFs() const {
    if (!has_rare_data_)
      return MulticolCollection();
    return const_cast<MulticolCollection&>(
        ComputeRareDataAddress()->multicols_with_pending_oofs);
  }

  NGPixelSnappedPhysicalBoxStrut PixelSnappedPadding() const {
    if (!has_padding_)
      return NGPixelSnappedPhysicalBoxStrut();
    return ComputePaddingAddress()->SnapToDevicePixels();
  }

  // Return true if this is either a container that establishes an inline
  // formatting context, or if it's non-atomic inline content participating in
  // one. Empty blocks don't establish an inline formatting context.
  //
  // The return value from this method is undefined and irrelevant if the object
  // establishes a different type of formatting context than block/inline, such
  // as table or flexbox.
  //
  // Example:
  // <div>                                       <!-- false -->
  //   <div>                                     <!-- true -->
  //     <div style="float:left;"></div>         <!-- false -->
  //     <div style="float:left;">               <!-- true -->
  //       xxx                                   <!-- true -->
  //     </div>
  //     <div style="float:left;">               <!-- false -->
  //       <div style="float:left;"></div>       <!-- false -->
  //     </div>
  //     <span>                                  <!-- true -->
  //       xxx                                   <!-- true -->
  //       <span style="display:inline-block;">  <!-- false -->
  //         <div></div>                         <!-- false -->
  //       </span>
  //       <span style="display:inline-block;">  <!-- true -->
  //         xxx                                 <!-- true -->
  //       </span>
  //       <span style="display:inline-flex;">   <!-- N/A -->
  bool IsInlineFormattingContext() const {
    return is_inline_formatting_context_;
  }

  // The |LayoutBox| whose |PhysicalFragments()| contains |this|. This is
  // different from |GetLayoutObject()| if |this.IsColumnBox()|.
  const LayoutBox* OwnerLayoutBox() const;
  LayoutBox* MutableOwnerLayoutBox() const;

  // Returns the offset in the |OwnerLayoutBox| coordinate system.
  PhysicalOffset OffsetFromOwnerLayoutBox() const;

  PhysicalRect ScrollableOverflow(TextHeightType height_type) const;
  PhysicalRect ScrollableOverflowFromChildren(TextHeightType height_type) const;

  OverflowClipAxes GetOverflowClipAxes() const {
    if (const auto* layout_object = GetLayoutObject())
      return layout_object->GetOverflowClipAxes();
    return kNoOverflowClip;
  }

  // TODO(layout-dev): These three methods delegate to legacy layout for now,
  // update them to use LayoutNG based overflow information from the fragment
  // and change them to use NG geometry types once LayoutNG supports overflow.
  PhysicalRect OverflowClipRect(
      const PhysicalOffset& location,
      OverlayScrollbarClipBehavior = kIgnoreOverlayScrollbarSize) const;
  PhysicalRect OverflowClipRect(
      const PhysicalOffset& location,
      const NGBlockBreakToken* incoming_break_token,
      OverlayScrollbarClipBehavior = kIgnoreOverlayScrollbarSize) const;
  LayoutSize PixelSnappedScrolledContentOffset() const;
  PhysicalSize ScrollSize() const;

  NGInkOverflow::Type InkOverflowType() const {
    return static_cast<NGInkOverflow::Type>(ink_overflow_type_);
  }
  bool HasInkOverflow() const {
    return InkOverflowType() != NGInkOverflow::kNone;
  }

  // 3 types of ink overflows:
  // * |SelfInkOverflow| includes box decorations that are outside of the
  //   border box.
  //   Returns |LocalRect| when there are no overflow.
  // * |ContentsInkOverflow| includes anything that would bleed out of the box
  //   and would be clipped by the overflow clip ('overflow' != visible). This
  //   corresponds to children that overflows their parent.
  //   Returns an empty rect when there are no overflow.
  // * |InkOverflow| includes self and contents ink overflow, unless it has
  //   clipping, in that case it includes self ink overflow only.
  //   Returns |LocalRect| when there are no overflow.
  PhysicalRect InkOverflow() const;
  PhysicalRect SelfInkOverflow() const;
  PhysicalRect ContentsInkOverflow() const;

  // Fast check if |NodeAtPoint| may find a hit.
  bool MayIntersect(const HitTestResult& result,
                    const HitTestLocation& hit_test_location,
                    const PhysicalOffset& accumulated_offset) const;

  // In order to paint united outline rectangles, the "owner" fragment paints
  // outlines for non-owner fragments.
  bool IsOutlineOwner() const {
    return !IsInlineBox() || InlineContainerFragmentIfOutlineOwner();
  }
  const NGPhysicalBoxFragment* InlineContainerFragmentIfOutlineOwner() const;

  // Returns the |ComputedStyle| to use for painting outlines. When |this| is
  // a block in a continuation-chain, it may need to paint outlines if its
  // ancestor inline boxes in the DOM tree has outlines.
  const ComputedStyle* StyleForContinuationOutline() const {
    if (const auto* layout_object = GetLayoutObject())
      return layout_object->StyleForContinuationOutline();
    return nullptr;
  }

  // Fragment offset is this fragment's offset from parent.
  // Needed to compensate for LayoutInline Legacy code offsets.
  void AddSelfOutlineRects(const PhysicalOffset& additional_offset,
                           NGOutlineType include_block_overflows,
                           Vector<PhysicalRect>* outline_rects) const;
  // Same as |AddSelfOutlineRects|, except when |this.IsInlineBox()|, in which
  // case the coordinate system is relative to the inline formatting context.
  void AddOutlineRects(const PhysicalOffset& additional_offset,
                       NGOutlineType include_block_overflows,
                       Vector<PhysicalRect>* outline_rects) const;

  PositionWithAffinity PositionForPoint(PhysicalOffset) const;

  UBiDiLevel BidiLevel() const;

  PhysicalBoxSides SidesToInclude() const {
    return PhysicalBoxSides(include_border_top_, include_border_right_,
                            include_border_bottom_, include_border_left_);
  }
  NGPixelSnappedPhysicalBoxStrut BorderWidths() const;

  // Return true if this is the first fragment generated from a node.
  bool IsFirstForNode() const { return is_first_for_node_; }

  bool HasDescendantsForTablePart() const {
    DCHECK(IsTableNGPart() || IsTableNGCell());
    return has_descendants_for_table_part_;
  }

  // Returns true if we have a descendant within this formatting context, which
  // is potentially above our block-start edge.
  bool MayHaveDescendantAboveBlockStart() const {
    return may_have_descendant_above_block_start_;
  }

#if DCHECK_IS_ON()
  void CheckSameForSimplifiedLayout(const NGPhysicalBoxFragment&,
                                    bool check_same_block_size) const;
#endif

  bool HasExtraMathMLPainting() const {
    if (IsMathMLFraction())
      return true;

    if (has_rare_data_ && ComputeRareDataAddress()->mathml_paint_info)
      return true;

    return false;
  }
  const NGMathMLPaintInfo& GetMathMLPaintInfo() const {
    return *ComputeRareDataAddress()->mathml_paint_info;
  }

  // Painters can use const methods only, except for these explicitly declared
  // methods.
  class MutableForPainting {
    STACK_ALLOCATED();

   public:
    void RecalcInkOverflow() { fragment_.RecalcInkOverflow(); }
    void RecalcInkOverflow(const PhysicalRect& contents) {
      fragment_.RecalcInkOverflow(contents);
    }

   private:
    friend class NGPhysicalBoxFragment;
    explicit MutableForPainting(const NGPhysicalBoxFragment& fragment)
        : fragment_(const_cast<NGPhysicalBoxFragment&>(fragment)) {}

    NGPhysicalBoxFragment& fragment_;
  };
  MutableForPainting GetMutableForPainting() const {
    return MutableForPainting(*this);
  }

  // Returns if this fragment can compute ink overflow.
  bool CanUseFragmentsForInkOverflow() const;
  // Recalculates and updates |*InkOverflow|.
  void RecalcInkOverflow();
  // |RecalcInkOverflow| using the given contents ink overflow rect.
  void RecalcInkOverflow(const PhysicalRect& contents);

 private:
  static size_t ByteSize(wtf_size_t num_fragment_items,
                         wtf_size_t num_children,
                         bool has_layout_overflow,
                         bool has_borders,
                         bool has_padding,
                         bool has_inflow_bounds,
                         bool has_rare_data);

  struct RareData {
    RareData(const RareData&);
    RareData(NGBoxFragmentBuilder*, PhysicalSize size);

    Vector<NGPhysicalOutOfFlowPositionedNode>
        oof_positioned_fragmentainer_descendants;
    MulticolCollection multicols_with_pending_oofs;
    const std::unique_ptr<const NGMathMLPaintInfo> mathml_paint_info;

    // TablesNG rare data.
    PhysicalRect table_grid_rect;
    NGTableFragmentData::ColumnGeometries table_column_geometries;
    scoped_refptr<const NGTableBorders> table_collapsed_borders;
    std::unique_ptr<NGTableFragmentData::CollapsedBordersGeometry>
        table_collapsed_borders_geometry;
    wtf_size_t table_cell_column_index;
  };

  const NGFragmentItems* ComputeItemsAddress() const {
    DCHECK(has_fragment_items_ || has_layout_overflow_ || has_borders_ ||
           has_padding_ || has_inflow_bounds_ || has_rare_data_);
    const NGLink* children_end = children_ + Children().size();
    return reinterpret_cast<const NGFragmentItems*>(children_end);
  }

  const PhysicalRect* ComputeLayoutOverflowAddress() const {
    DCHECK(has_layout_overflow_ || has_borders_ || has_padding_ ||
           has_inflow_bounds_ || has_rare_data_);
    const NGFragmentItems* items = ComputeItemsAddress();
    if (!has_fragment_items_)
      return reinterpret_cast<const PhysicalRect*>(items);
    return reinterpret_cast<const PhysicalRect*>(
        reinterpret_cast<const uint8_t*>(items) + items->ByteSize());
  }

  const NGPhysicalBoxStrut* ComputeBordersAddress() const {
    DCHECK(has_borders_ || has_padding_ || has_inflow_bounds_ ||
           has_rare_data_);
    const PhysicalRect* address = ComputeLayoutOverflowAddress();
    return has_layout_overflow_
               ? reinterpret_cast<const NGPhysicalBoxStrut*>(address + 1)
               : reinterpret_cast<const NGPhysicalBoxStrut*>(address);
  }

  const NGPhysicalBoxStrut* ComputePaddingAddress() const {
    DCHECK(has_padding_ || has_inflow_bounds_ || has_rare_data_);
    const NGPhysicalBoxStrut* address = ComputeBordersAddress();
    return has_borders_ ? address + 1 : address;
  }

  const PhysicalRect* ComputeInflowBoundsAddress() const {
    DCHECK(has_inflow_bounds_ || has_rare_data_);
    NGPhysicalBoxStrut* address =
        const_cast<NGPhysicalBoxStrut*>(ComputePaddingAddress());
    return has_padding_ ? reinterpret_cast<PhysicalRect*>(address + 1)
                        : reinterpret_cast<PhysicalRect*>(address);
  }

  const RareData* ComputeRareDataAddress() const {
    DCHECK(has_rare_data_);
    PhysicalRect* address =
        const_cast<PhysicalRect*>(ComputeInflowBoundsAddress());
    return has_inflow_bounds_ ? reinterpret_cast<RareData*>(address + 1)
                              : reinterpret_cast<RareData*>(address);
  }

  void SetInkOverflow(const PhysicalRect& self, const PhysicalRect& contents);
  PhysicalRect RecalcContentsInkOverflow();
  PhysicalRect ComputeSelfInkOverflow() const;

  void AddOutlineRects(const PhysicalOffset& additional_offset,
                       NGOutlineType include_block_overflows,
                       bool inline_container_relative,
                       Vector<PhysicalRect>* outline_rects) const;
  void AddOutlineRectsForInlineBox(PhysicalOffset additional_offset,
                                   NGOutlineType include_block_overflows,
                                   bool inline_container_relative,
                                   Vector<PhysicalRect>* outline_rects) const;

  PositionWithAffinity PositionForPointByClosestChild(
      PhysicalOffset point_in_contents) const;

  PositionWithAffinity PositionForPointInBlockFlowDirection(
      PhysicalOffset point_in_contents) const;

  PositionWithAffinity PositionForPointInTable(
      PhysicalOffset point_in_contents) const;

  PositionWithAffinity PositionForPointRespectingEditingBoundaries(
      const NGPhysicalBoxFragment& child,
      PhysicalOffset point_in_child) const;

#if DCHECK_IS_ON()
  void CheckIntegrity() const;
#endif

  LayoutUnit baseline_;
  LayoutUnit last_baseline_;
  NGInkOverflow ink_overflow_;
  NGLink children_[];
  // fragment_items, borders, padding, and rare_data are after |children_| if
  // they are not empty/initial.
};

template <>
struct DowncastTraits<NGPhysicalBoxFragment> {
  static bool AllowFrom(const NGPhysicalFragment& fragment) {
    return fragment.Type() == NGPhysicalFragment::kFragmentBox;
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_PHYSICAL_BOX_FRAGMENT_H_
