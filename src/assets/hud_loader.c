#include "assets/hud_loader.h"

#include "assets/json.h"

#include "core/log.h"
#include "core/path_safety.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hud_asset_defaults(HudAsset* out) {
	memset(out, 0, sizeof(*out));
	out->version = 1;
	out->bar.height_mode = HUD_HEIGHT_CLASSIC;
	out->bar.padding_px = 8;
	out->bar.gap_px = 6;
	out->bar.background.mode = HUD_BACKGROUND_COLOR;
	out->bar.background.color_abgr = 0xFF202020u;
	out->bar.background.image[0] = '\0';
	out->bar.bevel.enabled = true;
	out->bar.bevel.hi_abgr = 0xFF404040u;
	out->bar.bevel.lo_abgr = 0xFF101010u;
	out->bar.bevel.thickness_px = 2;

	out->panel.background.mode = HUD_BACKGROUND_COLOR;
	out->panel.background.color_abgr = 0xFF282828u;
	out->panel.background.image[0] = '\0';
	out->panel.bevel.enabled = true;
	out->panel.bevel.hi_abgr = 0xFF404040u;
	out->panel.bevel.lo_abgr = 0xFF101010u;
	out->panel.bevel.thickness_px = 2;
	out->panel.shadow.enabled = true;
	out->panel.shadow.offset_x = 1;
	out->panel.shadow.offset_y = 1;
	out->panel.shadow.color_abgr = 0x80000000u;
	out->panel.text.color_abgr = 0xFFFFFFFFu;
	out->panel.text.accent_color_abgr = 0xFFFFE0A0u;
	out->panel.text.padding_x = 6;
	out->panel.text.padding_y = 6;
	out->panel.text.fit.min_scale = 0.65f;
	out->panel.text.fit.max_scale = 1.0f;
	out->panel.text.font_file[0] = '\0';

	out->widget_count = 0;
}

static void copy_sv_to_buf(char* dst, size_t dst_size, StringView sv) {
	if (!dst || dst_size == 0) {
		return;
	}
	size_t n = sv.len;
	if (n >= dst_size) {
		n = dst_size - 1;
	}
	memcpy(dst, sv.data, n);
	dst[n] = '\0';
}

static bool sv_eq_lit(StringView sv, const char* lit) {
	if (!lit) {
		return false;
	}
	size_t n = strlen(lit);
	return sv.len == n && strncmp(sv.data, lit, n) == 0;
}

static bool parse_u32_abgr(const JsonDoc* doc, int tok, uint32_t* out) {
	if (!doc || !out || tok < 0 || tok >= doc->token_count) {
		return false;
	}
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	if (!(d >= 0.0 && d <= 4294967295.0)) {
		return false;
	}
	// JSON numbers here are expected to be integer-valued.
	double i = (double)(uint64_t)d;
	if (i != d) {
		return false;
	}
	*out = (uint32_t)(uint64_t)d;
	return true;
}

static bool parse_int_range(const JsonDoc* doc, int tok, int lo, int hi, int* out) {
	int v = 0;
	if (!json_get_int(doc, tok, &v)) {
		return false;
	}
	if (v < lo || v > hi) {
		return false;
	}
	*out = v;
	return true;
}

static bool parse_float_range(const JsonDoc* doc, int tok, float lo, float hi, float* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	if (!(d >= (double)lo && d <= (double)hi)) {
		return false;
	}
	*out = (float)d;
	return true;
}

static bool filename_no_seps(StringView sv) {
	if (sv.len == 0) {
		return false;
	}
	for (size_t i = 0; i < sv.len; i++) {
		char c = sv.data[i];
		if (c == '/' || c == '\\') {
			return false;
		}
	}
	return true;
}

