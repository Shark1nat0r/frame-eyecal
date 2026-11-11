#include "canvas.h"

#include <cstddef>
#include <ft2build.h>
#include <string>
#include FT_FREETYPE_H
#include <png.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "common.h"

// Text: the system's Noto Sans through FreeType. Size s: capital letters are 7*s pixels high.
static FT_Face g_face;
bool font_init() {
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib)) return false;
    for (const char *p : {"/usr/share/fonts/noto/NotoSans-Medium.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans.ttf"})
        if (!FT_New_Face(lib, p, 0, &g_face)) return true;
    return false;
}
static void font_size(int s) { // setting the size, even to the same one, makes FreeType redo the font's setup
    static int cur = 0;
    if (s != cur) {
        FT_Set_Pixel_Sizes(g_face, 0, 10 * s);
        cur = s;
    }
}
static int advance(uint32_t cp, int s) { // a glyph's advance in pixels at size s, cached
    static std::unordered_map<uint64_t, int> cache;
    auto [it, fresh] = cache.try_emplace(static_cast<uint64_t>(s) << 32 | cp, 0);
    if (fresh) {
        font_size(s);
        if (!FT_Load_Char(g_face, cp, FT_LOAD_DEFAULT)) it->second = static_cast<int>(g_face->glyph->advance.x >> 6);
    }
    return it->second;
}

void Canvas::fill(int x0, int y0, int x1, int y1, Col c, unsigned char a) {
    for (int y = std::max(y0, 0); y < std::min(y1, kTex); y++)
        for (int x = std::max(x0, 0); x < std::min(x1, kTex); x++) {
            unsigned char *p = &px[(static_cast<size_t>(y) * kTex + x) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = a;
        }
}

int Canvas::width(const std::string &t, int s) {
    int w = 0;
    for (size_t i = 0; i < t.size();) w += advance(next_cp(t, i), s);
    return w;
}

void Canvas::text(const std::string &t, int x, int y, int s, Col c, bool centre) {
    if (centre) x -= width(t, s) / 2;
    font_size(s);
    for (size_t i = 0; i < t.size();) {
        if (FT_Load_Char(g_face, next_cp(t, i), FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = g_face->glyph;
        for (int r = 0; r < static_cast<int>(g->bitmap.rows); r++)
            for (int k = 0; k < static_cast<int>(g->bitmap.width); k++) {
                int px_ = x + g->bitmap_left + k, py = y + 7 * s - g->bitmap_top + r,
                    a = g->bitmap.buffer[r * g->bitmap.pitch + k];
                if (!a || px_ < 0 || py < 0 || px_ >= kTex || py >= kTex) continue;
                unsigned char *p = &px[(static_cast<size_t>(py) * kTex + px_) * 4];
                p[0] = static_cast<unsigned char>((p[0] * (255 - a) + c.r * a) / 255);
                p[1] = static_cast<unsigned char>((p[1] * (255 - a) + c.g * a) / 255);
                p[2] = static_cast<unsigned char>((p[2] * (255 - a) + c.b * a) / 255);
                p[3] = std::max(p[3], static_cast<unsigned char>(a));
            }
        x += static_cast<int>(g->advance.x >> 6);
    }
}

int Canvas::para(const std::string &t, int y, int s, Col c, int margin, int x0) {
    if (x0 < 0) x0 = margin;
    std::string line, word;
    int lw = 0; // width of line
    auto flush = [&] {
        text(line, x0, y, s, c, false);
        y += 13 * s;
        line.clear();
        lw = 0;
    };
    for (size_t i = 0; i <= t.size(); i++) {
        if (i < t.size() && t[i] != ' ' && t[i] != '\n') {
            word += t[i];
            continue;
        }
        int ww = width(word, s), sp = line.empty() ? 0 : width(" ", s);
        if (!line.empty() && lw + sp + ww > kTex - margin - x0) {
            flush();
            sp = 0;
        }
        if (!word.empty()) {
            line += (line.empty() ? "" : " ") + word;
            lw += sp + ww;
        }
        word.clear();
        if (i < t.size() && t[i] == '\n') flush();
    }
    if (!line.empty()) flush();
    return y;
}

void Canvas::button(const Button &b, bool hover) {
    const Col dark{30, 30, 30};
    Col tc = kText;
    switch (b.kind) {
    case Kind::PRIMARY:
        fill(b.x0, b.y0, b.x1, b.y1, hover ? Col{250, 228, 80} : kYellow);
        tc = dark;
        break;
    case Kind::SELECTED:
        fill(b.x0, b.y0, b.x1, b.y1, {205, 205, 210});
        tc = dark;
        break;
    case Kind::NORMAL: fill(b.x0, b.y0, b.x1, b.y1, hover ? kHover : kButton); break;
    case Kind::QUIET:
    case Kind::DANGER: { // outline only
        const bool danger = b.kind == Kind::DANGER;
        const Col hovered = danger ? Col{125, 65, 60} : kHover, edge = danger ? kBad : kButton;
        fill(b.x0, b.y0, b.x1, b.y1, hover ? hovered : edge);
        if (!hover) fill(b.x0 + 3, b.y0 + 3, b.x1 - 3, b.y1 - 3, {40, 40, 43});
        if (danger && !hover) tc = kBad;
        break;
    }
    }
    text(b.label, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2 - 11, 3, tc);
}

std::string write_png(Canvas &c, const std::string &path) {
    for (size_t i = 3; i < c.px.size(); i += 4) c.px[i] = 255;
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    img.width = img.height = kTex;
    img.format = PNG_FORMAT_RGBA;
    if (png_image_write_to_file(&img, path.c_str(), 0, c.px.data(), kTex * 4, nullptr)) return "";
    return img.message[0] ? img.message : "unknown error";
}
