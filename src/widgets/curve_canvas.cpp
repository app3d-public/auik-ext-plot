#include <auik/auik.hpp>
#include <auik/ext/plot/curve_canvas.hpp>

namespace auik::plot
{
    CurveCanvas::CurveCanvas(u32 id, Grid *grid, Curve *curve, CurveCanvasFlags canvas_flags,
                             const amal::vec2 &inline_size, WidgetFlags widget_flags)
        : Block(id, widget_flags, AUIK_TAG_PLOT_CURVE_CANVAS),
          _grid(grid),
          _curve(curve),
          _rubber_band(acul::alloc<RubberBand>(id, WidgetFlagBits::visible)),
          _canvas_flags(canvas_flags)
    {
        set_size(inline_size);
        add_event_flags(EventFlagBits::click | EventFlagBits::drag);
        if (_grid) add_child_to_background(_grid);
        if (_curve)
        {
            if (_grid) _grid->set_view(_curve->view());
            _curve->set_bounds_hit_enabled(false);
            add_child(_curve);
        }
        add_child_to_foreground(_rubber_band);
        reset_clamp_to_view();
    }

    ControlCurve *CurveCanvas::control_curve() const
    {
        return _curve && _curve->signature() == AUIK_TAG_PLOT_CONTROL_CURVE ? static_cast<ControlCurve *>(_curve)
                                                                            : nullptr;
    }

    void CurveCanvas::set_rubber_band_enabled(bool enabled)
    {
        if (enabled == rubber_band_enabled()) return;
        if (enabled) _canvas_flags |= CurveCanvasFlagBits::rubber_band;
        else
        {
            _canvas_flags &= ~CurveCanvasFlagBits::rubber_band;
            if (_rubber_band->active()) _rubber_band->dispatch_drag({}, KeyPressState::release);
            _rubber_band->set_visible();
            _rubber_band->clear_commit();
        }
    }

    void CurveCanvas::set_clamp(const amal::vec2 &minimum, const amal::vec2 &maximum)
    {
        if (auto *controls = control_curve()) controls->set_clamp(minimum, maximum);
    }

    void CurveCanvas::clear_clamp()
    {
        if (auto *controls = control_curve()) controls->clear_clamp();
    }

    void CurveCanvas::reset_clamp_to_view()
    {
        if (!_curve) return;
        set_clamp(_curve->view().offset, _curve->view().offset + _curve->view().size);
    }

    void CurveCanvas::rebuild_clip_rects()
    {
        Block::rebuild_clip_rects();
        invalidate_hit_rect(_hit_draw);
    }

    void CurveCanvas::reset_draw_records()
    {
        Block::reset_draw_records();
        _hit_draw = {};
    }

    void CurveCanvas::draw(DrawCtx &ctx)
    {
        QuadsInstanceData hidden{};
        emit_quads_instance(ctx, get_primary_quads_stream(), _hit_draw, hidden, get_rect(), false, can_emit_hit(ctx));
        Block::draw(ctx);
    }

    void CurveCanvas::on_click(MouseKey key, KeyPressState state, u32 click_count)
    {
        (void)click_count;
        if (key != MouseKey::left || state != KeyPressState::press) return;
        const ElementID target = detail::get_context().hover_id;
        if (target.widget_id != id() || target.tag_id != AUIK_TAG_PLOT_CURVE_CANVAS) return;
        _selection_mods = active_key_mods();
        if ((_selection_mods & KeyModeBits::shift) && control_curve() &&
            control_curve()->insert_point_at(get_mouse_pos(), true))
            return;
        if (auto *controls = control_curve();
            controls && !(_selection_mods & (KeyModeBits::control | KeyModeBits::alt)))
            controls->clear_selection();
    }

    void CurveCanvas::on_drag(const amal::vec2 &delta, KeyPressState state)
    {
        const ElementID drag_id = detail::get_context().io.drag_id;
        if (!rubber_band_enabled() || drag_id.widget_id != id() || drag_id.tag_id != AUIK_TAG_PLOT_CURVE_CANVAS) return;
        _rubber_band->dispatch_drag(delta, state);
        if (state == KeyPressState::release)
        {
            // RubberBand hides itself after committing. CurveCanvas keeps the inactive overlay in its
            // foreground layer so it retains a valid layout and depth for the next drag.
            _rubber_band->set_visible();
            commit_rubber_band();
        }
    }

    void CurveCanvas::commit_rubber_band()
    {
        if (!_rubber_band->committed()) return;
        auto *controls = control_curve();
        if (!controls)
        {
            _rubber_band->clear_commit();
            return;
        }

        const amal::rect selection = _rubber_band->selection_rect();
        acul::vector<size_t> matches;
        matches.reserve(controls->curve().points.size());
        for (size_t point_index = 0u; point_index < controls->curve().points.size(); ++point_index)
        {
            const amal::vec2 point = controls->map_to_screen(controls->curve().points[point_index].position);
            if (point.x >= amal::get_rect_left(selection) && point.x <= amal::get_rect_right(selection) &&
                point.y >= amal::get_rect_top(selection) && point.y <= amal::get_rect_bottom(selection))
                matches.push_back(point_index);
        }

        if (_selection_mods & KeyModeBits::alt)
            for (size_t point_index : matches) controls->deselect_point(point_index);
        else if (_selection_mods & KeyModeBits::control)
            for (size_t point_index : matches) controls->select_point(point_index, true);
        else controls->set_selected_points(matches);
        _rubber_band->clear_commit();
    }
} // namespace auik::plot