static void warn_unknown_keys(const JsonDoc* doc, int obj_tok, const char* const* allowed, int allowed_count, const char* prefix) {
	if (!json_token_is_object(doc, obj_tok)) {
		return;
	}
	jsmntok_t* obj = &doc->tokens[obj_tok];
	int pair_count = obj->size / 2;
	int i = obj_tok + 1;
	for (int pair = 0; pair < pair_count; pair++) {
		if (i < 0 || i + 1 >= doc->token_count) {
			break;
		}
		int key_tok = i;
		int val_tok = i + 1;
		(void)val_tok;
		StringView k = json_token_sv(doc, key_tok);
		bool known = false;
		for (int a = 0; a < allowed_count; a++) {
			const char* ak = allowed[a];
			size_t akn = strlen(ak);
			if (k.len == akn && strncmp(k.data, ak, akn) == 0) {
				known = true;
				break;
			}
		}
		if (!known) {
			char key_buf[64];
			copy_sv_to_buf(key_buf, sizeof(key_buf), k);
			if (prefix && prefix[0]) {
				log_warn("HUD: unknown key '%s.%s'", prefix, key_buf);
			} else {
				log_warn("HUD: unknown key '%s'", key_buf);
			}
		}

		// Advance to next key.
		jsmntok_t* t = &doc->tokens[val_tok];
		if (t->type == JSMN_STRING || t->type == JSMN_PRIMITIVE) {
			i = val_tok + 1;
		} else {
			int j = val_tok + 1;
			for (int n = 0; n < t->size; n++) {
				// Recurse-advance without recursion (same logic as config.c's tok_next_local).
				jsmntok_t* ct = &doc->tokens[j];
				if (ct->type == JSMN_STRING || ct->type == JSMN_PRIMITIVE) {
					j++;
				} else {
					int jj = j + 1;
					for (int cn = 0; cn < ct->size; cn++) {
						// nested
						jj = jj + 1;
					}
					j = jj;
				}
			}
			i = j;
		}
	}
}

static bool parse_background(const JsonDoc* doc, int tok_bg, HudBackground* out, const char* pfx) {
	if (!json_token_is_object(doc, tok_bg) || !out) {
		log_error("HUD: %s.background must be an object", pfx);
		return false;
	}
	static const char* const allowed[] = {"mode", "color_abgr", "image"};
	warn_unknown_keys(doc, tok_bg, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), pfx);

	int t_mode = -1;
	if (json_object_get(doc, tok_bg, "mode", &t_mode)) {
		StringView sv;
		if (!json_get_string(doc, t_mode, &sv)) {
			log_error("HUD: %s.background.mode must be a string", pfx);
			return false;
		}
		if (sv_eq_lit(sv, "color")) {
			out->mode = HUD_BACKGROUND_COLOR;
		} else if (sv_eq_lit(sv, "image")) {
			out->mode = HUD_BACKGROUND_IMAGE;
		} else {
			log_error("HUD: %s.background.mode must be 'color' or 'image'", pfx);
			return false;
		}
	}

	int t_col = -1;
	if (json_object_get(doc, tok_bg, "color_abgr", &t_col)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_col, &c)) {
			log_error("HUD: %s.background.color_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->color_abgr = c;
	}

	int t_img = -1;
	if (json_object_get(doc, tok_bg, "image", &t_img)) {
		StringView sv;
		if (!json_get_string(doc, t_img, &sv)) {
			log_error("HUD: %s.background.image must be a string", pfx);
			return false;
		}
		copy_sv_to_buf(out->image, sizeof(out->image), sv);
	}

	if (out->mode == HUD_BACKGROUND_IMAGE) {
		if (out->image[0] == '\0') {
			log_error("HUD: %s.background.image must be non-empty when mode='image'", pfx);
			return false;
		}
		if (!name_is_safe_relpath(out->image)) {
			log_error("HUD: %s.background.image must be a safe relative path: %s", pfx, out->image);
			return false;
		}
	}
	return true;
}

