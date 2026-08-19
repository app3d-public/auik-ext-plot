#include <acul/string/utils.hpp>
#include <auik/auik.hpp>
#include <auik/ext/plot/grid.hpp>
#include <auik/widgets/text.hpp>

#define MAX_GRID_LINES_PER_AXIS 16384u

namespace auik::plot
{
    struct Grid::Hints
    {
        struct Labels
        {
            acul::vector<f32> values;
            acul::vector<Text *> widgets;
            u32 precision = 1u;
            u32 style_tag = AUIK_STYLE_TAG_PLOT_GRID_LABEL;
        };

        Labels x_labels;
        Labels y_labels;
        acul::vector<GridBaseline> baselines;
        acul::vector<StyleSelector> baseline_styles;
        acul::vector<QuadsInstanceData> baseline_lines;
        acul::vector<DrawDataID> baseline_draws;
        StyleSelector label_background_style{Theme::STYLE_ID_INVALID, AUIK_STYLE_TAG_PLOT_GRID_LABEL_BACKGROUND};
        acul::vector<QuadsInstanceData> label_backgrounds;
        acul::vector<DrawDataID> label_background_draws;
        amal::vec2 unit_size{1.0f, 1.0f};
        bool custom_unit_size = false;
        bool style_dirty = true;

        ~Hints()
        {
            for (auto *label : x_labels.widgets) acul::release(label);
            for (auto *label : y_labels.widgets) acul::release(label);
        }
    };

    namespace
    {
        f32 sanitize_spacing(f32 value, f32 fallback)
        {
            if (!std::isfinite(value)) return fallback;
            return amal::max(amal::abs(value), 1e-4f);
        }

        amal::rect transformed_view(const amal::rect &view, const amal::vec2 &scale, const amal::vec2 &offset)
        {
            const amal::vec2 size = view.size / scale;
            return {view.offset + (view.size - size) * 0.5f + offset, size};
        }

        f32 limit_rendered_spacing(f32 spacing, f32 extent)
        {
            const f32 estimated_count = amal::ceil(extent / spacing) + 1.0f;
            if (estimated_count <= static_cast<f32>(MAX_GRID_LINES_PER_AXIS)) return spacing;
            const f32 stride = amal::ceil(estimated_count / static_cast<f32>(MAX_GRID_LINES_PER_AXIS));
            return spacing * stride;
        }

        void append_line(acul::vector<QuadsInstanceData> &lines, const Style &style, const amal::rect &rect,
                         f32 z_order, u16 clip_id)
        {
            QuadsInstanceData line{};
            line.rect = rect;
            line.z_order = z_order;
            if (fill_quads_instance_by_style(style, clip_id, line)) lines.push_back(line);
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

        template <class Labels>
        void release_labels(Labels &labels, bool invalidate)
        {
            for (auto *label : labels.widgets)
            {
                if (invalidate) label->invalidate_draw_commands();
                acul::release(label);
            }
            labels.widgets.clear();
        }

        template <class Labels>
        void rebuild_label_widgets(Grid *grid, Labels &labels, bool invalidate)
        {
            release_labels(labels, invalidate);
            labels.widgets.reserve(labels.values.size());
            for (f32 value : labels.values)
            {
                auto *label = acul::alloc<Text>(
                    AUIK_TAG_TEXT, acul::format("%.*f", static_cast<int>(labels.precision), static_cast<f64>(value)),
                    amal::vec2{AUIK_SIZE_X_FIT, AUIK_SIZE_Y_FIT}, WidgetFlagBits::visible,
                    make_text_layout_flags(TextOverflowMode::clip, TextWrapMode::none, TextLayoutWidthMode::natural));
                label->set_style_tag(labels.style_tag);
                label->set_parent(grid);
                labels.widgets.push_back(label);
            }
        }
    } // namespace

    Grid::Grid(u32 id, const amal::vec2 &inline_size, WidgetFlags widget_flags, u32 style_tag, u32 line_style_tag)
        : Widget(id, widget_flags, EventFlagBits::none, {{0.0f, 0.0f}, inline_size}, style_tag),
          _style{Theme::STYLE_ID_INVALID, style_tag},
          _line_style{Theme::STYLE_ID_INVALID, line_style_tag}
    {
    }

