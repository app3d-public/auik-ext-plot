#include <algorithm>
#include <auik/auik.hpp>
#include <auik/ext/plot/curve.hpp>
#include <limits>
#include "amal/bezier.hpp"

namespace auik::plot
{
    namespace
    {
        template <class Container>
        struct AppendIterator
        {
            Container *container = nullptr;

            AppendIterator &operator*() { return *this; }
            AppendIterator &operator++() { return *this; }
            AppendIterator operator++(int) { return *this; }
            AppendIterator &operator=(const typename Container::value_type &value)
            {
                container->push_back(value);
                return *this;
            }
        };

        amal::vec2 safe_normalize(const amal::vec2 &value)
        {
            const f32 length = amal::length(value);
            return length > 1e-5f ? value / length : amal::vec2{};
        }

        amal::vec2 perpendicular(const amal::vec2 &value) { return {-value.y, value.x}; }

        amal::vec2 polyline_join_offset(const acul::vector<amal::vec2> &points, size_t index, f32 distance)
        {
            if (index == 0u) return perpendicular(safe_normalize(points[1] - points[0])) * distance;
            if (index + 1u == points.size())
                return perpendicular(safe_normalize(points[index] - points[index - 1u])) * distance;
            const amal::vec2 previous = safe_normalize(points[index] - points[index - 1u]);
            const amal::vec2 next = safe_normalize(points[index + 1u] - points[index]);
            const amal::vec2 next_normal = perpendicular(next);
            const amal::vec2 miter = safe_normalize(perpendicular(previous + next));
            const f32 denominator = amal::dot(miter, next_normal);
            const f32 scale = amal::abs(denominator) > 1e-3f
                                  ? amal::clamp(distance / denominator, -distance * 4.0f, distance * 4.0f)
                                  : distance;
            return miter * scale;
        }

        void append_aa_strip_indices(acul::vector<VertexStreamIndex> &indices, u32 first, u32 next)
        {
            for (u32 band = 0u; band < 3u; ++band)
            {
                indices.push_back(first + band);
                indices.push_back(first + band + 1u);
                indices.push_back(next + band);
                indices.push_back(first + band + 1u);
                indices.push_back(next + band + 1u);
                indices.push_back(next + band);
            }
        }

        void append_vertex(acul::vector<VertexStreamVertex> &vertices, const amal::vec2 &position, f32 z_order,
                           u32 color, u16 clip_id)
        {
            VertexStreamVertex vertex{};
            vertex.position = position;
            vertex.z_order = z_order;
            vertex.color = color;
            vertex.clip_id = clip_id;
            vertices.push_back(vertex);
        }

        u32 grow_geometry_bucket(u32 current, u32 required, u32 minimum)
        {
            if (required <= current) return current;
            const u32 headroom = required / 2u;
            const u32 target = required <= std::numeric_limits<u32>::max() - headroom ? required + headroom : required;
            u32 bucket = amal::max(current, minimum);
            while (bucket < target && bucket <= std::numeric_limits<u32>::max() / 2u) bucket *= 2u;
            return amal::max(bucket, required);
        }

        void build_polyline(acul::vector<VertexStreamVertex> &vertices, acul::vector<VertexStreamIndex> &indices,
                            const acul::vector<amal::vec2> &points, f32 thickness, f32 z_order, u32 color, u16 clip_id)
        {
            vertices.clear();
            indices.clear();
            if (points.size() < 2u || thickness <= 0.0f) return;

            const f32 half_width = thickness * 0.5f;
            constexpr f32 fringe = 1.0f;
            const u32 transparent_color = color & 0x00FFFFFFu;
            vertices.reserve(points.size() * 4u);
            indices.reserve((points.size() - 1u) * 18u);
            for (size_t i = 0u; i < points.size(); ++i)
            {
                const amal::vec2 inner = polyline_join_offset(points, i, half_width);
                const amal::vec2 outer = polyline_join_offset(points, i, half_width + fringe);
                append_vertex(vertices, points[i] + outer, z_order, transparent_color, clip_id);
                append_vertex(vertices, points[i] + inner, z_order, color, clip_id);
                append_vertex(vertices, points[i] - inner, z_order, color, clip_id);
                append_vertex(vertices, points[i] - outer, z_order, transparent_color, clip_id);
            }

            for (u32 i = 0u; i + 1u < static_cast<u32>(points.size()); ++i)
                append_aa_strip_indices(indices, i * 4u, (i + 1u) * 4u);
        }

        void append_segment(acul::vector<VertexStreamVertex> &vertices, acul::vector<VertexStreamIndex> &indices,
                            const amal::vec2 &from, const amal::vec2 &to, f32 thickness, f32 z_order, u32 color,
                            u16 clip_id)
        {
            const amal::vec2 delta = to - from;
            const f32 segment_length = amal::length(delta);
            if (segment_length <= 1e-5f) return;
            const amal::vec2 direction = delta / segment_length;
            const f32 half_width = thickness * 0.5f;
            const amal::vec2 inner = perpendicular(direction) * half_width;
            const amal::vec2 outer = perpendicular(direction) * (half_width + 1.0f);
            const u32 transparent_color = color & 0x00FFFFFFu;
            const u32 first = static_cast<u32>(vertices.size());
            append_vertex(vertices, from + outer, z_order, transparent_color, clip_id);
            append_vertex(vertices, from + inner, z_order, color, clip_id);
            append_vertex(vertices, from - inner, z_order, color, clip_id);
            append_vertex(vertices, from - outer, z_order, transparent_color, clip_id);
            append_vertex(vertices, to + outer, z_order, transparent_color, clip_id);
            append_vertex(vertices, to + inner, z_order, color, clip_id);
            append_vertex(vertices, to - inner, z_order, color, clip_id);
            append_vertex(vertices, to - outer, z_order, transparent_color, clip_id);
            append_aa_strip_indices(indices, first, first + 4u);
        }