static bool parse_bevel(const JsonDoc* doc, int tok, HudBevel* out, const char* pfx) {
	if (!json_token_is_object(doc, tok) || !out) {
		log_error("HUD: %s.bevel must be an object", pfx);
		return false;
	}
	static const char* const allowed[] = {"enabled", "hi_abgr", "lo_abgr", "thickness_px"};
	warn_unknown_keys(doc, tok, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), pfx);

	int t_en = -1;
	if (json_object_get(doc, tok, "enabled", &t_en)) {
		StringView sv = json_token_sv(doc, t_en);
		if (sv_eq_lit(sv, "true")) {
			out->enabled = true;
		} else if (sv_eq_lit(sv, "false")) {
			out->enabled = false;
		} else {
			int v = 0;
			if (!json_get_int(doc, t_en, &v) || (v != 0 && v != 1)) {
				log_error("HUD: %s.bevel.enabled must be bool", pfx);
				return false;
			}
			out->enabled = (v != 0);
		}
	}

	int t_hi = -1;
	if (json_object_get(doc, tok, "hi_abgr", &t_hi)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_hi, &c)) {
			log_error("HUD: %s.bevel.hi_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->hi_abgr = c;
	}
	int t_lo = -1;
	if (json_object_get(doc, tok, "lo_abgr", &t_lo)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_lo, &c)) {
			log_error("HUD: %s.bevel.lo_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->lo_abgr = c;
	}

	int t_th = -1;
	if (json_object_get(doc, tok, "thickness_px", &t_th)) {
		int v = 0;
		if (!parse_int_range(doc, t_th, 0, 8, &v)) {
			log_error("HUD: %s.bevel.thickness_px must be int in [0..8]", pfx);
			return false;
		}
		out->thickness_px = v;
	}
	return true;
}

static bool parse_shadow(const JsonDoc* doc, int tok, HudShadow* out, const char* pfx) {
	if (!json_token_is_object(doc, tok) || !out) {
		log_error("HUD: %s.shadow must be an object", pfx);
		return false;
	}
	static const char* const allowed[] = {"enabled", "offset_x", "offset_y", "color_abgr"};
	warn_unknown_keys(doc, tok, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), pfx);

	int t_en = -1;
	if (json_object_get(doc, tok, "enabled", &t_en)) {
		StringView sv = json_token_sv(doc, t_en);
		if (sv_eq_lit(sv, "true")) {
			out->enabled = true;
		} else if (sv_eq_lit(sv, "false")) {
			out->enabled = false;
		} else {
			int v = 0;
			if (!json_get_int(doc, t_en, &v) || (v != 0 && v != 1)) {
				log_error("HUD: %s.shadow.enabled must be bool", pfx);
				return false;
			}
			out->enabled = (v != 0);
		}
	}

	int t_x = -1;
	if (json_object_get(doc, tok, "offset_x", &t_x)) {
		int v = 0;
		if (!parse_int_range(doc, t_x, -32, 32, &v)) {
			log_error("HUD: %s.shadow.offset_x must be int in [-32..32]", pfx);
			return false;
		}
		out->offset_x = v;
	}
	int t_y = -1;
	if (json_object_get(doc, tok, "offset_y", &t_y)) {
		int v = 0;
		if (!parse_int_range(doc, t_y, -32, 32, &v)) {
			log_error("HUD: %s.shadow.offset_y must be int in [-32..32]", pfx);
			return false;
		}
		out->offset_y = v;
	}
	int t_c = -1;
	if (json_object_get(doc, tok, "color_abgr", &t_c)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_c, &c)) {
			log_error("HUD: %s.shadow.color_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->color_abgr = c;
	}
	return true;
}