    Grid::~Grid()
    {
        if (_model_binding)
        {
            _model_binding->on_field_change = nullptr;
            detach_model_binding(*_model_binding);
        }
        if (_hints) acul::release(_hints);
    }

    const acul::vector<f32> &Grid::label_values(amal::axis axis) const
    {
        static const acul::vector<f32> empty;
        if (!_hints) return empty;
        if (axis == amal::axis::x) return _hints->x_labels.values;
        if (axis == amal::axis::y) return _hints->y_labels.values;
        return empty;
    }

    const acul::vector<GridBaseline> &Grid::baselines() const
    {
        static const acul::vector<GridBaseline> empty;
        return _hints ? _hints->baselines : empty;
    }

    amal::vec2 Grid::unit_size() const
    {
        return _hints && _hints->custom_unit_size ? _hints->unit_size : amal::vec2{1.0f, 1.0f};
    }

    amal::vec2 Grid::scaled_unit_size() const { return unit_size() * _scale; }

    GridState Grid::state() const
    {
        return {_view, _divisions, _division_axis, unit_size(), _scale, _offset, _min_scale, _max_scale,
                _hints && _hints->custom_unit_size};
    }

    void Grid::sync_model_binding()
    {
        if (_model_binding && !_syncing_model) set_model_binding_value<GridState>(*_model_binding, state());
    }