        bool append_control(acul::vector<QuadsInstanceData> &controls, const Style &style, const amal::vec2 &center,
                            f32 z_order, u16 clip_id)
        {
            const amal::vec2 size{amal::max(style.width(), 0.0f), amal::max(style.height(), 0.0f)};
            if (size.x <= 0.0f || size.y <= 0.0f) return false;
            QuadsInstanceData control{};
            control.rect = {center - size * 0.5f, size};
            control.z_order = z_order;
            if (!fill_quads_instance_by_style(style, clip_id, control)) return false;
            controls.push_back(control);
            return true;
        }

        amal::vec2 clamp_control_center(const amal::vec2 &center, const amal::vec2 &size,
                                        const amal::rect &screen_bounds)
        {
            const amal::vec2 half = size * 0.5f;
            const amal::vec2 minimum = screen_bounds.offset + half;
            const amal::vec2 maximum = screen_bounds.offset + screen_bounds.size - half;
            return {minimum.x <= maximum.x ? amal::clamp(center.x, minimum.x, maximum.x)
                                           : screen_bounds.offset.x + screen_bounds.size.x * 0.5f,
                    minimum.y <= maximum.y ? amal::clamp(center.y, minimum.y, maximum.y)
                                           : screen_bounds.offset.y + screen_bounds.size.y * 0.5f};
        }

        amal::vec2 project_control_to_bounds(const amal::vec2 &origin, const amal::vec2 &target, const amal::vec2 &size,
                                             const amal::rect &screen_bounds)
        {
            const amal::vec2 half = size * 0.5f;
            const amal::vec2 minimum = screen_bounds.offset + half;
            const amal::vec2 maximum = screen_bounds.offset + screen_bounds.size - half;
            if (minimum.x > maximum.x || minimum.y > maximum.y)
                return clamp_control_center(target, size, screen_bounds);
            if (target.x >= minimum.x && target.x <= maximum.x && target.y >= minimum.y && target.y <= maximum.y)
                return target;

            const amal::vec2 direction = target - origin;
            f32 enter = 0.0f;
            f32 exit = 1.0f;
            const auto clip_axis = [&](f32 start, f32 delta, f32 axis_min, f32 axis_max) {
                if (amal::abs(delta) <= 1e-6f) return start >= axis_min && start <= axis_max;
                f32 first = (axis_min - start) / delta;
                f32 second = (axis_max - start) / delta;
                if (first > second) std::swap(first, second);
                enter = amal::max(enter, first);
                exit = amal::min(exit, second);
                return enter <= exit;
            };
            if (clip_axis(origin.x, direction.x, minimum.x, maximum.x) &&
                clip_axis(origin.y, direction.y, minimum.y, maximum.y) && exit >= 0.0f && enter <= 1.0f)
                return origin + direction * amal::clamp(exit, 0.0f, 1.0f);
            return clamp_control_center(target, size, screen_bounds);
        }

        void emit_quad_batch(DrawCtx &ctx, DrawStream *stream, acul::vector<DrawDataID> &draws,
                             const acul::vector<QuadsInstanceData> &instances)
        {
            const u32 previous_count = static_cast<u32>(draws.size());
            const u32 count = static_cast<u32>(instances.size());
            if (ctx.reason & DrawReasonBits::invalidate)
            {
                emit_context_draw_batch(ctx, stream, draws.data(), nullptr, previous_count);
                return;
            }
            const u32 common_count = amal::min(previous_count, count);
            if (common_count > 0u) emit_context_draw_batch(ctx, stream, draws.data(), instances.data(), common_count);
            if (previous_count > count)
            {
                DrawCtx invalidate_ctx = ctx;
                invalidate_ctx.reason |= DrawReasonBits::invalidate;
                emit_context_draw_batch(invalidate_ctx, stream, draws.data() + count, nullptr, previous_count - count);
                draws.resize(count);
            }
            else if (count > previous_count)
            {
                draws.resize(count);
                emit_context_draw_batch(ctx, stream, draws.data() + previous_count, instances.data() + previous_count,
                                        count - previous_count);
            }
        }
    } // namespace

    Curve::Curve(u32 id, const amal::bezier_curve2 &curve, const amal::vec2 &inline_size, WidgetFlags widget_flags,
                 u32 style_tag, u32 line_style_tag)
        : Widget(id, widget_flags, EventFlagBits::none, {{0.0f, 0.0f}, inline_size}, style_tag),
          _curve(std::move(curve)),
          _style{Theme::STYLE_ID_INVALID, style_tag},
          _line_style{Theme::STYLE_ID_INVALID, line_style_tag}
    {
    }

