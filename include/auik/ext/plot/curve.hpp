#pragma once

#include <acul/vector.hpp>
#include <amal/bezier.hpp>
#include <auik-ext-plot/symbol_export.h>
#include <auik/ext/plot/widget_tags.hpp>
#include <auik/model.hpp>
#include <auik/pipelines.hpp>
#include <auik/widgets/widget.hpp>

#define AUIK_TAG_PLOT_CURVE         0x1BD91815u
#define AUIK_TAG_PLOT_CONTROL_CURVE 0x0F24728Eu
#define AUIK_TAG_PLOT_CURVE_POINT   0x258A7FD4u
#define AUIK_TAG_PLOT_HANDLE_IN     0x9F1C839Eu
#define AUIK_TAG_PLOT_HANDLE_OUT    0xFC4EF7C8u

namespace auik::plot
{
    class Curve : public Widget
    {
    public:
        AUIK_EXT_PLOT_EXPORT Curve(u32 id, const amal::bezier_curve2 &curve = {},
                                   const amal::vec2 &inline_size = AUIK_SIZE_INHERIT,
                                   WidgetFlags widget_flags = get_default_widget_flags(),
                                   u32 style_tag = AUIK_STYLE_TAG_PLOT_CURVE,
                                   u32 line_style_tag = AUIK_STYLE_TAG_PLOT_CURVE_LINE);
        AUIK_EXT_PLOT_EXPORT ~Curve() override;

        const amal::bezier_curve2 &curve() const { return _curve; }
        amal::bezier_curve2 &curve() { return _curve; }
        const amal::rect &view() const { return _view; }
        f32 flatten_tolerance() const { return _flatten_tolerance; }
        u32 style_tag() const { return _style.tag_id; }
        u32 line_style_tag() const { return _line_style.tag_id; }
        ModelBinding *model_binding() const { return _model_binding; }
        bool bounds_hit_enabled() const { return _bounds_hit_enabled; }
        AUIK_EXT_PLOT_EXPORT amal::vec2 map_to_screen(const amal::vec2 &point) const;
        AUIK_EXT_PLOT_EXPORT amal::vec2 map_to_curve(const amal::vec2 &point) const;

        AUIK_EXT_PLOT_EXPORT void set_curve(const amal::bezier_curve2 &curve);
        AUIK_EXT_PLOT_EXPORT void invalidate_curve();
        AUIK_EXT_PLOT_EXPORT void set_view(const amal::rect &view);
        AUIK_EXT_PLOT_EXPORT void set_flatten_tolerance(f32 tolerance);
        AUIK_EXT_PLOT_EXPORT void set_style_tags(u32 style_tag, u32 line_style_tag);
        AUIK_EXT_PLOT_EXPORT void set_model_binding(ModelBinding *binding);
        void set_bounds_hit_enabled(bool enabled) { _bounds_hit_enabled = enabled; }

        AUIK_EXT_PLOT_EXPORT StyleUpdateFlags update_style() override;
        AUIK_EXT_PLOT_EXPORT void update_layout_min_size_force() override;
        AUIK_EXT_PLOT_EXPORT void update_layout(bool min_size_known) override;
        AUIK_EXT_PLOT_EXPORT void translate(const amal::vec2 &delta) override;
        AUIK_EXT_PLOT_EXPORT void rebuild_clip_rects() override;
        AUIK_EXT_PLOT_EXPORT void reset_draw_records() override;
        u32 get_depth_requirement() const override { return 1u; }
        AUIK_EXT_PLOT_EXPORT void update_depth(const amal::vec2 &depth_range) override;
        AUIK_EXT_PLOT_EXPORT void draw(DrawCtx &ctx) override;
        u32 signature() const override { return AUIK_TAG_PLOT_CURVE; }

    protected:
        AUIK_EXT_PLOT_EXPORT amal::vec2 curve_to_screen(const amal::vec2 &point) const;
        AUIK_EXT_PLOT_EXPORT amal::vec2 screen_to_curve(const amal::vec2 &point) const;
        AUIK_EXT_PLOT_EXPORT void invalidate_geometry();
        AUIK_EXT_PLOT_EXPORT virtual void rebuild_geometry();
        AUIK_EXT_PLOT_EXPORT virtual void draw_controls(DrawCtx &ctx);
        virtual amal::vec2 display_curve_point(const amal::vec2 &point) const { return point; }
        virtual amal::vec2 inset_curve_screen_point(const amal::vec2 &point, f32 inset) const
        {
            (void)inset;
            return point;
        }
        AUIK_EXT_PLOT_EXPORT void sync_model_binding();

