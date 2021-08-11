﻿#include "headers.hpp"
#include <functional>

#include "system/util/settings.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "youtube_parser/parser.hpp"
#include "network/thumbnail_loader.hpp"

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int CONTENT_Y_HIGH = 240;
	
	volatile bool save_settings_request = false;
	volatile bool change_brightness_request = false;
	volatile bool string_resource_reload_request = false;
	
	Thread settings_misc_thread;
};
using namespace Settings;



static void settings_misc_thread_func(void *arg) {
	while (!exiting) {
		if (save_settings_request) {
			save_settings_request = false;
			save_settings();
		} else if (change_brightness_request) {
			change_brightness_request = false;
			Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
		} else if (string_resource_reload_request) {
			string_resource_reload_request = false;
			load_string_resources(var_lang);
		} else usleep(50000);
	}
	
	Util_log_save("settings/save", "Thread exit.");
	threadExit(0);
}

ScrollView *main_view;

bool Sem_query_init_flag(void) {
	return already_init;
}

void Sem_resume(std::string arg)
{
	overlay_menu_on_resume();
	main_view->on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}

void Sem_suspend(void)
{
	thread_suspend = true;
}

void Sem_init(void)
{
	Util_log_save("settings/init", "Initializing...");
	Result_with_string result;
	
	load_settings();
	load_string_resources(var_lang);
	
	settings_misc_thread = threadCreate(settings_misc_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	
	main_view = (new ScrollView(0, MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2, 320, 240 - MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2))
		->set_views({
			// 'Settings'
			(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
				->set_text(LOCALIZED(SETTINGS))
				->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
			// ---------------------
			(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2)),
			
			// UI language
			(new SelectorView(0, 0, 320, 35))
				->set_texts({
					(std::function<std::string ()>) []() { return LOCALIZED(LANG_EN); },
					(std::function<std::string ()>) []() { return LOCALIZED(LANG_JA); }
				}, var_lang == "ja" ? 1 : 0)
				->set_title([](const SelectorView &) { return LOCALIZED(UI_LANGUAGE); })
				->set_on_change([](const SelectorView &view) {
					auto next_lang = std::vector<std::string>{"en", "ja"}[view.selected_button];
					if (var_lang != next_lang) {
						var_lang = next_lang;
						save_settings_request = true;
						string_resource_reload_request = true;
					}
				}),
			// Content language
			(new SelectorView(0, 0, 320, 35))
				->set_texts({
					(std::function<std::string ()>) []() { return LOCALIZED(LANG_EN); },
					(std::function<std::string ()>) []() { return LOCALIZED(LANG_JA); }
				}, var_lang_content == "ja" ? 1 : 0)
				->set_title([](const SelectorView &) { return LOCALIZED(CONTENT_LANGUAGE); })
				->set_on_change([](const SelectorView &view) {
					auto next_lang = std::vector<std::string>{"en", "ja"}[view.selected_button];
					if (var_lang_content != next_lang) {
						var_lang_content = next_lang;
						save_settings_request = true;
						youtube_change_content_language(var_lang_content);
					}
				}),
			// LCD Brightness
			(new BarView(0, 0, 320, 40))
				->set_values(15, 163, var_lcd_brightness)
				->set_title([] (const BarView &view) { return LOCALIZED(LCD_BRIGHTNESS); })
				->set_while_holding([] (const BarView &view) {
					var_lcd_brightness = view.value;
					change_brightness_request = true;
				})
				->set_on_release([] (const BarView &view) { save_settings_request = true; }),
			// Time to turn off LCD
			(new BarView(0, 0, 320, 40))
				->set_values(10, 310, var_time_to_turn_off_lcd <= 309 ? var_time_to_turn_off_lcd : 310)
				->set_title([] (const BarView &view) { return LOCALIZED(TIME_TO_TURN_OFF_LCD) + " : " +
					(view.value <= 309 ? std::to_string((int) view.value) + " " + LOCALIZED(SECONDS) : LOCALIZED(NEVER_TURN_OFF)); })
				->set_on_release([] (const BarView &view) {
					var_time_to_turn_off_lcd = view.value <= 309 ? view.value : 1000000000;
					save_settings_request = true;
				}),
			// Eco mode
			(new SelectorView(0, 0, 320, 35))
				->set_texts({
					(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
					(std::function<std::string ()>) []() { return LOCALIZED(ON); }
				}, var_eco_mode)
				->set_title([](const SelectorView &) { return LOCALIZED(ECO_MODE); })
				->set_on_change([](const SelectorView &view) {
					if (var_eco_mode != view.selected_button) {
						var_eco_mode = view.selected_button;
						save_settings_request = true;
					}
				}),
			// Dark theme (plus flash)
			(new SelectorView(0, 0, 320, 35))
				->set_texts({
					(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
					(std::function<std::string ()>) []() { return LOCALIZED(ON); },
					(std::function<std::string ()>) []() { return LOCALIZED(FLASH); }
				}, var_flash_mode ? 2 : var_night_mode)
				->set_title([](const SelectorView &) { return LOCALIZED(DARK_THEME); })
				->set_on_change([](const SelectorView &view) {
					if (var_flash_mode != (view.selected_button == 2)) {
						var_flash_mode = (view.selected_button == 2);
						save_settings_request = true;
					}
					if (!var_flash_mode && var_night_mode != view.selected_button) {
						var_night_mode = view.selected_button;
						save_settings_request = true;
					}
				}),
			// margin at the end of the list
			(new EmptyView(0, 0, 320, 4))
		});
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	Sem_resume("");
	already_init = true;
}

void Sem_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	u64 time_out = 10000000000;
	Util_log_save("settings", "threadJoin()...", threadJoin(settings_misc_thread, time_out));
	threadFree(settings_misc_thread);
	
	save_settings();
	
	Util_log_save("settings/exit", "Exited.");
}

Intent Sem_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::SETTINGS);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_view->update_y_range(0, CONTENT_Y_HIGH);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		main_view->draw();
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key, &intent, SceneType::SETTINGS);
		
		main_view->update(key);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