    Curve::~Curve()
    {
        if (_model_binding)
        {
            _model_binding->on_field_change = nullptr;
            detach_model_binding(*_model_binding);
        }
    }

    void Curve::set_curve(const amal::bezier_curve2 &curve)
    {
        _curve = std::move(curve);
        invalidate_curve();
    }

    void Curve::invalidate_curve()
    {
        invalidate_geometry();
        sync_model_binding();
        mark_changed();
    }

    void Curve::set_model_binding(ModelBinding *binding)
    {
        if (_model_binding)
        {
            _model_binding->on_field_change = nullptr;
            detach_model_binding(*_model_binding);
        }
        _model_binding = binding;
        if (!_model_binding) return;
        _model_binding->on_field_change = [this](ModelRecordID, ModelFieldID) {
            amal::bezier_curve2 value;
            if (!read_model_binding_value(*_model_binding, value)) return;
            _syncing_model = true;
            set_curve(std::move(value));
            _syncing_model = false;
        };
        attach_model_binding(*_model_binding);
        amal::bezier_curve2 value;
        if (read_model_binding_value(*_model_binding, value))
        {
            _syncing_model = true;
            set_curve(std::move(value));
            _syncing_model = false;
        }
    }

    void Curve::sync_model_binding()
    {
        if (_model_binding && !_syncing_model) set_model_binding_value<amal::bezier_curve2>(*_model_binding, _curve);
    }

    void Curve::set_view(const amal::rect &view)
    {
        amal::rect value = view;
        value.size.x = amal::max(amal::abs(value.size.x), 1e-5f);
        value.size.y = amal::max(amal::abs(value.size.y), 1e-5f);
        if (_view.offset == value.offset && _view.size == value.size) return;
        _view = value;
        invalidate_geometry();
    }

    void Curve::set_flatten_tolerance(f32 tolerance)
    {
        const f32 value = std::isfinite(tolerance) ? amal::max(tolerance, 0.05f) : 0.5f;
        if (_flatten_tolerance == value) return;
        _flatten_tolerance = value;
        invalidate_geometry();
    }

    void Curve::set_style_tags(u32 style_tag, u32 line_style_tag)
    {
        if (_style.tag_id == style_tag && _line_style.tag_id == line_style_tag) return;
        _style = {Theme::STYLE_ID_INVALID, style_tag};
        _line_style = {Theme::STYLE_ID_INVALID, line_style_tag};
        _geometry_dirty = true;
        if (is_attached()) sync_widget_style();
    }

    StyleUpdateFlags Curve::update_style()
    {
        const u32 parent_id = parent() ? parent()->id() : 0u;
        StyleUpdateFlags flags = resolve_style_selector(_style, id(), parent_id, style_state());
        flags |= resolve_style_selector(_line_style, id(), parent_id, style_state());
        apply_style_layout(get_theme()->get_style(_style.id));
        _geometry_dirty = true;
        return flags;
    }

    void Curve::update_layout_min_size_force()
    {
        if (_style.id == Theme::STYLE_ID_INVALID) update_style();
        const auto &style = get_theme()->get_style(_style.id);
        const amal::vec4 margin = style.margin();
        const amal::vec4 padding = style.padding();
        const f32 width = is_size_concrete(style_size().x) ? style_size().x : style.min_width();
        const f32 height = is_size_concrete(style_size().y) ? style_size().y : style.min_height();
        set_required_size({amal::max(width, padding.x + padding.z) + margin.x + margin.z,
                           amal::max(height, padding.y + padding.w) + margin.y + margin.w});
    }

    void Curve::update_layout(bool min_size_known)
    {
        if (layout_measure_required(min_size_known)) update_layout_min_size_force();
        if (_style.id == Theme::STYLE_ID_INVALID) update_style();
        const auto margin = get_theme()->get_style(_style.id).margin();
        const amal::vec2 minimum{amal::max(required_size().x - margin.x - margin.z, 0.0f),
                                 amal::max(required_size().y - margin.y - margin.w, 0.0f)};
        amal::vec2 next_size{amal::max(size().x - margin.x - margin.z, 0.0f),
                             amal::max(size().y - margin.y - margin.w, 0.0f)};
        next_size.x = fill_width() || is_width_fixed() ? amal::max(next_size.x, minimum.x) : minimum.x;
        next_size.y = fill_height() || is_height_fixed() ? amal::max(next_size.y, minimum.y) : minimum.y;
        if (parent()) set_position(position() + amal::vec2{margin.x, margin.y});
        set_layout_size(next_size);
        Widget::update_layout(true);
        if (parent()) set_clip_id(parent()->content_clip_id());
        else ensure_own_clip_rect({position().x, position().y, size().x, size().y});
        _geometry_dirty = true;
    }

    void Curve::translate(const amal::vec2 &delta)
    {
        if (delta == amal::vec2{}) return;
        Widget::translate(delta);
        _curve_batch.offset += delta;
        _curve_offset_dirty = true;
    }

    void Curve::rebuild_clip_rects()
    {
        if (parent()) set_clip_id(parent()->content_clip_id());
        else ensure_own_clip_rect({position().x, position().y, size().x, size().y});
        _geometry_dirty = true;
    }

    void Curve::reset_draw_records()
    {
        Widget::reset_draw_records();
        _curve_draw = {};
    }

