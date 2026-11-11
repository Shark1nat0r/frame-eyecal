// The panel image: an RGBA canvas with filled rectangles, text (the system's Noto Sans through FreeType) and
// buttons.
#pragma once

#include <string>
#include <vector>

inline constexpr int kTex = 1024; // the panel is kTex x kTex pixels

struct Col {
    unsigned char r, g, b;
};
inline constexpr Col kText{225, 225, 225}, kDim{150, 150, 150}, kGood{120, 200, 130}, kWarn{225, 185, 90},
    kBad{225, 120, 110}, kYellow{230, 205, 30}, kButton{70, 70, 74}, kHover{95, 95, 102};

// PRIMARY: the one next step on a screen, in the yellow of the targets. SELECTED: the chosen side of a switch.
// DANGER: a destructive action waiting for its second press.
enum class Kind { NORMAL, PRIMARY, QUIET, SELECTED, DANGER };
struct Button {
    int x0, y0, x1, y1;
    std::string id, label;
    Kind kind;
};

bool font_init(); // false if no font was found

// Text size s: capital letters are 7*s pixels high, lines 13*s apart.
struct Canvas {
    std::vector<unsigned char> px = std::vector<unsigned char>(static_cast<size_t>(kTex) * kTex * 4);
    void fill(int x0, int y0, int x1, int y1, Col c, unsigned char a = 255);
    static int width(const std::string &t, int s);
    void text(const std::string &t, int x, int y, int s, Col c, bool centre = true);
    // word-wrapped, left-aligned from x0 (default: margin) to kTex - margin; returns the next y
    int para(const std::string &t, int y, int s, Col c, int margin = 70, int x0 = -1);
    void button(const Button &b, bool hover);
};

// The canvas as an opaque PNG; returns "" when saved, else why not
std::string write_png(Canvas &c, const std::string &path);
