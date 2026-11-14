#include "status_tile.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::status_tile {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// ---- Readable colours ---------------------------------------------------------------------------------------------

// A colour's luminance and the WCAG contrast of two: amber or yellow words on the light look's white card fall under
// what a person reads, so there they take the ink instead; on the dark look they keep their colour.
static float luminance(lv_color_t c) {
  auto channel = [](uint8_t v) {
    const float s = v / 255.0f;
    return s <= 0.03928f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * channel(c.red) + 0.7152f * channel(c.green) + 0.0722f * channel(c.blue);
}
static bool reads_on_card(lv_color_t c) {
  const float a = luminance(c), b = luminance(ui::color(theme::CARD));
  return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f) >= 3.0f;
}
// A label (words or an icon) in a tone's or a role's colour where that reads on the card, else in the ink.
static void set_readable(lv_obj_t *label, tessera::Tone tone, theme::Role role) {
  if (reads_on_card(ui::tone_color(tone, role))) ui::set_tone(label, tone, role);
  else ui::set_color(label, theme::INK);
}

// ---- Kinds and colours --------------------------------------------------------------------------------------------

// What the entity is: its icons for on and off, its words for on and off, and its colours when the tile leaves them
// on "auto".
enum class Color : uint8_t { AUTO, RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, ACCENT, GREY };

struct Kind {
  const char *id;
  uint32_t icon_on, icon_off;
  const char *text_on, *text_off;  // keys of the screen's texts
  Color on, off;
};

static const Kind KINDS[] = {
    {"window", 0xF05B1, 0xF11DB, "open", "closed", Color::RED, Color::GREEN},           // window-open / -closed-variant
    {"door", 0xF081C, 0xF081B, "open", "closed", Color::RED, Color::GREEN},             // door-open / door-closed
    {"garage", 0xF06DA, 0xF06D9, "open", "closed", Color::RED, Color::GREEN},           // garage-open / garage
    {"gate", 0xF116A, 0xF0299, "open", "closed", Color::RED, Color::GREEN},             // gate-open / gate
    {"blinds", 0xF111E, 0xF111C, "open", "closed", Color::YELLOW, Color::GREY},         // window-shutter-open / -shutter
    {"light", 0xF0335, 0xF0E4F, "on", "off", Color::YELLOW, Color::GREY},               // lightbulb / lightbulb-off
    {"plug", 0xF06A5, 0xF06A6, "on", "off", Color::ACCENT, Color::GREY},                // power-plug / power-plug-off
    {"motion", 0xF0D91, 0xF1435, "motion", "calm", Color::ORANGE, Color::GREY},         // motion-sensor / -off
    {"presence", 0xF02DC, 0xF06A1, "home", "away", Color::GREEN, Color::GREY},          // home / home-outline
    {"lock", 0xF033F, 0xF033E, "unlocked", "locked", Color::RED, Color::GREEN},         // lock-open / lock
    {"leak", 0xF1502, 0xF058D, "wet", "dry", Color::RED, Color::GREEN},                 // water-alert / water-off
    {"smoke", 0xF192E, 0xF0392, "smoke", "clear", Color::RED, Color::GREEN},            // smoke-detector-alert / -detector
    {"appliance", 0xF072A, 0xF072A, "running", "idle", Color::BLUE, Color::GREY},       // washing-machine
    {"alarm", 0xF099D, 0xF099E, "armed", "disarmed", Color::GREEN, Color::GREY},        // shield-lock / shield-off
    {"switch", 0xF0521, 0xF1A26, "on", "off", Color::ACCENT, Color::GREY},              // toggle-switch / -variant-off
    {"temperature", 0xF050F, 0xF050F, "on", "off", Color::GREY, Color::GREY},           // thermometer
    {"humidity", 0xF058E, 0xF058E, "on", "off", Color::GREY, Color::GREY},              // water-percent
    {"trash", 0xF0A79, 0xF0A79, "on", "off", Color::RED, Color::GREY},                  // trash-can-outline
};

static const Kind *kind_by_id(const char *id) {
  for (const Kind &k : KINDS)
    if (strcmp(k.id, id) == 0) return &k;
  return nullptr;
}