    void Curve::update_depth(const amal::vec2 &depth_range)
    {
        Widget::update_depth(depth_range);
        assign_next_depth(this->depth_range(), _curve_depth_range);
        _geometry_dirty = true;
    }

    void Curve::draw(DrawCtx &ctx)
    {
        if (_style.id == Theme::STYLE_ID_INVALID || _line_style.id == Theme::STYLE_ID_INVALID) update_style();
        if (_geometry_dirty)
        {
            const u32 previous_vertex_count = _curve_batch.vertex_count;
            const u32 previous_index_count = _curve_batch.index_count;
            rebuild_geometry();
            _curve_vertex_bucket =
                grow_geometry_bucket(_curve_vertex_bucket, static_cast<u32>(_curve_vertices.size()), 64u);
            _curve_index_bucket =
                grow_geometry_bucket(_curve_index_bucket, static_cast<u32>(_curve_indices.size()), 192u);
            const size_t used_vertex_count = _curve_vertices.size();
            const size_t used_index_count = _curve_indices.size();
            _curve_vertices.resize(_curve_vertex_bucket);
            _curve_indices.resize(_curve_index_bucket);
            for (size_t i = used_vertex_count; i < _curve_vertices.size(); ++i)
                _curve_vertices[i] = VertexStreamVertex{};
            for (size_t i = used_index_count; i < _curve_indices.size(); ++i) _curve_indices[i] = 0u;
            _curve_batch = {
                _curve_vertices.data(), _curve_indices.data(), _curve_vertex_bucket, _curve_index_bucket, {0.0f, 0.0f}};
            _curve_offset_dirty = false;
            if (_curve_draw.render_id != AUIK_INVALID_DRAW_DATA_ID &&
                (previous_vertex_count != _curve_batch.vertex_count ||
                 previous_index_count != _curve_batch.index_count))
                invalidate_render_draw(get_primary_vertex_stream(), _curve_draw);
        }

        const bool curve_visible = _curve_batch.vertex_count > 0u && _curve_batch.index_count > 0u;
        if ((ctx.reason & DrawReasonBits::record) || curve_visible ||
            _curve_draw.render_id != AUIK_INVALID_DRAW_DATA_ID)
            emit_vertex_stream_batch(ctx, get_primary_vertex_stream(), _curve_draw, _curve_batch, get_rect(),
                                     can_emit_hit(ctx) && _bounds_hit_enabled, _curve_offset_dirty);
        _curve_offset_dirty = false;
        draw_controls(ctx);
    }

    amal::vec2 Curve::curve_to_screen(const amal::vec2 &point) const
    {
        const amal::vec2 normalized = (point - _view.offset) / _view.size;
        return {bounds().offset.x + normalized.x * bounds().size.x,
                bounds().offset.y + (1.0f - normalized.y) * bounds().size.y};
    }

    amal::vec2 Curve::screen_to_curve(const amal::vec2 &point) const
    {
        const amal::vec2 safe_size{amal::max(bounds().size.x, 1e-5f), amal::max(bounds().size.y, 1e-5f)};
        const amal::vec2 normalized{(point.x - bounds().offset.x) / safe_size.x,
                                    1.0f - (point.y - bounds().offset.y) / safe_size.y};
        return _view.offset + normalized * _view.size;
    }

    amal::vec2 Curve::map_to_screen(const amal::vec2 &point) const { return curve_to_screen(point); }

    amal::vec2 Curve::map_to_curve(const amal::vec2 &point) const { return screen_to_curve(point); }

    void Curve::invalidate_geometry()
    {
        _geometry_dirty = true;
        if (!is_attached()) return;
        const bool has_record = _curve_draw.render_id != AUIK_INVALID_DRAW_DATA_ID;
        redraw_external(has_record);
        mark_host_refresh_request();
    }

    void Curve::rebuild_geometry()
    {
        _geometry_dirty = false;
        _flattened_points.clear();
        _screen_points.clear();
        _flattened_points.reserve(_curve.segment_count() * 8u + 1u);
        amal::curve_flatten_projected_with_workspace(
            _curve, AppendIterator<acul::vector<amal::vec2>>{&_flattened_points}, _flatten_workspace,
            [this](const amal::vec2 &point) { return curve_to_screen(point); }, _flatten_tolerance);
        const Style &line_style = get_theme()->get_style(_line_style.id);
        const f32 line_inset = amal::max(line_style.height(), 0.0f) * 0.5f + 1.0f;
        _screen_points.reserve(_flattened_points.size());
        for (const auto &point : _flattened_points)
            _screen_points.push_back(inset_curve_screen_point(curve_to_screen(display_curve_point(point)), line_inset));

        build_polyline(_curve_vertices, _curve_indices, _screen_points, amal::max(line_style.height(), 0.0f),
                       next_depth(_curve_depth_range), line_style.background_color(), clip_id());
        _curve_batch = {_curve_vertices.data(),
                        _curve_indices.data(),
                        static_cast<u32>(_curve_vertices.size()),
                        static_cast<u32>(_curve_indices.size()),
                        {0.0f, 0.0f}};
    }

    void Curve::draw_controls(DrawCtx &) {}

    amal::vec2 ControlCurve::display_curve_point(const amal::vec2 &point) const
    {
        if (!_clamp_enabled) return point;
        return {point.x, amal::clamp(point.y, _clamp_min.y, _clamp_max.y)};
    }

