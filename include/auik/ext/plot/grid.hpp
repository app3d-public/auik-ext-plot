#pragma once

#include <acul/vector.hpp>
#include <amal/geometric.hpp>
#include <auik-ext-plot/symbol_export.h>
#include <auik/ext/plot/widget_tags.hpp>
#include <auik/model.hpp>
#include <auik/pipelines.hpp>
#include <auik/widgets/widget.hpp>

#define AUIK_TAG_PLOT_GRID 0x162F2C23u

namespace auik::plot
{
    struct GridBaseline
    {
        f32 coordinate = 0.0f;
        amal::axis axis = amal::axis::x;
        u32 style_tag = AUIK_STYLE_TAG_PLOT_GRID_BASELINE;
    };

    struct GridState
    {
        amal::rect view{{0.0f, 0.0f}, {1.0f, 1.0f}};
        u32 divisions = 10u;
        amal::axis division_axis = amal::axis::x;
        amal::vec2 unit_size{1.0f, 1.0f};
        amal::vec2 scale{1.0f, 1.0f};
        amal::vec2 offset{0.0f, 0.0f};
        f32 min_scale = 0.05f;
        f32 max_scale = 100.0f;
        bool custom_unit_size = false;
    };

    class Grid final : public Widget
    {
    public:
        AUIK_EXT_PLOT_EXPORT Grid(u32 id, const amal::vec2 &inline_size = AUIK_SIZE_INHERIT,
                                  WidgetFlags widget_flags = get_default_widget_flags(),
                                  u32 style_tag = AUIK_STYLE_TAG_PLOT_GRID,
                                  u32 line_style_tag = AUIK_STYLE_TAG_PLOT_GRID_LINE);
        AUIK_EXT_PLOT_EXPORT ~Grid() override;

        const amal::rect &view() const { return _view; }
        u32 divisions() const { return _divisions; }
        amal::axis division_axis() const { return _division_axis; }
        const amal::vec2 &scale() const { return _scale; }
        const amal::vec2 &offset() const { return _offset; }
        AUIK_EXT_PLOT_EXPORT amal::vec2 unit_size() const;
        AUIK_EXT_PLOT_EXPORT amal::vec2 scaled_unit_size() const;
        f32 min_scale() const { return _min_scale; }
        f32 max_scale() const { return _max_scale; }
        ModelBinding *model_binding() const { return _model_binding; }
        u32 style_tag() const { return _style.tag_id; }
        u32 line_style_tag() const { return _line_style.tag_id; }
        AUIK_EXT_PLOT_EXPORT const acul::vector<f32> &label_values(amal::axis axis) const;
        AUIK_EXT_PLOT_EXPORT const acul::vector<GridBaseline> &baselines() const;
        AUIK_EXT_PLOT_EXPORT GridState state() const;

        AUIK_EXT_PLOT_EXPORT void set_view(const amal::rect &view);
        AUIK_EXT_PLOT_EXPORT void set_divisions(u32 count, amal::axis axis);
        AUIK_EXT_PLOT_EXPORT void set_unit_size(const amal::vec2 &unit_size);
        AUIK_EXT_PLOT_EXPORT void set_scale(f32 scale);
        AUIK_EXT_PLOT_EXPORT void set_scale(const amal::vec2 &scale);
        AUIK_EXT_PLOT_EXPORT void set_scale_limits(f32 min_scale, f32 max_scale);
        AUIK_EXT_PLOT_EXPORT void set_offset(const amal::vec2 &offset);
        AUIK_EXT_PLOT_EXPORT void set_transform(const amal::vec2 &scale, const amal::vec2 &offset);
        AUIK_EXT_PLOT_EXPORT void set_state(const GridState &state);
        AUIK_EXT_PLOT_EXPORT void set_model_binding(ModelBinding *binding);
        AUIK_EXT_PLOT_EXPORT void set_style_tags(u32 style_tag, u32 line_style_tag);
        AUIK_EXT_PLOT_EXPORT void set_label_values(amal::axis axis, const acul::vector<f32> &values, u32 precision = 1u,
                                                   u32 style_tag = AUIK_STYLE_TAG_PLOT_GRID_LABEL);
        AUIK_EXT_PLOT_EXPORT void clear_label_values(amal::axis axis);
        AUIK_EXT_PLOT_EXPORT void set_baselines(const acul::vector<GridBaseline> &baselines);
        AUIK_EXT_PLOT_EXPORT void clear_baselines();

        AUIK_EXT_PLOT_EXPORT StyleUpdateFlags update_style() override;
        AUIK_EXT_PLOT_EXPORT void update_layout_min_size_force() override;
        AUIK_EXT_PLOT_EXPORT void update_layout(bool min_size_known) override;
        AUIK_EXT_PLOT_EXPORT void translate(const amal::vec2 &delta) override;
        AUIK_EXT_PLOT_EXPORT void rebuild_clip_rects() override;
        AUIK_EXT_PLOT_EXPORT void reset_draw_records() override;
        u32 get_depth_requirement() const override { return 6u; }
        AUIK_EXT_PLOT_EXPORT void update_depth(const amal::vec2 &depth_range) override;
        AUIK_EXT_PLOT_EXPORT void draw(DrawCtx &ctx) override;
        u32 signature() const noexcept override { return AUIK_TAG_PLOT_GRID; }

    private:
        void invalidate_geometry();
        void rebuild_geometry();
        void sync_model_binding();
        struct Hints;

        StyleSelector _style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_GRID};
        StyleSelector _line_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_GRID_LINE};
        DrawDataID _background_draw{};
        DrawDataID _rounded_mask_draw{};
        acul::vector<QuadsInstanceData> _lines;
        acul::vector<DrawDataID> _line_draws;
        amal::vec2 _background_depth_range{0.0f, 1.0f};
        amal::vec2 _lines_depth_range{0.0f, 1.0f};
        amal::vec2 _baselines_depth_range{0.0f, 1.0f};
        amal::vec2 _rounded_mask_depth_range{0.0f, 1.0f};
        amal::vec2 _label_backgrounds_depth_range{0.0f, 1.0f};
        amal::vec2 _labels_depth_range{0.0f, 1.0f};
        amal::rect _view{{0.0f, 0.0f}, {1.0f, 1.0f}};
        u32 _divisions = 10u;
        amal::axis _division_axis = amal::axis::x;
        amal::vec2 _scale{1.0f, 1.0f};
        amal::vec2 _offset{0.0f, 0.0f};
        f32 _min_scale = 0.05f;
        f32 _max_scale = 100.0f;
        Hints *_hints = nullptr;
        ModelBinding *_model_binding = nullptr;
        bool _geometry_dirty = true;
        bool _syncing_model = false;
    };

    inline Grid *make_grid(u32 id, const amal::vec2 &inline_size = AUIK_SIZE_INHERIT, u32 divisions = 10u,
                           amal::axis division_axis = amal::axis::x)
    {
        auto *grid = acul::alloc<Grid>(id, inline_size);
        grid->set_divisions(divisions, division_axis);
        return grid;
    }
} // namespace auik::plot