// "auto": the kind from Home Assistant's device class, else from the entity's domain.
static const Kind *guess_kind(const std::string &device_class, const std::string &entity) {
  static const struct {
    const char *device_class, *kind;
  } BY_CLASS[] = {{"window", "window"}, {"door", "door"},      {"opening", "door"},     {"garage_door", "garage"},
                  {"garage", "garage"}, {"gate", "gate"},      {"shutter", "blinds"},   {"blind", "blinds"},
                  {"curtain", "blinds"}, {"awning", "blinds"}, {"motion", "motion"},    {"occupancy", "motion"},
                  {"presence", "presence"}, {"moisture", "leak"}, {"smoke", "smoke"},   {"gas", "smoke"},
                  {"carbon_monoxide", "smoke"}, {"lock", "lock"}, {"plug", "plug"},     {"outlet", "plug"},
                  {"light", "light"},   {"running", "appliance"}, {"power", "plug"},     {"safety", "alarm"},
                  {"temperature", "temperature"}, {"humidity", "humidity"}, {"moisture_percent", "humidity"}};
  for (const auto &m : BY_CLASS)
    if (device_class == m.device_class) return kind_by_id(m.kind);
  const std::string domain = entity.substr(0, entity.find('.'));
  if (domain == "light") return kind_by_id("light");
  if (domain == "lock") return kind_by_id("lock");
  if (domain == "cover") return kind_by_id("blinds");
  if (domain == "fan" || domain == "switch" || domain == "input_boolean") return kind_by_id("switch");
  return kind_by_id("switch");
}

static Color color_by_id(const char *id) {
  static const char *const IDS[] = {"auto", "red", "orange", "yellow", "green", "blue", "purple", "accent", "grey"};
  for (int i = 0; i < 9; ++i)
    if (strcmp(IDS[i], id) == 0) return static_cast<Color>(i);
  return Color::AUTO;
}

// A colour as the screen's tone and role: red, orange and green follow the screen's alert, busy and good colours.
static void paint(lv_obj_t *obj, Color c, bool fill) {
  Tone tone = Tone::NORMAL;
  theme::Role role = theme::MUTED;
  switch (c) {
    case Color::RED: tone = Tone::ALERT; break;
    case Color::ORANGE: tone = Tone::BUSY; break;
    case Color::GREEN: tone = Tone::GOOD; break;
    case Color::YELLOW: role = theme::MARK_AMBER; break;
    case Color::BLUE: role = theme::MARK_BLUE; break;
    case Color::PURPLE: role = theme::MARK_PURPLE; break;
    case Color::ACCENT: role = theme::ACCENT; break;
    default: role = fill ? theme::TRACK : theme::MUTED; break;
  }
  if (fill) ui::set_fill(obj, tone, role);
  else set_readable(obj, tone, role);
}

// The states that count as "on": open, on, unlocked, home, wet, running and the like. Everything else is off, or a
// value (a sensor's number) when it is not a state word at all.
static bool is_on(const std::string &s) {
  static const char *const ON[] = {"on", "open", "opening", "unlocked", "unlocking", "home", "playing", "heat",
                                   "detected", "wet", "true", "armed_home", "armed_away", "armed_night",
                                   "armed_vacation", "armed_custom_bypass", "triggered", "jammed"};
  for (const char *o : ON)
    if (s == o) return true;
  return false;
}
static bool is_state_word(const std::string &s) {
  static const char *const OFF[] = {"off", "closed", "closing", "locked", "locking", "not_home", "away", "idle",
                                    "paused", "standby", "dry", "false", "clear", "disarmed", "stopped"};
  if (is_on(s)) return true;
  for (const char *o : OFF)
    if (s == o) return true;
  return false;
}

// ---- Drawing helpers ----------------------------------------------------------------------------------------------

static void place(lv_obj_t *obj, int x, int y, int w, int h) {
  lv_obj_set_size(obj, std::max(1, w), std::max(1, h));
  lv_obj_set_pos(obj, x, y);
}

// Opacities written only when they differ: a write redraws the object.
static void set_bg_opa(lv_obj_t *obj, lv_opa_t opa) {
  if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(obj, opa, 0);
}

// A label inside a circle: a child of it, so the two are one part, centred in it.
static void inside(lv_obj_t *circle, lv_obj_t *label, int w, int h, int dy = 0) {
  lv_obj_set_style_pad_all(circle, 0, 0);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(label, std::max(1, w), std::max(1, h));
  lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
}

// ---- The tile -----------------------------------------------------------------------------------------------------

// One entity: its icon in a circle of its state's colour, its name, and its state in words (or its value with unit).
// Square or tall, the circle above and the words under it; wide, the circle at the left and the words beside it. The
// tile takes the colour lightly while the entity is on, when the tile says so.
class StatusTile : public tessera::Tile {
 public:
  explicit StatusTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    snprintf(entity_, sizeof entity_, "%s", c.entity ? c.entity : "");
    snprintf(tile_name_, sizeof tile_name_, "%s", c.name ? c.name : "");
    const char *kind = c.options["kind"] | "auto";
    snprintf(kind_id_, sizeof kind_id_, "%s", kind);
    on_color_ = color_by_id(c.options["on_color"] | "auto");
    off_color_ = color_by_id(c.options["off_color"] | "auto");
    snprintf(on_text_, sizeof on_text_, "%s", c.options["on_text"] | "");
    snprintf(off_text_, sizeof off_text_, "%s", c.options["off_text"] | "");
    tint_on_ = c.options["tint"] | true;
    show_name_ = c.options["show_name"] | true;
    tap_ = c.options["tap"] | false;