    amal::vec2 ControlCurve::inset_curve_screen_point(const amal::vec2 &point, f32 inset) const
    {
        if (!_clamp_enabled) return point;
        const amal::vec2 corner_a = curve_to_screen(_clamp_min);
        const amal::vec2 corner_b = curve_to_screen(_clamp_max);
        const amal::vec2 minimum{amal::min(corner_a.x, corner_b.x) + inset, amal::min(corner_a.y, corner_b.y) + inset};
        const amal::vec2 maximum{amal::max(corner_a.x, corner_b.x) - inset, amal::max(corner_a.y, corner_b.y) - inset};
        return {minimum.x <= maximum.x ? amal::clamp(point.x, minimum.x, maximum.x) : (corner_a.x + corner_b.x) * 0.5f,
                minimum.y <= maximum.y ? amal::clamp(point.y, minimum.y, maximum.y) : (corner_a.y + corner_b.y) * 0.5f};
    }

    ControlCurve::ControlCurve(u32 id, const amal::bezier_curve2 &curve, const amal::vec2 &inline_size,
                               WidgetFlags widget_flags, u32 style_tag, u32 line_style_tag, u32 control_line_style_tag,
                               u32 point_style_tag, u32 handle_style_tag)
        : Curve(id, std::move(curve), inline_size, widget_flags, style_tag, line_style_tag),
          _control_line_style{Theme::STYLE_ID_INVALID, control_line_style_tag},
          _point_style{Theme::STYLE_ID_INVALID, point_style_tag},
          _handle_style{Theme::STYLE_ID_INVALID, handle_style_tag}
    {
        set_rect_tag_id(AUIK_TAG_PLOT_CONTROL_CURVE);
        set_event_flags(EventFlagBits::click | EventFlagBits::drag | EventFlagBits::hover);
    }

    void ControlCurve::set_control_style_tags(u32 line_style_tag, u32 point_style_tag, u32 handle_style_tag)
    {
        if (_control_line_style.tag_id == line_style_tag && _point_style.tag_id == point_style_tag &&
            _handle_style.tag_id == handle_style_tag)
            return;
        _control_line_style = {Theme::STYLE_ID_INVALID, line_style_tag};
        _point_style = {Theme::STYLE_ID_INVALID, point_style_tag};
        _point_hover_style = {Theme::STYLE_ID_INVALID, point_style_tag};
        _point_active_style = {Theme::STYLE_ID_INVALID, point_style_tag};
        _handle_style = {Theme::STYLE_ID_INVALID, handle_style_tag};
        _handle_hover_style = {Theme::STYLE_ID_INVALID, handle_style_tag};
        _handle_active_style = {Theme::STYLE_ID_INVALID, handle_style_tag};
        _geometry_dirty = true;
        if (is_attached()) sync_widget_style();
    }

    StyleUpdateFlags ControlCurve::update_style()
    {
        StyleUpdateFlags flags = Curve::update_style();
        const u32 parent_id = parent() ? parent()->id() : 0u;
        flags |= resolve_style_selector(_control_line_style, id(), parent_id, style_state());
        flags |= resolve_style_selector(_point_style, _point_style.tag_id, id(), StyleState::normal);
        flags |= resolve_style_selector(_point_hover_style, _point_hover_style.tag_id, id(), StyleState::hover);
        flags |= resolve_style_selector(_point_active_style, _point_active_style.tag_id, id(), StyleState::active);
        flags |= resolve_style_selector(_handle_style, _handle_style.tag_id, id(), StyleState::normal);
        flags |= resolve_style_selector(_handle_hover_style, _handle_hover_style.tag_id, id(), StyleState::hover);
        flags |= resolve_style_selector(_handle_active_style, _handle_active_style.tag_id, id(), StyleState::active);
        return flags;
    }

    void ControlCurve::reset_draw_records()
    {
        Curve::reset_draw_records();
        _control_draws.clear();
        _control_hits.clear();
    }

    void ControlCurve::update_depth(const amal::vec2 &depth_range)
    {
        Widget::update_depth(depth_range);
        DepthCursor cursor(this->depth_range(), get_depth_requirement());
        _curve_depth_range = cursor.next();
        _control_lines_depth_range = cursor.next();
        _control_points_depth_range = cursor.next();
        _geometry_dirty = true;
    }

    void ControlCurve::translate(const amal::vec2 &delta)
    {
        if (delta == amal::vec2{}) return;
        Curve::translate(delta);
        for (auto &control : _controls) control.rect.offset += delta;
        for (auto &rect : _control_rects) rect.bounds.offset += delta;
    }

