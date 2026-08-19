#pragma once

#include <auik-ext-plot/symbol_export.h>
#include <auik/ext/plot/curve.hpp>
#include <auik/ext/plot/grid.hpp>
#include <auik/widgets/containers.hpp>
#include <auik/widgets/rubber_band.hpp>

#define AUIK_TAG_PLOT_CURVE_CANVAS 0xFF39D5A4u

namespace auik::plot
{
    struct CurveCanvasFlagBits
    {
        enum enum_type
        {
            none = 0x0,
            rubber_band = 0x1
        };
        using flag_bitmask = std::true_type;
    };

    using CurveCanvasFlags = acul::flags<CurveCanvasFlagBits>;

    class CurveCanvas final : public Block
    {
    public:
        AUIK_EXT_PLOT_EXPORT
        CurveCanvas(u32 id, Grid *grid, Curve *curve, CurveCanvasFlags canvas_flags = CurveCanvasFlagBits::rubber_band,
                    const amal::vec2 &inline_size = AUIK_SIZE_FILL,
                    WidgetFlags widget_flags = get_default_widget_flags() | WidgetFlagBits::hittable);

        Grid *grid() const { return _grid; }
        Curve *curve() const { return _curve; }
        AUIK_EXT_PLOT_EXPORT ControlCurve *control_curve() const;
        RubberBand *rubber_band() const { return _rubber_band; }
        CurveCanvasFlags canvas_flags() const { return _canvas_flags; }
        bool rubber_band_enabled() const { return _canvas_flags & CurveCanvasFlagBits::rubber_band; }
        AUIK_EXT_PLOT_EXPORT void set_rubber_band_enabled(bool enabled);
        AUIK_EXT_PLOT_EXPORT void set_clamp(const amal::vec2 &minimum, const amal::vec2 &maximum);
        AUIK_EXT_PLOT_EXPORT void clear_clamp();
        AUIK_EXT_PLOT_EXPORT void reset_clamp_to_view();

        AUIK_EXT_PLOT_EXPORT void rebuild_clip_rects() override;
        AUIK_EXT_PLOT_EXPORT void reset_draw_records() override;
        AUIK_EXT_PLOT_EXPORT void draw(DrawCtx &ctx) override;
        AUIK_EXT_PLOT_EXPORT void on_click(MouseKey key, KeyPressState state, u32 click_count) override;
        AUIK_EXT_PLOT_EXPORT void on_drag(const amal::vec2 &delta, KeyPressState state) override;
        u32 signature() const override { return AUIK_TAG_PLOT_CURVE_CANVAS; }

    private:
        void commit_rubber_band();

        Grid *_grid = nullptr;
        Curve *_curve = nullptr;
        RubberBand *_rubber_band = nullptr;
        DrawDataID _hit_draw{};
        CurveCanvasFlags _canvas_flags = CurveCanvasFlagBits::none;
        KeyMode _selection_mods{};
    };

    inline CurveCanvas *make_curve_canvas(u32 id, Grid *grid, Curve *curve,
                                          CurveCanvasFlags canvas_flags = CurveCanvasFlagBits::rubber_band,
                                          const amal::vec2 &inline_size = AUIK_SIZE_FILL)
    {
        return acul::alloc<CurveCanvas>(id, grid, curve, canvas_flags, inline_size);
    }
} // namespace auik::plot
