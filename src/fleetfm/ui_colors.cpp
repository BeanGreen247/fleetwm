#include "ui_colors.hpp"

#include <algorithm>

namespace fleetwm::fm {

namespace {
Color hex(unsigned v, double a = 1) { return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0, a}; }
Color mix(Color a, Color b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
double luma(Color c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

// One GTK-ish light base reused by the Linux styles, then tinted by each style's accent and chrome.
Colors light_base() {
  Colors c;
  c.window = hex(0xffffff);
  c.text = hex(0x1a1a1a);
  c.text_dim = hex(0x6b6b6b);
  c.text_off = hex(0xa0a0a0);
  c.accent = hex(0x3584e4);
  c.accent_text = hex(0xffffff);
  c.danger = hex(0xc01c28);
  c.ok = hex(0x26a269);
  c.chrome_top = hex(0xf6f5f4);
  c.chrome_bot = hex(0xe8e6e3);
  c.chrome_border = hex(0xcdc7c2);
  c.nav_bg = hex(0xf2f1f0);
  c.nav_text = hex(0x2e3436);
  c.nav_head = hex(0x77767b);
  c.content = hex(0xffffff);
  c.content_alt = hex(0xf6f6f6);
  c.head_top = hex(0xfafafa);
  c.head_bot = hex(0xeeeeee);
  c.head_sep = hex(0xd8d8d8);
  c.sel_top = hex(0x3584e4);
  c.sel_bot = hex(0x3584e4);
  c.sel_border = hex(0x3584e4);
  c.sel_text = hex(0xffffff);
  c.selx_top = hex(0xc0bfbc);
  c.selx_bot = hex(0xc0bfbc);
  c.selx_border = hex(0xc0bfbc);
  c.hot_top = hex(0xe9f1fb);
  c.hot_bot = hex(0xe9f1fb);
  c.hot_border = hex(0xe9f1fb);
  c.btn_top = hex(0xf8f7f6);
  c.btn_bot = hex(0xe6e4e1);
  c.btn_border = hex(0xc4bfba);
  c.btn_hot_top = hex(0xffffff);
  c.btn_hot_bot = hex(0xeeece9);
  c.btn_down_top = hex(0xd8d5d1);
  c.btn_down_bot = hex(0xe6e4e1);
  c.field = hex(0xffffff);
  c.field_border = hex(0xc4bfba);
  c.tab_on = hex(0xffffff);
  c.tab_off = hex(0xe8e6e3);
  c.tab_border = hex(0xcdc7c2);
  c.status_top = hex(0xf6f5f4);
  c.status_bot = hex(0xeceae7);
  c.bar_top = hex(0x57c5f0);
  c.bar_bot = hex(0x2f8fd0);
  c.bar_border = hex(0x5b8db8);
  c.bar_track = hex(0xe6e9ee);
  c.bar_warn_top = hex(0xf07f7f);
  c.bar_warn_bot = hex(0xc0392b);
  c.menu_bg = hex(0xffffff);
  c.menu_border = hex(0xb8b3ae);
  c.menu_hot = hex(0x3584e4);
  c.shadow = {0, 0, 0, 0.25};
  c.scroll_track = hex(0xf0efee);
  c.scroll_thumb = hex(0xb9b5b0);
  return c;
}

Colors windows7() {
  Colors c = light_base();
  c.text = hex(0x000000);
  c.text_dim = hex(0x6d6d6d);
  c.accent = hex(0x3399ff);
  c.chrome_top = hex(0xf5f8fc);
  c.chrome_bot = hex(0xe0e8f3);
  c.chrome_border = hex(0xa5b6cf);
  c.nav_bg = hex(0xfcfdff);
  c.nav_text = hex(0x1e1e1e);
  c.nav_head = hex(0x1e395b);
  c.head_top = hex(0xffffff);
  c.head_bot = hex(0xf4f7fb);
  c.head_sep = hex(0xd5dfeb);
  c.sel_top = hex(0xdcebfc);
  c.sel_bot = hex(0xc1dbfc);
  c.sel_border = hex(0x7da2ce);
  c.sel_text = hex(0x000000);
  c.selx_top = hex(0xf0f0f0);
  c.selx_bot = hex(0xe0e0e0);
  c.selx_border = hex(0xd9d9d9);
  c.hot_top = hex(0xf2f7fe);
  c.hot_bot = hex(0xdbeafd);
  c.hot_border = hex(0xb8d6fb);
  c.btn_top = hex(0xf6f9fd);
  c.btn_bot = hex(0xdce6f4);
  c.btn_border = hex(0x8ea2bf);
  c.btn_hot_top = hex(0xeaf3fd);
  c.btn_hot_bot = hex(0xc8dcf5);
  c.btn_down_top = hex(0xc2d8f1);
  c.btn_down_bot = hex(0xdce8f7);
  c.field_border = hex(0x8ea2bf);
  c.tab_on = hex(0xffffff);
  c.tab_off = hex(0xe2eaf5);
  c.tab_border = hex(0xa5b6cf);
  c.status_top = hex(0xf2f6fc);
  c.status_bot = hex(0xe3ebf6);
  c.bar_top = hex(0x6fe06f);
  c.bar_bot = hex(0x1eb21e);
  c.bar_border = hex(0x2c8a2c);
  c.bar_track = hex(0xe6e6e6);
  c.menu_hot = hex(0xdcebfc);
  return c;
}

Colors windows10() {
  Colors c = light_base();
  c.accent = hex(0x0078d7);
  c.chrome_top = hex(0xffffff);
  c.chrome_bot = hex(0xf5f6f7);
  c.chrome_border = hex(0xdadbdc);
  c.nav_bg = hex(0xffffff);
  c.nav_head = hex(0x1a1a1a);
  c.head_top = hex(0xffffff);
  c.head_bot = hex(0xffffff);
  c.head_sep = hex(0xe5e5e5);
  c.sel_top = hex(0xcce8ff);
  c.sel_bot = hex(0xcce8ff);
  c.sel_border = hex(0x99d1ff);
  c.sel_text = hex(0x000000);
  c.selx_top = hex(0xd9d9d9);
  c.selx_bot = hex(0xd9d9d9);
  c.selx_border = hex(0xd9d9d9);
  c.hot_top = hex(0xe5f3ff);
  c.hot_bot = hex(0xe5f3ff);
  c.hot_border = hex(0xe5f3ff);
  c.btn_top = hex(0xffffff);
  c.btn_bot = hex(0xffffff);
  c.btn_border = hex(0xdadbdc);
  c.btn_hot_top = hex(0xe5f3ff);
  c.btn_hot_bot = hex(0xe5f3ff);
  c.btn_down_top = hex(0xcce8ff);
  c.btn_down_bot = hex(0xcce8ff);
  c.bar_top = hex(0x26a0da);
  c.bar_bot = hex(0x26a0da);
  c.bar_border = hex(0x26a0da);
  c.menu_hot = hex(0xe5f3ff);
  c.status_top = hex(0xffffff);
  c.status_bot = hex(0xffffff);
  c.tab_off = hex(0xf0f0f0);
  return c;
}

Colors mac() {
  Colors c = light_base();
  c.accent = hex(0x0a60ff);
  c.chrome_top = hex(0xececec);
  c.chrome_bot = hex(0xd9d9d9);
  c.chrome_border = hex(0xbfbfbf);
  c.nav_bg = hex(0xe9e9ee);
  c.nav_head = hex(0x8a8a8f);
  c.sel_top = hex(0x0a60ff);
  c.sel_bot = hex(0x0a60ff);
  c.sel_border = hex(0x0a60ff);
  c.selx_top = hex(0xd0d0d0);
  c.selx_bot = hex(0xd0d0d0);
  c.selx_border = hex(0xd0d0d0);
  c.hot_top = hex(0xe8e8ec);
  c.hot_bot = hex(0xe8e8ec);
  c.hot_border = hex(0xe8e8ec);
  c.btn_top = hex(0xffffff);
  c.btn_bot = hex(0xf6f6f6);
  c.btn_border = hex(0xc6c6c6);
  c.bar_top = hex(0x3a8bff);
  c.bar_bot = hex(0x0a60ff);
  c.bar_border = hex(0x0a60ff);
  c.tab_off = hex(0xdcdcdc);
  c.status_top = hex(0xf2f2f2);
  c.status_bot = hex(0xf2f2f2);
  return c;
}

Colors dark_from(const Colors& base) {
  Colors c = base;
  c.dark = true;
  c.window = hex(0x242424);
  c.text = hex(0xeeeeee);
  c.text_dim = hex(0xa0a0a0);
  c.text_off = hex(0x6a6a6a);
  c.chrome_top = hex(0x3a3a3a);
  c.chrome_bot = hex(0x303030);
  c.chrome_border = hex(0x1c1c1c);
  c.nav_bg = hex(0x2b2b2b);
  c.nav_text = hex(0xe0e0e0);
  c.nav_head = hex(0x9a9a9a);
  c.content = hex(0x1e1e1e);
  c.content_alt = hex(0x252525);
  c.head_top = hex(0x333333);
  c.head_bot = hex(0x2c2c2c);
  c.head_sep = hex(0x444444);
  c.sel_top = mix(base.accent, hex(0x000000), 0.35);
  c.sel_bot = mix(base.accent, hex(0x000000), 0.45);
  c.sel_border = base.accent;
  c.sel_text = hex(0xffffff);
  c.selx_top = hex(0x3c3c3c);
  c.selx_bot = hex(0x3c3c3c);
  c.selx_border = hex(0x4a4a4a);
  c.hot_top = hex(0x333a44);
  c.hot_bot = hex(0x2d333c);
  c.hot_border = hex(0x3d4756);
  c.btn_top = hex(0x474747);
  c.btn_bot = hex(0x3a3a3a);
  c.btn_border = hex(0x1c1c1c);
  c.btn_hot_top = hex(0x535353);
  c.btn_hot_bot = hex(0x444444);
  c.btn_down_top = hex(0x2c2c2c);
  c.btn_down_bot = hex(0x363636);
  c.field = hex(0x1a1a1a);
  c.field_border = hex(0x4a4a4a);
  c.tab_on = hex(0x242424);
  c.tab_off = hex(0x303030);
  c.tab_border = hex(0x1c1c1c);
  c.status_top = hex(0x333333);
  c.status_bot = hex(0x2c2c2c);
  c.bar_track = hex(0x3a3a3a);
  c.menu_bg = hex(0x2d2d2d);
  c.menu_border = hex(0x1c1c1c);
  c.menu_hot = mix(base.accent, hex(0x000000), 0.4);
  c.scroll_track = hex(0x2b2b2b);
  c.scroll_thumb = hex(0x5a5a5a);
  return c;
}

Colors from_theme(Colors base, const kit::Palette& p) {
  Colors c = luma(p.bg_primary) < 0.5 ? dark_from(base) : base;
  const Color border = mix(p.bg_secondary, p.fg_secondary, 0.42);
  const Color soft_border = mix(p.bg_secondary, p.fg_secondary, 0.26);
  const Color raised = mix(p.bg_secondary, p.bg_primary, 0.35);
  const Color raised_hot = mix(raised, p.accent, 0.16);
  const Color accent_text = luma(p.accent) > 0.58 ? p.bg_primary : p.fg_primary;
  c.window = p.bg_primary;
  c.content = mix(p.bg_primary, p.bg_secondary, 0.0);
  c.content_alt = mix(p.bg_primary, p.bg_secondary, 0.5);
  c.nav_bg = p.bg_secondary;
  c.nav_head = p.fg_secondary;
  c.chrome_top = mix(p.bg_secondary, p.bg_primary, 0.5);
  c.chrome_bot = p.bg_secondary;
  c.chrome_border = border;
  c.text = p.fg_primary;
  c.nav_text = p.fg_primary;
  c.text_dim = p.fg_secondary;
  c.accent = p.accent;
  c.accent_text = accent_text;
  c.danger = mix(p.fg_primary, hex(0xc01c28), 0.55);
  c.ok = mix(p.fg_primary, hex(0x26a269), 0.55);
  c.head_top = raised;
  c.head_bot = p.bg_secondary;
  c.head_sep = soft_border;
  c.sel_top = mix(p.bg_primary, p.accent, 0.45);
  c.sel_bot = mix(p.bg_primary, p.accent, 0.55);
  c.sel_border = p.accent;
  c.sel_text = accent_text;
  c.selx_top = mix(p.bg_secondary, p.accent, 0.14);
  c.selx_bot = mix(p.bg_secondary, p.accent, 0.2);
  c.selx_border = mix(p.bg_secondary, p.accent, 0.35);
  c.hot_top = mix(p.bg_secondary, p.accent, 0.15);
  c.hot_bot = mix(p.bg_secondary, p.accent, 0.2);
  c.hot_border = mix(p.bg_secondary, p.accent, 0.35);
  c.btn_top = raised;
  c.btn_bot = p.bg_secondary;
  c.btn_border = border;
  c.btn_hot_top = raised_hot;
  c.btn_hot_bot = mix(p.bg_secondary, p.accent, 0.12);
  c.btn_down_top = mix(p.bg_secondary, p.accent, 0.28);
  c.btn_down_bot = mix(p.bg_secondary, p.accent, 0.18);
  c.menu_bg = p.bg_secondary;
  c.menu_hot = mix(p.bg_secondary, p.accent, 0.5);
  c.field = p.bg_primary;
  c.field_border = border;
  c.tab_on = p.bg_primary;
  c.tab_off = p.bg_secondary;
  c.tab_border = border;
  c.status_top = raised;
  c.status_bot = p.bg_secondary;
  c.bar_top = mix(p.accent, p.fg_primary, 0.18);
  c.bar_bot = p.accent;
  c.bar_border = p.accent;
  c.bar_track = mix(p.bg_primary, p.fg_secondary, 0.18);
  c.bar_warn_top = mix(p.accent, hex(0xf07f7f), 0.55);
  c.bar_warn_bot = mix(p.accent, hex(0xc0392b), 0.55);
  c.scroll_track = p.bg_secondary;
  c.scroll_thumb = mix(p.bg_secondary, p.fg_secondary, 0.6);
  c.dark = luma(p.bg_primary) < 0.5;
  return c;
}
}  // namespace

Colors make_colors(ViewStyle style, ColorScheme scheme, const kit::Palette& theme) {
  Colors base;
  switch (style) {
    case ViewStyle::Windows7: base = windows7(); break;
    case ViewStyle::Windows10: base = windows10(); break;
    case ViewStyle::Mac: base = mac(); break;
    case ViewStyle::Dolphin:
      base = light_base();
      base.accent = hex(0x3daee9);
      base.sel_top = base.sel_bot = base.sel_border = hex(0x3daee9);
      base.menu_hot = hex(0x3daee9);
      base.chrome_top = hex(0xeff0f1);
      base.chrome_bot = hex(0xe3e5e7);
      base.nav_bg = hex(0xeff0f1);
      break;
    case ViewStyle::Nemo:
      base = light_base();
      base.accent = hex(0x6fa8dc);
      base.sel_top = base.sel_bot = base.sel_border = hex(0x6a9fd8);
      base.menu_hot = hex(0x6a9fd8);
      break;
    case ViewStyle::Nautilus:
      base = light_base();
      base.chrome_top = hex(0xebebeb);
      base.chrome_bot = hex(0xe1e1e1);
      base.nav_bg = hex(0xf4f4f4);
      break;
    default: base = light_base(); break;
  }
  switch (scheme) {
    case ColorScheme::Dark: return dark_from(base);
    case ColorScheme::Light: return base;
    case ColorScheme::FollowTheme: return from_theme(base, theme);
    default: return base;
  }
}

}  // namespace fleetwm::fm