    void ControlCurve::rebuild_geometry()
    {
        Curve::rebuild_geometry();
        _controls.clear();
        _control_rects.clear();
        while (!_selected_points.empty() && _selected_points.back() >= _curve.points.size())
            _selected_points.pop_back();

        const Style &line_style = get_theme()->get_style(_control_line_style.id);
        const f32 line_z = next_depth(_control_lines_depth_range);
        const f32 point_z = next_depth(_control_points_depth_range);
        const auto &ctx = detail::get_context();
        amal::rect control_bounds = bounds();
        if (_clamp_enabled)
        {
            const amal::vec2 clamp_corner_a = curve_to_screen(_clamp_min);
            const amal::vec2 clamp_corner_b = curve_to_screen(_clamp_max);
            control_bounds = {
                {amal::min(clamp_corner_a.x, clamp_corner_b.x), amal::min(clamp_corner_a.y, clamp_corner_b.y)},
                {amal::abs(clamp_corner_b.x - clamp_corner_a.x), amal::abs(clamp_corner_b.y - clamp_corner_a.y)}};
        }
        const auto add_control_visual = [&](const amal::vec2 &center, const amal::vec2 &origin, size_t point_index,
                                            ControlKind kind, u32 tag_id, const StyleSelector &normal,
                                            const StyleSelector &hover, const StyleSelector &active,
                                            amal::vec2 &display_center) {
            const ElementID element = make_element_id(id(), tag_id, static_cast<u32>(point_index));
            const bool selected = kind == ControlKind::point && is_point_selected(point_index);
            const StyleSelector *selector = &normal;
            if (ctx.io.drag_id == element || selected) selector = &active;
            else if (ctx.hover_id == element) selector = &hover;
            const Style &style = get_theme()->get_style(selector->id);
            const amal::vec2 size{amal::max(style.width(), 0.0f), amal::max(style.height(), 0.0f)};
            display_center = kind == ControlKind::point
                                 ? clamp_control_center(center, size, control_bounds)
                                 : project_control_to_bounds(origin, center, size, control_bounds);
            if (!append_control(_controls, style, display_center, point_z, clip_id())) return false;
            _control_rects.push_back(detail::make_rect_data(id(), tag_id, _controls.back().rect, clip_id(), point_z, 0u,
                                                            static_cast<u32>(point_index)));
            return true;
        };

        acul::vector<amal::vec2> display_knots(_curve.points.size());
        for (size_t point_index = 0u; point_index < _curve.points.size(); ++point_index)
        {
            const auto &point = _curve.points[point_index];
            const amal::vec2 knot = curve_to_screen(point.position);
            add_control_visual(knot, knot, point_index, ControlKind::point, AUIK_TAG_PLOT_CURVE_POINT, _point_style,
                               _point_hover_style, _point_active_style, display_knots[point_index]);
        }

        for (size_t point_index : _selected_points)
        {
            const auto &point = _curve.points[point_index];
            const amal::vec2 knot = curve_to_screen(point.position);
            const amal::vec2 handle_in = curve_to_screen(point.handle_in);
            const amal::vec2 handle_out = curve_to_screen(point.handle_out);
            if (amal::distance(point.position, point.handle_in) > 1e-5f)
            {
                amal::vec2 display_handle{};
                if (add_control_visual(handle_in, knot, point_index, ControlKind::handle_in, AUIK_TAG_PLOT_HANDLE_IN,
                                       _handle_style, _handle_hover_style, _handle_active_style, display_handle))
                    append_segment(_curve_vertices, _curve_indices, display_knots[point_index], display_handle,
                                   line_style.height(), line_z, line_style.background_color(), clip_id());
            }
            if (amal::distance(point.position, point.handle_out) > 1e-5f)
            {
                amal::vec2 display_handle{};
                if (add_control_visual(handle_out, knot, point_index, ControlKind::handle_out, AUIK_TAG_PLOT_HANDLE_OUT,
                                       _handle_style, _handle_hover_style, _handle_active_style, display_handle))
                    append_segment(_curve_vertices, _curve_indices, display_knots[point_index], display_handle,
                                   line_style.height(), line_z, line_style.background_color(), clip_id());
            }
        }
        _curve_batch = {_curve_vertices.data(),
                        _curve_indices.data(),
                        static_cast<u32>(_curve_vertices.size()),
                        static_cast<u32>(_curve_indices.size()),
                        {0.0f, 0.0f}};
    }

    void ControlCurve::draw_controls(DrawCtx &ctx)
    {
        emit_quad_batch(ctx, get_primary_quads_stream(), _control_draws, _controls);
        sync_control_hits(ctx);
    }

    bool ControlCurve::is_point_selected(size_t point_index) const
    {
        return std::binary_search(_selected_points.begin(), _selected_points.end(), point_index);
    }

    void ControlCurve::select_point(size_t point_index, bool additive)
    {
        if (point_index >= _curve.points.size()) return;
        if (!additive)
        {
            if (_selected_points.size() == 1u && _selected_points.front() == point_index) return;
            _selected_points.clear();
            _selected_points.push_back(point_index);
            invalidate_controls();
            return;
        }
        auto it = std::lower_bound(_selected_points.begin(), _selected_points.end(), point_index);
        if (it != _selected_points.end() && *it == point_index) return;
        _selected_points.insert(it, point_index);
        invalidate_controls();
    }

    void ControlCurve::deselect_point(size_t point_index)
    {
        auto it = std::lower_bound(_selected_points.begin(), _selected_points.end(), point_index);
        if (it == _selected_points.end() || *it != point_index) return;
        _selected_points.erase(it);
        invalidate_controls();
    }