static bool parse_text(const JsonDoc* doc, int tok, HudTextStyle* out, const char* pfx) {
	if (!json_token_is_object(doc, tok) || !out) {
		log_error("HUD: %s.text must be an object", pfx);
		return false;
	}
	static const char* const allowed[] = {"color_abgr", "accent_color_abgr", "padding_x", "padding_y", "fit", "font_file"};
	warn_unknown_keys(doc, tok, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), pfx);

	int t_col = -1;
	if (json_object_get(doc, tok, "color_abgr", &t_col)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_col, &c)) {
			log_error("HUD: %s.text.color_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->color_abgr = c;
	}

	int t_acc = -1;
	if (json_object_get(doc, tok, "accent_color_abgr", &t_acc)) {
		uint32_t c = 0;
		if (!parse_u32_abgr(doc, t_acc, &c)) {
			log_error("HUD: %s.text.accent_color_abgr must be uint32 ABGR as JSON number", pfx);
			return false;
		}
		out->accent_color_abgr = c;
	}

	int t_px = -1;
	if (json_object_get(doc, tok, "padding_x", &t_px)) {
		int v = 0;
		if (!parse_int_range(doc, t_px, 0, 64, &v)) {
			log_error("HUD: %s.text.padding_x must be int in [0..64]", pfx);
			return false;
		}
		out->padding_x = v;
	}
	int t_py = -1;
	if (json_object_get(doc, tok, "padding_y", &t_py)) {
		int v = 0;
		if (!parse_int_range(doc, t_py, 0, 64, &v)) {
			log_error("HUD: %s.text.padding_y must be int in [0..64]", pfx);
			return false;
		}
		out->padding_y = v;
	}

	int t_ff = -1;
	if (json_object_get(doc, tok, "font_file", &t_ff)) {
		StringView sv;
		if (!json_get_string(doc, t_ff, &sv)) {
			log_error("HUD: %s.text.font_file must be a string", pfx);
			return false;
		}
		if (sv.len > 0) {
			if (!filename_no_seps(sv)) {
				char tmp[64];
				copy_sv_to_buf(tmp, sizeof(tmp), sv);
				log_error("HUD: %s.text.font_file must be a filename under Assets/Fonts/ (no path separators): %s", pfx, tmp);
				return false;
			}
			copy_sv_to_buf(out->font_file, sizeof(out->font_file), sv);
		}
	}

	int t_fit = -1;
	if (json_object_get(doc, tok, "fit", &t_fit)) {
		if (!json_token_is_object(doc, t_fit)) {
			log_error("HUD: %s.text.fit must be an object", pfx);
			return false;
		}
		static const char* const allowed_fit[] = {"min_scale", "max_scale"};
		warn_unknown_keys(doc, t_fit, allowed_fit, (int)(sizeof(allowed_fit) / sizeof(allowed_fit[0])), "widgets.panel.text.fit");
		int t_min = -1;
		if (json_object_get(doc, t_fit, "min_scale", &t_min)) {
			float v = 0.0f;
			if (!parse_float_range(doc, t_min, 0.1f, 1.0f, &v)) {
				log_error("HUD: %s.text.fit.min_scale must be number in [0.1..1.0]", pfx);
				return false;
			}
			out->fit.min_scale = v;
		}
		int t_max = -1;
		if (json_object_get(doc, t_fit, "max_scale", &t_max)) {
			float v = 0.0f;
			if (!parse_float_range(doc, t_max, 0.1f, 2.0f, &v)) {
				log_error("HUD: %s.text.fit.max_scale must be number in [0.1..2.0]", pfx);
				return false;
			}
			out->fit.max_scale = v;
		}
		if (!(out->fit.min_scale > 0.0f && out->fit.max_scale >= out->fit.min_scale)) {
			log_error("HUD: %s.text.fit requires max_scale >= min_scale", pfx);
			return false;
		}
	}
	return true;
}

