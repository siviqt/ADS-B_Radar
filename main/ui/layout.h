#pragma once
namespace adsb_ui {
struct Layout {
    static constexpr int width=800, height=480, header=30, footer=32;
    static constexpr int content_y=header, content_height=height-header-footer;
    static constexpr int margin=12, row_height=28, rows=10;
};
static_assert(Layout::content_height==418);
}