    void Grid::set_view(const amal::rect &view)
    {
        amal::rect value = view;
        value.size.x = sanitize_spacing(value.size.x, 1.0f);
        value.size.y = sanitize_spacing(value.size.y, 1.0f);
        if (!std::isfinite(value.offset.x)) value.offset.x = 0.0f;
        if (!std::isfinite(value.offset.y)) value.offset.y = 0.0f;
        if (_view.offset == value.offset && _view.size == value.size) return;
        _view = value;
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_divisions(u32 count, amal::axis axis)
    {
        count = amal::clamp(count, 1u, MAX_GRID_LINES_PER_AXIS - 1u);
        if (axis != amal::axis::x && axis != amal::axis::y) axis = amal::axis::x;
        if (_divisions == count && _division_axis == axis) return;
        _divisions = count;
        _division_axis = axis;
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_unit_size(const amal::vec2 &unit_size)
    {
        const amal::vec2 value{sanitize_spacing(unit_size.x, 1.0f), sanitize_spacing(unit_size.y, 1.0f)};
        if (!_hints) _hints = acul::alloc<Hints>();
        if (_hints->custom_unit_size && _hints->unit_size == value) return;
        _hints->unit_size = value;
        _hints->custom_unit_size = true;
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_scale(f32 scale) { set_scale(amal::vec2{scale, scale}); }

    void Grid::set_scale(const amal::vec2 &scale)
    {
        const auto sanitize = [&](f32 value) {
            if (!std::isfinite(value)) return 1.0f;
            return amal::clamp(value, _min_scale, _max_scale);
        };
        const amal::vec2 value{sanitize(scale.x), sanitize(scale.y)};
        if (_scale == value) return;
        _scale = value;
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_scale_limits(f32 min_scale, f32 max_scale)
    {
        if (!std::isfinite(min_scale)) min_scale = 0.05f;
        if (!std::isfinite(max_scale)) max_scale = 100.0f;
        min_scale = amal::max(min_scale, 1e-4f);
        max_scale = amal::max(max_scale, min_scale);
        if (_min_scale == min_scale && _max_scale == max_scale) return;
        _min_scale = min_scale;
        _max_scale = max_scale;
        set_scale(_scale);
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_offset(const amal::vec2 &offset)
    {
        if (_offset == offset) return;
        _offset = offset;
        invalidate_geometry();
        sync_model_binding();
    }

    void Grid::set_transform(const amal::vec2 &scale, const amal::vec2 &offset)
    {
        set_scale(scale);
        set_offset(offset);
    }

    void Grid::set_state(const GridState &grid_state)
    {
        const bool was_syncing = _syncing_model;
        _syncing_model = true;
        set_view(grid_state.view);
        set_divisions(grid_state.divisions, grid_state.division_axis);
        set_scale_limits(grid_state.min_scale, grid_state.max_scale);
        if (grid_state.custom_unit_size) set_unit_size(grid_state.unit_size);
        else if (_hints && _hints->custom_unit_size)
        {
            _hints->custom_unit_size = false;
            invalidate_geometry();
        }
        set_transform(grid_state.scale, grid_state.offset);
        _syncing_model = was_syncing;
        sync_model_binding();
    }

    void Grid::set_model_binding(ModelBinding *binding)
    {
        if (_model_binding)
        {
            _model_binding->on_field_change = nullptr;
            detach_model_binding(*_model_binding);
        }
        _model_binding = binding;
        if (!_model_binding) return;
        _model_binding->on_field_change = [this](ModelRecordID, ModelFieldID) {
            GridState value{};
            if (!read_model_binding_value(*_model_binding, value)) return;
            const bool was_syncing = _syncing_model;
            _syncing_model = true;
            set_state(value);
            _syncing_model = was_syncing;
        };
        attach_model_binding(*_model_binding);
        GridState value{};
        if (read_model_binding_value(*_model_binding, value))
        {
            const bool was_syncing = _syncing_model;
            _syncing_model = true;
            set_state(value);
            _syncing_model = was_syncing;
        }
    }

    void Grid::set_style_tags(u32 style_tag, u32 line_style_tag)
    {
        if (_style.tag_id == style_tag && _line_style.tag_id == line_style_tag) return;
        _style = {Theme::STYLE_ID_INVALID, style_tag};
        _line_style = {Theme::STYLE_ID_INVALID, line_style_tag};
        _geometry_dirty = true;
        if (is_attached()) sync_widget_style();
    }

    void Grid::set_label_values(amal::axis axis, const acul::vector<f32> &values, u32 precision, u32 style_tag)
    {
        if (axis != amal::axis::x && axis != amal::axis::y) return;
        if (!_hints) _hints = acul::alloc<Hints>();
        auto &labels = axis == amal::axis::x ? _hints->x_labels : _hints->y_labels;
        labels.values = values;
        labels.precision = amal::min(precision, 9u);
        labels.style_tag = style_tag;
        rebuild_label_widgets(this, labels, is_attached());
        _hints->style_dirty = true;
        if (is_attached()) sync_widget_style();
        invalidate_geometry();
    }

    void Grid::clear_label_values(amal::axis axis)
    {
        if (!_hints || (axis != amal::axis::x && axis != amal::axis::y)) return;
        auto &labels = axis == amal::axis::x ? _hints->x_labels : _hints->y_labels;
        labels.values.clear();
        release_labels(labels, is_attached());
        invalidate_geometry();
    }

    void Grid::set_baselines(const acul::vector<GridBaseline> &baselines)
    {
        if (!_hints) _hints = acul::alloc<Hints>();
        _hints->baselines = baselines;
        _hints->baseline_styles.clear();
        _hints->baseline_styles.reserve(baselines.size());
        for (const auto &baseline : baselines)
            _hints->baseline_styles.push_back({Theme::STYLE_ID_INVALID, baseline.style_tag});
        _hints->style_dirty = true;
        if (is_attached()) sync_widget_style();
        invalidate_geometry();
    }

    void Grid::clear_baselines()
    {
        if (!_hints || _hints->baselines.empty()) return;
        _hints->baselines.clear();
        _hints->baseline_styles.clear();
        invalidate_geometry();
    }

    StyleUpdateFlags Grid::update_style()
    {
        const u32 parent_id = parent() ? parent()->id() : 0u;
        StyleUpdateFlags flags = resolve_style_selector(_style, id(), parent_id, style_state());
        flags |= resolve_style_selector(_line_style, id(), parent_id, style_state());
        if (_hints)
        {
            for (auto &style : _hints->baseline_styles)
                flags |= resolve_style_selector(style, id(), parent_id, style_state());
            flags |= resolve_style_selector(_hints->label_background_style, id(), parent_id, style_state());
            for (auto *label : _hints->x_labels.widgets) flags |= label->update_style_invalidated();
            for (auto *label : _hints->y_labels.widgets) flags |= label->update_style_invalidated();
            _hints->style_dirty = false;
        }
        apply_style_layout(get_theme()->get_style(_style.id));
        _geometry_dirty = true;
        return flags;
    }

    void Grid::update_layout_min_size_force()
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

    void Grid::update_layout(bool min_size_known)
    {
        if (layout_measure_required(min_size_known)) update_layout_min_size_force();
        if (_style.id == Theme::STYLE_ID_INVALID) update_style();
        const auto &style = get_theme()->get_style(_style.id);
        const amal::vec4 margin = style.margin();
        const amal::vec2 min_grid_size{amal::max(required_size().x - margin.x - margin.z, 0.0f),
                                       amal::max(required_size().y - margin.y - margin.w, 0.0f)};
        amal::vec2 grid_size{amal::max(size().x - margin.x - margin.z, 0.0f),
                             amal::max(size().y - margin.y - margin.w, 0.0f)};
        if (fill_width() || is_width_fixed()) grid_size.x = amal::max(grid_size.x, min_grid_size.x);
        else grid_size.x = min_grid_size.x;
        if (fill_height() || is_height_fixed()) grid_size.y = amal::max(grid_size.y, min_grid_size.y);
        else grid_size.y = min_grid_size.y;
        if (parent()) set_position(position() + amal::vec2{margin.x, margin.y});
        set_layout_size(grid_size);
        Widget::update_layout(true);
        if (parent()) set_clip_id(parent()->content_clip_id());
        else ensure_own_clip_rect({position().x, position().y, size().x, size().y});
        _geometry_dirty = true;
    }

    void Grid::translate(const amal::vec2 &delta)
    {
        if (delta.x == 0.0f && delta.y == 0.0f) return;
        Widget::translate(delta);
        if (!_geometry_dirty)
            for (auto &line : _lines) line.rect.offset += delta;
        if (_hints && !_geometry_dirty)
        {
            for (auto &line : _hints->baseline_lines) line.rect.offset += delta;
            for (auto &background : _hints->label_backgrounds) background.rect.offset += delta;
            for (auto *label : _hints->x_labels.widgets) label->translate(delta);
            for (auto *label : _hints->y_labels.widgets) label->translate(delta);
        }
    }

    void Grid::rebuild_clip_rects()
    {
        if (parent()) set_clip_id(parent()->content_clip_id());
        else ensure_own_clip_rect({position().x, position().y, size().x, size().y});
        invalidate_hit_rect(_background_draw);
        if (_hints)
        {
            for (auto *label : _hints->x_labels.widgets)
            {
                label->set_clip_id(clip_id());
                label->rebuild_clip_rects();
            }
            for (auto *label : _hints->y_labels.widgets)
            {
                label->set_clip_id(clip_id());
                label->rebuild_clip_rects();
            }
        }
        _geometry_dirty = true;
    }

    void Grid::reset_draw_records()
    {
        Widget::reset_draw_records();
        _background_draw = {};
        _rounded_mask_draw = {};
        _line_draws.clear();
        if (_hints)
        {
            _hints->baseline_draws.clear();
            _hints->label_background_draws.clear();
            for (auto *label : _hints->x_labels.widgets) label->reset_draw_records();
            for (auto *label : _hints->y_labels.widgets) label->reset_draw_records();
        }
    }

    void Grid::update_depth(const amal::vec2 &depth_range)
    {
        Widget::update_depth(depth_range);
        DepthCursor cursor(this->depth_range(), get_depth_requirement());
        _background_depth_range = cursor.next();
        _lines_depth_range = cursor.next();
        _baselines_depth_range = cursor.next();
        _rounded_mask_depth_range = cursor.next();
        _label_backgrounds_depth_range = cursor.next();
        _labels_depth_range = cursor.next();
        if (_hints)
        {
            for (auto *label : _hints->x_labels.widgets) label->update_depth(_labels_depth_range);
            for (auto *label : _hints->y_labels.widgets) label->update_depth(_labels_depth_range);
        }
        _geometry_dirty = true;
    }

    void Grid::draw(DrawCtx &ctx)
    {
        if (_style.id == Theme::STYLE_ID_INVALID || _line_style.id == Theme::STYLE_ID_INVALID ||
            (_hints && _hints->style_dirty))
            update_style();
        if (_geometry_dirty) rebuild_geometry();

        auto *quads_stream = get_primary_quads_stream();
        const Style &style = get_theme()->get_style(_style.id);
        QuadsInstanceData background{};
        background.rect = bounds();
        background.z_order = next_depth(_background_depth_range);
        const bool background_visible = fill_quads_instance_by_style(style, clip_id(), background);
        emit_quads_instance(ctx, quads_stream, _background_draw, background, get_rect(), background_visible, false);

        QuadsInstanceData rounded_mask = background;
        rounded_mask.background_color = 0u;
        rounded_mask.border_color = 0u;
        rounded_mask.border_thickness = 0.0f;
        rounded_mask.z_order = next_depth(_rounded_mask_depth_range);
        rounded_mask.mask &= ~(static_cast<u32>(AUIK_HAS_BORDER_BIT | AUIK_HAS_CHECKER_BIT) << 20u);
        rounded_mask.mask |= static_cast<u32>(AUIK_INVERT_RADIUS_BIT) << 20u;
        const bool rounded_mask_visible =
            (style.mask() & detail::StylePropertiesBits::border_radius) && background.border_radius > 0.0f &&
            style.corner_mask() != 0u;
        emit_quads_instance(ctx, quads_stream, _rounded_mask_draw, rounded_mask, get_rect(), rounded_mask_visible, false);

        emit_quad_batch(ctx, quads_stream, _line_draws, _lines);
        if (_hints)
        {
            emit_quad_batch(ctx, quads_stream, _hints->baseline_draws, _hints->baseline_lines);
            emit_quad_batch(ctx, quads_stream, _hints->label_background_draws, _hints->label_backgrounds);
            DrawCtx label_ctx = ctx;
            label_ctx.is_hit_allowed = false;
            for (auto *label : _hints->x_labels.widgets) label->draw_local(label_ctx);
            for (auto *label : _hints->y_labels.widgets) label->draw_local(label_ctx);
        }
    }

    void Grid::invalidate_geometry()
    {
        _geometry_dirty = true;
        if (!is_attached()) return;
        bool has_record = _background_draw.render_id != AUIK_INVALID_DRAW_DATA_ID ||
                          _rounded_mask_draw.render_id != AUIK_INVALID_DRAW_DATA_ID || !_line_draws.empty();
        if (_hints)
        {
            has_record |= !_hints->baseline_draws.empty();
            has_record |= !_hints->label_background_draws.empty();
            for (auto *label : _hints->x_labels.widgets) has_record |= label->draw_record_count() > 0u;
            for (auto *label : _hints->y_labels.widgets) has_record |= label->draw_record_count() > 0u;
        }
        redraw_external(has_record);
        mark_host_refresh_request();
    }

    void Grid::rebuild_geometry()
    {
        _lines.clear();
        if (_hints)
        {
            _hints->baseline_lines.clear();
            _hints->label_backgrounds.clear();
        }
        _geometry_dirty = false;

        const amal::rect rect = bounds();
        if (rect.size.x <= 0.0f || rect.size.y <= 0.0f || clip_id() == 0xFFFFu) return;

        const Style &line_style = get_theme()->get_style(_line_style.id);
        const bool has_line_color = line_style.mask() & detail::StylePropertiesBits::background_color;
        const f32 vertical_width = amal::max(line_style.width(), 0.0f);
        const f32 horizontal_height = amal::max(line_style.height(), 0.0f);

        const amal::rect view = transformed_view(_view, _scale, _offset);
        f32 step_x = 0.0f;
        f32 step_y = 0.0f;
        const f32 division_count = static_cast<f32>(_divisions);
        if (_division_axis == amal::axis::x)
        {
            step_x = _view.size.x / division_count;
            const f32 cell_size = step_x / view.size.x * rect.size.x;
            step_y = cell_size / rect.size.y * view.size.y;
        }
        else
        {
            step_y = _view.size.y / division_count;
            const f32 cell_size = step_y / view.size.y * rect.size.y;
            step_x = cell_size / rect.size.x * view.size.x;
        }
        step_x = limit_rendered_spacing(step_x, view.size.x);
        step_y = limit_rendered_spacing(step_y, view.size.y);
        const f32 first_value_x = amal::ceil(view.offset.x / step_x) * step_x;
        const f32 first_value_y = amal::ceil(view.offset.y / step_y) * step_y;
        const f32 max_value_x = view.offset.x + view.size.x;
        const f32 max_value_y = view.offset.y + view.size.y;
        const f32 right = rect.offset.x + rect.size.x;
        const f32 bottom = rect.offset.y + rect.size.y;
        const u32 vertical_count =
            first_value_x <= max_value_x
                ? static_cast<u32>(amal::floor((max_value_x - first_value_x) / step_x)) + 1u
                : 0u;
        const u32 horizontal_count =
            first_value_y <= max_value_y
                ? static_cast<u32>(amal::floor((max_value_y - first_value_y) / step_y)) + 1u
                : 0u;
        _lines.reserve(static_cast<size_t>(vertical_count + horizontal_count));

        const auto map_x = [&](f32 value) {
            return rect.offset.x + (value - view.offset.x) / view.size.x * rect.size.x;
        };
        const auto map_y = [&](f32 value) {
            return bottom - (value - view.offset.y) / view.size.y * rect.size.y;
        };

        const f32 z_order = next_depth(_lines_depth_range);
        if (has_line_color && vertical_width > 0.0f)
            for (u32 i = 0u; i < vertical_count; ++i)
            {
                const f32 x = map_x(first_value_x + static_cast<f32>(i) * step_x);
                append_line(_lines, line_style,
                            {{x - vertical_width * 0.5f, rect.offset.y}, {vertical_width, rect.size.y}}, z_order,
                            clip_id());
            }
        if (has_line_color && horizontal_height > 0.0f)
        {
            for (u32 i = 0u; i < horizontal_count; ++i)
            {
                const f32 y = map_y(first_value_y + static_cast<f32>(i) * step_y);
                append_line(_lines, line_style,
                            {{rect.offset.x, y - horizontal_height * 0.5f}, {rect.size.x, horizontal_height}}, z_order,
                            clip_id());
            }
        }

        if (!_hints) return;

        const amal::vec2 coordinate_unit = unit_size();
        const f32 baseline_z = next_depth(_baselines_depth_range);
        for (size_t i = 0; i < _hints->baselines.size(); ++i)
        {
            const auto &baseline = _hints->baselines[i];
            if (baseline.axis != amal::axis::x && baseline.axis != amal::axis::y) continue;
            const Style &style = get_theme()->get_style(_hints->baseline_styles[i].id);
            if (!(style.mask() & detail::StylePropertiesBits::background_color)) continue;
            if (baseline.axis == amal::axis::x)
            {
                const f32 thickness = amal::max(style.height(), 0.0f);
                const f32 y = map_y(baseline.coordinate * coordinate_unit.y);
                if (thickness > 0.0f && y >= rect.offset.y && y <= bottom)
                    append_line(_hints->baseline_lines, style,
                                {{rect.offset.x, y - thickness * 0.5f}, {rect.size.x, thickness}}, baseline_z,
                                clip_id());
            }
            else
            {
                const f32 thickness = amal::max(style.width(), 0.0f);
                const f32 x = map_x(baseline.coordinate * coordinate_unit.x);
                if (thickness > 0.0f && x >= rect.offset.x && x <= right)
                    append_line(_hints->baseline_lines, style,
                                {{x - thickness * 0.5f, rect.offset.y}, {thickness, rect.size.y}}, baseline_z,
                                clip_id());
            }
        }

        const auto layout_labels = [&](Hints::Labels &labels, amal::axis axis) {
            const size_t count = amal::min(labels.values.size(), labels.widgets.size());
            for (size_t i = 0; i < count; ++i)
            {
                auto *label = labels.widgets[i];
                label->update_layout_min_size_force();
                const amal::vec2 required = label->required_size();
                if (axis == amal::axis::x)
                {
                    const f32 x = map_x(labels.values[i] * coordinate_unit.x);
                    const f32 label_x =
                        amal::clamp(x - required.x * 0.5f, rect.offset.x, amal::max(right - required.x, rect.offset.x));
                    label->set_position({label_x, bottom - required.y});
                }
                else
                {
                    const f32 y = map_y(labels.values[i] * coordinate_unit.y);
                    const f32 label_y = amal::clamp(y - required.y * 0.5f, rect.offset.y,
                                                    amal::max(bottom - required.y, rect.offset.y));
                    label->set_position({rect.offset.x, label_y});
                }
                label->set_layout_size(required);
                label->update_layout(true);
                label->set_clip_id(clip_id());
                label->update_depth(_labels_depth_range);
                append_line(_hints->label_backgrounds, get_theme()->get_style(_hints->label_background_style.id),
                            label->bounds(), next_depth(_label_backgrounds_depth_range), clip_id());
            }
        };
        layout_labels(_hints->x_labels, amal::axis::x);
        layout_labels(_hints->y_labels, amal::axis::y);
    }
} // namespace auik::plot