    void ControlCurve::set_selected_points(const acul::vector<size_t> &point_indices)
    {
        acul::vector<size_t> selected;
        selected.reserve(point_indices.size());
        for (size_t point_index : point_indices)
            if (point_index < _curve.points.size()) selected.push_back(point_index);
        std::sort(selected.begin(), selected.end());
        auto last = std::unique(selected.begin(), selected.end());
        selected.erase(last, selected.end());
        if (_selected_points == selected) return;
        _selected_points = std::move(selected);
        invalidate_controls();
    }

    void ControlCurve::clear_selection()
    {
        if (_selected_points.empty()) return;
        _selected_points.clear();
        invalidate_controls();
    }

    void ControlCurve::set_clamp(const amal::vec2 &minimum, const amal::vec2 &maximum)
    {
        if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(maximum.x) ||
            !std::isfinite(maximum.y))
        {
            clear_clamp();
            return;
        }
        const amal::vec2 next_min{amal::min(minimum.x, maximum.x), amal::min(minimum.y, maximum.y)};
        const amal::vec2 next_max{amal::max(minimum.x, maximum.x), amal::max(minimum.y, maximum.y)};
        if (_clamp_enabled && _clamp_min == next_min && _clamp_max == next_max) return;
        _clamp_min = next_min;
        _clamp_max = next_max;
        _clamp_enabled = true;
        invalidate_controls();
    }

    void ControlCurve::clear_clamp()
    {
        if (!_clamp_enabled) return;
        _clamp_enabled = false;
        invalidate_controls();
    }

    void ControlCurve::invalidate_controls(bool notify_change)
    {
        _geometry_dirty = true;
        if (notify_change)
        {
            sync_model_binding();
            mark_changed();
        }
        if (!is_attached()) return;
        const bool has_record = _curve_draw.render_id != AUIK_INVALID_DRAW_DATA_ID || !_control_draws.empty();
        redraw_external(has_record);
        mark_host_refresh_request();
    }

    void ControlCurve::sync_control_hits(DrawCtx &ctx)
    {
        if (!ctx.is_hit_allowed) return;
        const size_t previous_count = _control_hits.size();
        const size_t count = _control_rects.size();
        if (ctx.reason & DrawReasonBits::invalidate)
        {
            const detail::RectData empty{};
            for (size_t i = 0u; i < previous_count; ++i) update_hit_rect(_control_hits[i].hit_id, empty, true);
            return;
        }

        const size_t common_count = amal::min(previous_count, count);
        for (size_t i = 0u; i < common_count; ++i) update_hit_rect(_control_hits[i].hit_id, _control_rects[i], true);

        if (previous_count > count)
        {
            const detail::RectData empty{};
            for (size_t i = count; i < previous_count; ++i) update_hit_rect(_control_hits[i].hit_id, empty, true);
            _control_hits.resize(count);
        }
        else if (count > previous_count)
        {
            _control_hits.resize(count);
            for (size_t i = previous_count; i < count; ++i)
                update_hit_rect(_control_hits[i].hit_id, _control_rects[i], true);
        }
    }

    bool ControlCurve::insert_point_at(const amal::vec2 &screen_position, bool automatic)
    {
        if (is_read_only() || _curve.segment_count() == 0u) return false;
        size_t closest_segment = 0u;
        f32 closest_t = 0.0f;
        f32 closest_distance = std::numeric_limits<f32>::max();
        for (size_t segment_index = 0u; segment_index < _curve.segment_count(); ++segment_index)
        {
            const auto segment = amal::curve_segment(_curve, segment_index);
            const amal::cubic_bezier<amal::vec2> screen_segment{
                curve_to_screen(segment.p0), curve_to_screen(segment.p1), curve_to_screen(segment.p2),
                curve_to_screen(segment.p3)};
            const f32 t = amal::bezier_closest_parameter(screen_segment, screen_position);
            const f32 distance = amal::distance(amal::bezier_evaluate(screen_segment, t), screen_position);
            if (distance >= closest_distance) continue;
            closest_distance = distance;
            closest_segment = segment_index;
            closest_t = t;
        }
        if (closest_distance > 8.0f) return false;
        const bool point_inserted = automatic
                                        ? amal::curve_insert_point_auto(_curve, closest_segment, closest_t)
                                        : amal::curve_insert_point_preserving_shape(_curve, closest_segment, closest_t);
        if (!point_inserted) return false;
        const size_t inserted = closest_segment + 1u;
        _selected_points.clear();
        _selected_points.push_back(inserted < _curve.points.size() ? inserted : _curve.points.size() - 1u);
        invalidate_controls(true);
        return true;
    }

    void ControlCurve::on_click(MouseKey key, KeyPressState state, u32 click_count)
    {
        (void)click_count;
        if (key != MouseKey::left) return;
        if (state == KeyPressState::release)
        {
            if (_drag_changed)
            {
                sync_model_binding();
                mark_changed();
            }
            _drag_changed = false;
            invalidate_controls();
            return;
        }
        if (state != KeyPressState::press) return;
        const ElementID target = detail::get_context().hover_id;
        const KeyMode mods = active_key_mods();
        if (target.widget_id != id()) return;

        if (target.tag_id == AUIK_TAG_PLOT_CURVE_POINT && target.element_id < _curve.points.size())
        {
            const size_t point_index = target.element_id;
            if (mods & KeyModeBits::control)
            {
                if (is_point_selected(point_index)) deselect_point(point_index);
                else select_point(point_index, true);
            }
            else select_point(point_index);
            return;
        }
        if ((target.tag_id == AUIK_TAG_PLOT_HANDLE_IN || target.tag_id == AUIK_TAG_PLOT_HANDLE_OUT) &&
            target.element_id < _curve.points.size())
        {
            select_point(target.element_id);
            invalidate_controls();
            return;
        }
        const bool automatic_insert = mods & KeyModeBits::shift;
        if ((automatic_insert || (mods & KeyModeBits::control)) && insert_point_at(get_mouse_pos(), automatic_insert))
            return;
        clear_selection();
    }

    void ControlCurve::on_hover(HoverState state)
    {
        (void)state;
        invalidate_controls();
    }

    void ControlCurve::on_drag(const amal::vec2 &delta, KeyPressState state)
    {
        if (state == KeyPressState::release)
        {
            if (_drag_changed)
            {
                sync_model_binding();
                mark_changed();
            }
            _drag_changed = false;
            invalidate_controls();
            return;
        }
        if (is_read_only() || (state != KeyPressState::press && state != KeyPressState::repeat)) return;
        const ElementID target = detail::get_context().io.drag_id;
        if (target.widget_id != id() || target.element_id >= _curve.points.size()) return;
        amal::vec2 logical_delta{delta.x * _view.size.x / amal::max(bounds().size.x, 1e-5f),
                                 -delta.y * _view.size.y / amal::max(bounds().size.y, 1e-5f)};
        if (target.tag_id == AUIK_TAG_PLOT_CURVE_POINT)
        {
            if (!is_point_selected(target.element_id)) select_point(target.element_id);
            if (_clamp_enabled && !_selected_points.empty())
            {
                amal::vec2 selected_min = _curve.points[_selected_points.front()].position;
                amal::vec2 selected_max = selected_min;
                for (size_t point_index : _selected_points)
                {
                    const auto &position = _curve.points[point_index].position;
                    selected_min = {amal::min(selected_min.x, position.x), amal::min(selected_min.y, position.y)};
                    selected_max = {amal::max(selected_max.x, position.x), amal::max(selected_max.y, position.y)};
                }
                const amal::vec2 minimum_delta = _clamp_min - selected_min;
                const amal::vec2 maximum_delta = _clamp_max - selected_max;
                if (minimum_delta.x <= maximum_delta.x)
                    logical_delta.x = amal::clamp(logical_delta.x, minimum_delta.x, maximum_delta.x);
                if (minimum_delta.y <= maximum_delta.y)
                    logical_delta.y = amal::clamp(logical_delta.y, minimum_delta.y, maximum_delta.y);
            }
            for (size_t point_index : _selected_points)
            {
                auto &point = _curve.points[point_index];
                amal::vec2 position = point.position + logical_delta;
                if (_clamp_enabled)
                {
                    position.x = amal::clamp(position.x, _clamp_min.x, _clamp_max.x);
                    position.y = amal::clamp(position.y, _clamp_min.y, _clamp_max.y);
                }
                amal::bezier_point_set_position(point, position);
            }
        }
        else if (target.tag_id == AUIK_TAG_PLOT_HANDLE_IN || target.tag_id == AUIK_TAG_PLOT_HANDLE_OUT)
        {
            auto &point = _curve.points[target.element_id];
            const auto handle =
                target.tag_id == AUIK_TAG_PLOT_HANDLE_IN ? amal::bezier_handle::in : amal::bezier_handle::out;
            const amal::vec2 current = handle == amal::bezier_handle::in ? point.handle_in : point.handle_out;
            const KeyMode mods = active_key_mods();
            amal::vec2 next = current + logical_delta;
            const bool one_sided_endpoint =
                !_curve.closed &&
                ((target.element_id == 0u && handle == amal::bezier_handle::out) ||
                 (target.element_id + 1u == _curve.points.size() && handle == amal::bezier_handle::in));
            if ((mods & KeyModeBits::alt) || one_sided_endpoint)
            {
                if (_clamp_enabled) next.x = amal::clamp(next.x, _clamp_min.x, _clamp_max.x);
                if (handle == amal::bezier_handle::in) point.handle_in = next;
                else point.handle_out = next;
                if (one_sided_endpoint)
                {
                    if (handle == amal::bezier_handle::in) point.handle_out = point.position;
                    else point.handle_in = point.position;
                }
            }
            else
            {
                if (_clamp_enabled)
                {
                    const auto clamp_mirrored_axis = [](f32 value, f32 origin, f32 minimum, f32 maximum) {
                        const f32 mirrored_min = origin * 2.0f - maximum;
                        const f32 mirrored_max = origin * 2.0f - minimum;
                        const f32 allowed_min = amal::max(minimum, mirrored_min);
                        const f32 allowed_max = amal::min(maximum, mirrored_max);
                        return allowed_min <= allowed_max ? amal::clamp(value, allowed_min, allowed_max)
                                                          : amal::clamp(value, minimum, maximum);
                    };
                    next.x = clamp_mirrored_axis(next.x, point.position.x, _clamp_min.x, _clamp_max.x);
                }
                amal::bezier_point_set_handle(point, handle, next, amal::bezier_handle_mode::mirrored);
            }
        }
        else return;
        _drag_changed = true;
        invalidate_controls();
    }
} // namespace auik::plot