        amal::bezier_curve2 _curve;
        amal::rect _view{{0.0f, 0.0f}, {1.0f, 1.0f}};
        StyleSelector _style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CURVE};
        StyleSelector _line_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CURVE_LINE};
        DrawDataID _curve_draw{};
        acul::vector<VertexStreamVertex> _curve_vertices;
        acul::vector<VertexStreamIndex> _curve_indices;
        acul::vector<amal::vec2> _flattened_points;
        acul::vector<amal::vec2> _screen_points;
        amal::bezier_flatten_workspace2 _flatten_workspace;
        VertexStreamBatchData _curve_batch{};
        amal::vec2 _curve_depth_range{0.0f, 1.0f};
        u32 _curve_vertex_bucket = 0u;
        u32 _curve_index_bucket = 0u;
        f32 _flatten_tolerance = 0.5f;
        ModelBinding *_model_binding = nullptr;
        bool _geometry_dirty = true;
        bool _curve_offset_dirty = false;
        bool _bounds_hit_enabled = true;
        bool _syncing_model = false;
    };

    class ControlCurve final : public Curve
    {
    public:
        AUIK_EXT_PLOT_EXPORT
        ControlCurve(u32 id, const amal::bezier_curve2 &curve = {}, const amal::vec2 &inline_size = AUIK_SIZE_INHERIT,
                     WidgetFlags widget_flags = get_default_widget_flags() | WidgetFlagBits::hittable,
                     u32 style_tag = AUIK_STYLE_TAG_PLOT_CURVE, u32 line_style_tag = AUIK_STYLE_TAG_PLOT_CURVE_LINE,
                     u32 control_line_style_tag = AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_LINE,
                     u32 point_style_tag = AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_POINT,
                     u32 handle_style_tag = AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_HANDLE);

        AUIK_EXT_PLOT_EXPORT void set_control_style_tags(u32 line_style_tag, u32 point_style_tag, u32 handle_style_tag);
        const acul::vector<size_t> &selected_points() const { return _selected_points; }
        AUIK_EXT_PLOT_EXPORT bool is_point_selected(size_t point_index) const;
        AUIK_EXT_PLOT_EXPORT void select_point(size_t point_index, bool additive = false);
        AUIK_EXT_PLOT_EXPORT void deselect_point(size_t point_index);
        AUIK_EXT_PLOT_EXPORT void set_selected_points(const acul::vector<size_t> &point_indices);
        AUIK_EXT_PLOT_EXPORT void clear_selection();
        bool has_clamp() const { return _clamp_enabled; }
        const amal::vec2 &clamp_min() const { return _clamp_min; }
        const amal::vec2 &clamp_max() const { return _clamp_max; }
        AUIK_EXT_PLOT_EXPORT void set_clamp(const amal::vec2 &minimum, const amal::vec2 &maximum);
        AUIK_EXT_PLOT_EXPORT void clear_clamp();
        AUIK_EXT_PLOT_EXPORT bool insert_point_at(const amal::vec2 &screen_position, bool automatic = true);
        AUIK_EXT_PLOT_EXPORT StyleUpdateFlags update_style() override;
        AUIK_EXT_PLOT_EXPORT void reset_draw_records() override;
        u32 get_depth_requirement() const override { return 3u; }
        AUIK_EXT_PLOT_EXPORT void update_depth(const amal::vec2 &depth_range) override;
        AUIK_EXT_PLOT_EXPORT void translate(const amal::vec2 &delta) override;
        AUIK_EXT_PLOT_EXPORT void on_hover(HoverState state) override;
        AUIK_EXT_PLOT_EXPORT void on_click(MouseKey key, KeyPressState state, u32 click_count) override;
        AUIK_EXT_PLOT_EXPORT void on_drag(const amal::vec2 &delta, KeyPressState state) override;
        u32 signature() const override { return AUIK_TAG_PLOT_CONTROL_CURVE; }

    protected:
        AUIK_EXT_PLOT_EXPORT void rebuild_geometry() override;
        AUIK_EXT_PLOT_EXPORT void draw_controls(DrawCtx &ctx) override;
        AUIK_EXT_PLOT_EXPORT amal::vec2 display_curve_point(const amal::vec2 &point) const override;
        AUIK_EXT_PLOT_EXPORT amal::vec2 inset_curve_screen_point(const amal::vec2 &point, f32 inset) const override;

    private:
        enum class ControlKind : u8
        {
            point,
            handle_in,
            handle_out
        };

        StyleSelector _control_line_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_LINE};
        StyleSelector _point_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_POINT};
        StyleSelector _point_hover_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_POINT};
        StyleSelector _point_active_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_POINT};
        StyleSelector _handle_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_HANDLE};
        StyleSelector _handle_hover_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_HANDLE};
        StyleSelector _handle_active_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_CONTROL_CURVE_HANDLE};
        acul::vector<QuadsInstanceData> _controls;
        acul::vector<DrawDataID> _control_draws;
        acul::vector<detail::RectData> _control_rects;
        acul::vector<DrawDataID> _control_hits;
        acul::vector<size_t> _selected_points;
        amal::vec2 _control_lines_depth_range{0.0f, 1.0f};
        amal::vec2 _control_points_depth_range{0.0f, 1.0f};
        amal::vec2 _clamp_min{0.0f, 0.0f};
        amal::vec2 _clamp_max{1.0f, 1.0f};
        bool _drag_changed = false;
        bool _clamp_enabled = false;

        void invalidate_controls(bool notify_change = false);
        void sync_control_hits(DrawCtx &ctx);
    };

    inline Curve *make_curve(u32 id, const amal::bezier_curve2 &curve = {},
                             const amal::vec2 &inline_size = AUIK_SIZE_INHERIT)
    {
        return acul::alloc<Curve>(id, std::move(curve), inline_size);
    }

    inline ControlCurve *make_control_curve(u32 id, const amal::bezier_curve2 &curve = {},
                                            const amal::vec2 &inline_size = AUIK_SIZE_INHERIT)
    {
        return acul::alloc<ControlCurve>(id, std::move(curve), inline_size);
    }
} // namespace auik::plot