static bool parse_panel_style(const JsonDoc* doc, int tok, HudPanelStyle* out) {
	if (!json_token_is_object(doc, tok) || !out) {
		log_error("HUD: widgets.panel must be an object");
		return false;
	}
	static const char* const allowed[] = {"background", "bevel", "shadow", "text"};
	warn_unknown_keys(doc, tok, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), "widgets.panel");

	int t_bg = -1;
	if (json_object_get(doc, tok, "background", &t_bg)) {
		if (!parse_background(doc, t_bg, &out->background, "widgets.panel")) {
			return false;
		}
	}
	int t_bev = -1;
	if (json_object_get(doc, tok, "bevel", &t_bev)) {
		if (!parse_bevel(doc, t_bev, &out->bevel, "widgets.panel")) {
			return false;
		}
	}
	int t_sh = -1;
	if (json_object_get(doc, tok, "shadow", &t_sh)) {
		if (!parse_shadow(doc, t_sh, &out->shadow, "widgets.panel")) {
			return false;
		}
	}
	int t_tx = -1;
	if (json_object_get(doc, tok, "text", &t_tx)) {
		if (!parse_text(doc, t_tx, &out->text, "widgets.panel")) {
			return false;
		}
	}
	return true;
}

static bool parse_bar(const JsonDoc* doc, int tok, HudBarConfig* out) {
	if (!json_token_is_object(doc, tok) || !out) {
		log_error("HUD: bar must be an object");
		return false;
	}
	static const char* const allowed[] = {"height_mode", "padding_px", "gap_px", "background", "bevel"};
	warn_unknown_keys(doc, tok, allowed, (int)(sizeof(allowed) / sizeof(allowed[0])), "bar");

	int t_hm = -1;
	if (json_object_get(doc, tok, "height_mode", &t_hm)) {
		StringView sv;
		if (!json_get_string(doc, t_hm, &sv)) {
			log_error("HUD: bar.height_mode must be a string");
			return false;
		}
		if (sv_eq_lit(sv, "classic")) {
			out->height_mode = HUD_HEIGHT_CLASSIC;
		} else {
			log_error("HUD: bar.height_mode must be 'classic'");
			return false;
		}
	}

	int t_pad = -1;
	if (json_object_get(doc, tok, "padding_px", &t_pad)) {
		int v = 0;
		if (!parse_int_range(doc, t_pad, 0, 64, &v)) {
			log_error("HUD: bar.padding_px must be int in [0..64]");
			return false;
		}
		out->padding_px = v;
	}
	int t_gap = -1;
	if (json_object_get(doc, tok, "gap_px", &t_gap)) {
		int v = 0;
		if (!parse_int_range(doc, t_gap, 0, 64, &v)) {
			log_error("HUD: bar.gap_px must be int in [0..64]");
			return false;
		}
		out->gap_px = v;
	}

	int t_bg = -1;
	if (json_object_get(doc, tok, "background", &t_bg)) {
		if (!parse_background(doc, t_bg, &out->background, "bar")) {
			return false;
		}
	}
	int t_bev = -1;
	if (json_object_get(doc, tok, "bevel", &t_bev)) {
		if (!parse_bevel(doc, t_bev, &out->bevel, "bar")) {
			return false;
		}
	}
	return true;
}

static bool widget_kind_from_sv(StringView sv, HudWidgetKind* out) {
	if (!out) {
		return false;
	}
	if (sv_eq_lit(sv, "health")) {
		*out = HUD_WIDGET_HEALTH;
		return true;
	}
	if (sv_eq_lit(sv, "mortum")) {
		*out = HUD_WIDGET_MORTUM;
		return true;
	}
	if (sv_eq_lit(sv, "ammo")) {
		*out = HUD_WIDGET_AMMO;
		return true;
	}
	if (sv_eq_lit(sv, "equipped_weapon")) {
		*out = HUD_WIDGET_EQUIPPED_WEAPON;
		return true;
	}
	if (sv_eq_lit(sv, "keys")) {
		*out = HUD_WIDGET_KEYS;
		return true;
	}
	return false;
}