    // The tint covers the whole tile, its padding too, with the tile's own corners. Floating: it never scrolls it.
    const int pl = lv_obj_get_style_pad_left(c.parent, LV_PART_MAIN), pt = lv_obj_get_style_pad_top(c.parent, LV_PART_MAIN);
    const int pr = lv_obj_get_style_pad_right(c.parent, LV_PART_MAIN);
    const int pb = lv_obj_get_style_pad_bottom(c.parent, LV_PART_MAIN);
    tint_ = ui::block(c.parent, theme::CARD);
    lv_obj_add_flag(tint_, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(tint_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(tint_, lv_obj_get_style_radius(c.parent, LV_PART_MAIN), 0);
    place(tint_, -pl, -pt, width_ + pl + pr, height_ + pt + pb);
    lv_obj_set_style_bg_opa(tint_, LV_OPA_TRANSP, 0);

    const int name_h = ui::line_height(Font::BODY);
    state_font_ = Font::TITLE;
    const int state_h = ui::line_height(state_font_);
    const int text_h = (show_name_ ? name_h : 0) + state_h;
    wide_ = width_ >= height_ * 3 / 2;
    if (wide_) {
      dia_ = std::min({ui::mm(14), height_ - ui::px(8), width_ / 3});
    } else {
      dia_ = std::min({ui::mm(16), width_ - ui::px(8), height_ - text_h - ui::px(8)});
    }
    dia_ = std::max(dia_, ui::line_height(Font::ICON_SMALL) + ui::px(8));
    const Font icon_font = dia_ >= ui::line_height(Font::ICON) + ui::px(12) ? Font::ICON : Font::ICON_SMALL;
    const int ih = ui::line_height(icon_font);

    circle_ = ui::block(c.parent, theme::TRACK);
    lv_obj_set_style_radius(circle_, LV_RADIUS_CIRCLE, 0);
    icon_ = ui::label(circle_, icon_font, theme::MUTED);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    state_ = ui::label(c.parent, state_font_, theme::INK);

    if (wide_) {
      const int cy = height_ / 2;
      place(circle_, 0, cy - dia_ / 2, dia_, dia_);
      inside(circle_, icon_, dia_, ih);
      const int x = dia_ + ui::px(12), w = width_ - x, top = cy - text_h / 2;
      place(name_, x, top, w, name_h);
      place(state_, x, top + (show_name_ ? name_h : 0), w, state_h);
    } else {
      const int gap = ui::px(6);
      const int top = std::max(0, (height_ - dia_ - gap - text_h) / 2);
      place(circle_, (width_ - dia_) / 2, top, dia_, dia_);
      inside(circle_, icon_, dia_, ih);
      lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_align(state_, LV_TEXT_ALIGN_CENTER, 0);
      place(name_, 0, top + dia_ + gap, width_, name_h);
      place(state_, 0, top + dia_ + gap + (show_name_ ? name_h : 0), width_, state_h);
    }
    if (!show_name_) ui::set_text(name_, "");
    paint_now();
  }

void on_state(JsonObjectConst data) override {
    const char *raw_state = data["state"] | "";
    have_state_ = !data["state"].isNull();
    snprintf(name_text_, sizeof name_text_, "%s", data["name"] | "");
    JsonObjectConst a = data["attributes"];
    snprintf(device_class_, sizeof device_class_, "%s", a["device_class"] | "");
    snprintf(unit_, sizeof unit_, "%s", a["unit_of_measurement"] | "");

    // Prüfen, ob unser Kombi-Format "on|yellow|Text" im State steckt:
    const char *p1 = strchr(raw_state, '|');
    const char *p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
    if (p1 && p2) {
      size_t len1 = std::min(static_cast<size_t>(p1 - raw_state), sizeof(state_text_) - 1);
      memcpy(state_text_, raw_state, len1);
      state_text_[len1] = '\0';

      size_t len2 = std::min(static_cast<size_t>(p2 - (p1 + 1)), sizeof(custom_color_) - 1);
      memcpy(custom_color_, p1 + 1, len2);
      custom_color_[len2] = '\0';

      snprintf(custom_text_, sizeof custom_text_, "%s", p2 + 1);
    } else {
      snprintf(state_text_, sizeof state_text_, "%s", raw_state);
      snprintf(custom_text_, sizeof custom_text_, "%s", a["volltext"] | "");
      snprintf(custom_color_, sizeof custom_color_, "%s", a["farbe"] | "");
    }

    wrong_ = strcmp(data["wait"] | "", "wrong_entity") == 0;
    paint_now();
  }

  void on_theme() override { paint_now(); }

  void on_tap() override {
    if (tap_ && entity_[0]) tessera::action("homeassistant.toggle", entity_);
  }

 private:
  void paint_now() {
    const char *title = tile_name_[0] ? tile_name_ : name_text_[0] ? name_text_ : plugin_->text("title");
    if (show_name_) {
      ui::set_text(name_, title);
      ui::set_color(name_, theme::MUTED);
    }
    const std::string s = state_text_;
    const Kind *k = nullptr;
    if (strcmp(kind_id_, "auto") == 0 && (custom_text_[0] || custom_color_[0])) {
      k = kind_by_id("trash");
    } else {
      k = strcmp(kind_id_, "auto") == 0 ? guess_kind(device_class_, entity_) : kind_by_id(kind_id_);
    }
    if (!k) k = kind_by_id("switch");

    if (!have_state_ || wrong_ || s == "unavailable" || s == "unknown" || s.empty()) {
      // Nothing to show yet: the kind's off icon, grey, and why.
      set_bg_opa(tint_, LV_OPA_TRANSP);
      ui::set_fill(circle_, Tone::NORMAL, theme::TRACK);
      set_bg_opa(circle_, LV_OPA_60);
      ui::set_text(icon_, ui::icon(k->icon_off));
      ui::set_color(icon_, theme::MUTED);
      ui::set_text(state_, plugin_->text(wrong_ ? "wrong" : have_state_ ? "unavailable" : "choose"));
      ui::set_color(state_, theme::MUTED);
      return;
    }

    const bool word = is_state_word(s);
    const bool on = is_on(s);
    Color color = on ? (on_color_ == Color::AUTO ? k->on : on_color_)
                     : (off_color_ == Color::AUTO ? k->off : off_color_);
    if (on && custom_color_[0]) {
      const Color c = color_by_id(custom_color_);
      if (c != Color::AUTO) color = c;
    }
    const bool colored = color != Color::GREY && word;

    // The circle lightly in the state's colour and the icon fully; grey, a plain circle.
    if (colored) {
      paint(circle_, color, true);
      set_bg_opa(circle_, LV_OPA_20);
      paint(icon_, color, false);
    } else {
      ui::set_fill(circle_, Tone::NORMAL, theme::TRACK);
      set_bg_opa(circle_, LV_OPA_60);
      ui::set_color(icon_, theme::MUTED);
    }
    ui::set_text(icon_, ui::icon(on ? k->icon_on : k->icon_off));

    // The whole tile tinted while it is on, when the tile says so.
    if (tint_on_ && on && colored) {
      paint(tint_, color, true);
      set_bg_opa(tint_, LV_OPA_10);
    } else {
      set_bg_opa(tint_, LV_OPA_TRANSP);
    }

    // The words: the tile's own, else the kind's; a value with its unit when the state is no state word.
    std::string text;
    if (custom_text_[0]) {
      text = custom_text_;
    } else if (!word) {
      text = s;
      if (const char *d = plugin_->text("decimal"); d && *d && *d != '.' && !text.empty() && isdigit(static_cast<unsigned char>(text.back())))
        for (char &ch : text)
          if (ch == '.') ch = *d;
      if (unit_[0]) text += std::string(" ") + unit_;
    } else if (on) {
      text = on_text_[0] ? on_text_ : plugin_->text(k->text_on);
    } else {
      text = off_text_[0] ? off_text_ : plugin_->text(k->text_off);
    }
    // A long word ("Geschlossen") steps down a size on a narrow tile rather than being cut.
    const int room = wide_ ? width_ - dia_ - ui::px(12) : width_;
    ui::set_font(state_, ui::text_width(text.c_str(), Font::TITLE) <= room ? Font::TITLE : Font::BODY_LARGE);
    ui::set_text(state_, text);
    if (colored) paint(state_, color, false);
    else ui::set_color(state_, word ? theme::INK_SOFT : theme::INK);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0, dia_ = 0;
  bool wide_ = false, tint_on_ = true, show_name_ = true, tap_ = false, have_state_ = false, wrong_ = false;
  Color on_color_ = Color::AUTO, off_color_ = Color::AUTO;
  Font state_font_ = Font::TITLE;
  char entity_[96]{}, tile_name_[48]{}, kind_id_[16]{}, on_text_[48]{}, off_text_[48]{};
  char state_text_[64]{}, name_text_[64]{}, device_class_[32]{}, unit_[16]{};
  char custom_text_[64]{}, custom_color_[16]{};
  lv_obj_t *tint_{}, *circle_{}, *icon_{}, *name_{}, *state_{};
};

void StatusTilePlugin::setup() {
  add_tile("status", [this]() { return new StatusTile(this); });
}

}  // namespace esphome::status_tile
