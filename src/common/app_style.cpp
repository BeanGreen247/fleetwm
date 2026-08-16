#include "app_style.hpp"

#include <gtk/gtk.h>

#include <fstream>
#include <sstream>
#include <string>

namespace fleetwm {

void apply_app_style(const ThemeConfig& theme_config) {
  // Two separate providers, both at APPLICATION priority (later-added
  // provider at the same priority wins on conflicting rules, per GTK's
  // own provider-ordering docs):
  //   1. The active theme file loaded via load_from_path() -- its own
  //      "@import url(base.css)" resolves correctly this way (relative
  //      to the importing file's real path), unlike load_from_string()
  //      which has no file context to resolve a relative @import
  //      against.
  //   2. A small inline provider for corner-style + the explicit-accent
  //      override, which have no file of their own and don't need one.
  static GtkCssProvider* theme_provider = nullptr;
  static GtkCssProvider* overrides_provider = nullptr;
  if (!theme_provider) {
    theme_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(theme_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    overrides_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(overrides_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }

  std::string dir = themes_dir();
  std::string theme_path = dir + "/" + theme_css_filename(theme_config.theme);
  gtk_css_provider_load_from_path(theme_provider, theme_path.c_str());

  std::string corners_path =
      dir + "/corners-" + (theme_config.corner_style == CornerStyle::Sharp ? "sharp" : "rounded") +
      ".css";
  std::string overrides_css;
  {
    // corners-*.css has no @import of its own, so string-loading it
    // alongside the accent override (rather than a third provider) is
    // fine here -- read it back in as text.
    std::ifstream in(corners_path);
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      overrides_css = ss.str();
    }
  }

  if (!theme_config.accent.auto_extract) {
    // Must be added after the theme provider so it actually wins --
    // GTK CSS resolves @define-color references at the point of use,
    // and a later same-priority provider's rules take precedence over
    // an earlier one's per GTK's own cascade-ordering rules.
    overrides_css += "@define-color accent_color " + theme_config.accent.hex + ";";
  }

  gtk_css_provider_load_from_string(overrides_provider, overrides_css.c_str());
}

}  // namespace fleetwm