bool hud_asset_load(HudAsset* out, const AssetPaths* paths, const char* filename) {
	if (!out || !paths || !filename || filename[0] == '\0') {
		log_error("HUD: invalid args to hud_asset_load");
		return false;
	}

	hud_asset_defaults(out);

	char* full = asset_path_join(paths, "HUD", filename);
	if (!full) {
		log_error("HUD: out of memory building path for %s", filename);
		return false;
	}

	JsonDoc doc;
	if (!json_doc_load_file(&doc, full)) {
		free(full);
		return false;
	}
	free(full);

	bool ok = true;
	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("HUD: root must be an object");
		ok = false;
	}

	if (ok) {
		static const char* const allowed_root[] = {"version", "bar", "widgets"};
		warn_unknown_keys(&doc, 0, allowed_root, (int)(sizeof(allowed_root) / sizeof(allowed_root[0])), "");

		int t_ver = -1;
		if (!json_object_get(&doc, 0, "version", &t_ver)) {
			log_error("HUD: missing required field: version");
			ok = false;
		} else {
			int v = 0;
			if (!json_get_int(&doc, t_ver, &v) || v != 1) {
				log_error("HUD: version must be 1");
				ok = false;
			} else {
				out->version = v;
			}
		}

		int t_bar = -1;
		if (json_object_get(&doc, 0, "bar", &t_bar)) {
			ok = ok && parse_bar(&doc, t_bar, &out->bar);
		} else {
			log_error("HUD: missing required field: bar");
			ok = false;
		}

		int t_widgets = -1;
		if (!json_object_get(&doc, 0, "widgets", &t_widgets)) {
			log_error("HUD: missing required field: widgets");
			ok = false;
		} else if (!json_token_is_object(&doc, t_widgets)) {
			log_error("HUD: widgets must be an object");
			ok = false;
		} else {
			static const char* const allowed_widgets[] = {"panel", "order"};
			warn_unknown_keys(&doc, t_widgets, allowed_widgets, (int)(sizeof(allowed_widgets) / sizeof(allowed_widgets[0])), "widgets");

			int t_panel = -1;
			if (!json_object_get(&doc, t_widgets, "panel", &t_panel)) {
				log_error("HUD: widgets.panel missing");
				ok = false;
			} else {
				ok = ok && parse_panel_style(&doc, t_panel, &out->panel);
			}

			int t_order = -1;
			if (!json_object_get(&doc, t_widgets, "order", &t_order)) {
				log_error("HUD: widgets.order missing");
				ok = false;
			} else if (!json_token_is_array(&doc, t_order)) {
				log_error("HUD: widgets.order must be an array");
				ok = false;
			} else {
				int n = json_array_size(&doc, t_order);
				out->widget_count = 0;
				for (int i = 0; i < n; i++) {
					int t_item = json_array_nth(&doc, t_order, i);
					if (!json_token_is_object(&doc, t_item)) {
						log_error("HUD: widgets.order[%d] must be an object", i);
						ok = false;
						break;
					}
					static const char* const allowed_item[] = {"kind"};
					warn_unknown_keys(&doc, t_item, allowed_item, (int)(sizeof(allowed_item) / sizeof(allowed_item[0])), "widgets.order[]");

					int t_kind = -1;
					if (!json_object_get(&doc, t_item, "kind", &t_kind)) {
						log_error("HUD: widgets.order[%d].kind missing", i);
						ok = false;
						break;
					}
					StringView sv;
					if (!json_get_string(&doc, t_kind, &sv)) {
						log_error("HUD: widgets.order[%d].kind must be a string", i);
						ok = false;
						break;
					}
					HudWidgetKind kind;
					if (!widget_kind_from_sv(sv, &kind)) {
						char tmp[64];
						copy_sv_to_buf(tmp, sizeof(tmp), sv);
						log_error("HUD: unknown widget kind: %s", tmp);
						ok = false;
						break;
					}
					if (out->widget_count >= HUD_MAX_WIDGETS) {
						log_warn("HUD: widgets.order has >%d entries; ignoring extras", HUD_MAX_WIDGETS);
						break;
					}
					out->widgets[out->widget_count++] = (HudWidgetSpec){.kind = kind};
				}

				if (ok && out->widget_count <= 0) {
					log_error("HUD: widgets.order must contain at least one widget");
					ok = false;
				}
			}
		}
	}

	json_doc_destroy(&doc);
	return ok;
}
