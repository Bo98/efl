#include <Efl_Ui.h>
#include <Efreet.h>

static Efl_Ui_Spotlight_Container *over_container;

#define SCALE 0.5

typedef struct
{
  Eina_Position2D position;
  const char *name;
  const char *icon;
  Efl_Event_Cb cb;
} Icon;

static void _fake_app_cb(void *data, const Efl_Event *ev);

#define P "/usr/share/icons/hicolor/128x128/apps/"

static Icon workspace1[] = {
    { EINA_POSITION2D(0, 0), "Chrome", P"chromium.png", NULL},
    { EINA_POSITION2D(0, 1), "Cups", P"cups.png", NULL},
    { EINA_POSITION2D(2, 0), NULL, NULL, NULL},
};

static Icon workspace2[] = {
    { EINA_POSITION2D(0, 3), "Cadence", P"cadence.png", _fake_app_cb},
    { EINA_POSITION2D(1, 3), "Emacs", P"emacs.png", _fake_app_cb},
    { EINA_POSITION2D(2, 3), "etui", P"etui.png", _fake_app_cb},
    { EINA_POSITION2D(3, 3), "CAD", P"librecad.png", _fake_app_cb},
    { EINA_POSITION2D(4, 3), "Libreoffice", P"libreoffice-base.png", _fake_app_cb},
    { EINA_POSITION2D(0, 4), "Riot", P"riot.png", _fake_app_cb},
    { EINA_POSITION2D(1, 4), "Tex", P"texstudio.png", _fake_app_cb},
    { EINA_POSITION2D(2, 4), "Telegram", P"telegram.png", _fake_app_cb},
    { EINA_POSITION2D(3, 4), "Vlc", P"vlc.png", _fake_app_cb},
    { EINA_POSITION2D(4, 4), "Mono", P"monodevelop.png", _fake_app_cb},
    { EINA_POSITION2D(2, 0), NULL, NULL, NULL},
};

static Icon workspace3[] = {
    { EINA_POSITION2D(0, 0), "bla", "ic", NULL},
    { EINA_POSITION2D(1, 1), "bla", "ic2", NULL},
    { EINA_POSITION2D(0, 2), "bla", "ic", NULL},
    { EINA_POSITION2D(1, 3), "bla", "ic2", NULL},
    { EINA_POSITION2D(0, 4), "bla", "ic", NULL},
    { EINA_POSITION2D(2, 0), "bla", "ic", NULL},
    { EINA_POSITION2D(3, 1), "bla", "ic2", NULL},
    { EINA_POSITION2D(2, 2), "bla", "ic", NULL},
    { EINA_POSITION2D(3, 3), "bla", "ic2", NULL},
    { EINA_POSITION2D(2, 4), "bla", "ic", NULL},
    { EINA_POSITION2D(4, 0), "bla", "ic", NULL},
    { EINA_POSITION2D(4, 2), "bla", "ic", NULL},
    { EINA_POSITION2D(4, 4), "bla", "ic", NULL},
    { EINA_POSITION2D(0, 2), NULL, NULL, NULL},
};

static Icon* workspaces[] = {workspace1, workspace2, workspace3};

static void _home_screen_cb(void *data, const Efl_Event *cb);

static Icon start_line_config[] = {
    { EINA_POSITION2D(0, 0), "Call", "call-start", _fake_app_cb},
    { EINA_POSITION2D(0, 0), "Contact", "contact-new", _fake_app_cb},
    { EINA_POSITION2D(0, 0), "Home", "applications-internet", _home_screen_cb},
    { EINA_POSITION2D(0, 0), "Mail", "emblem-mail", _fake_app_cb},
    { EINA_POSITION2D(0, 0), "Documents", "emblem-documents", _fake_app_cb},
    { EINA_POSITION2D(0, 0), NULL, NULL, NULL},
};

static Eo *compositor;


static Efl_Ui_Widget*
_create_icon(Icon *icon, Eo *parent)
{
   Eo *ret = efl_add(EFL_UI_BUTTON_CLASS, parent, efl_ui_widget_style_set(efl_added, "homescreen_icon"));
   Eo *ic = efl_add(EFL_UI_IMAGE_CLASS, parent);
   efl_ui_image_icon_set(ic, icon->icon);
   efl_content_set(ret, ic);

   efl_text_set(ret, icon->name);
   if (icon->cb)
     efl_event_callback_add(ret, EFL_INPUT_EVENT_CLICKED, icon->cb, icon);

   return ret;
}

static Efl_Ui_Table*
_hs_screen_new(Icon *icons)
{
   Efl_Ui_Table *table;

   table = efl_add(EFL_UI_TABLE_CLASS, over_container);

   for (int y = 0; y < 5; ++y)
     {
        for (int x = 0; x < 5; ++x)
          {
             Eo *obj = efl_add(EFL_CANVAS_RECTANGLE_CLASS, table, efl_gfx_color_set(efl_added, 0, 0, 0, 0));
             efl_pack_table(table, obj, x, y, 1, 1);
          }
     }

   for (int i = 0; icons[i].name; ++i)
     {
        Eo *icon = _create_icon(&icons[i], table);
        efl_pack_table(table, icon, icons[i].position.x, icons[i].position.y, 1, 1);
     }

   return table;
}

static Efl_Ui_Widget*
_build_homescreen(Efl_Ui_Win *win)
{
   Efl_Ui_Spotlight_Indicator *indicator = efl_new(EFL_UI_SPOTLIGHT_ICON_INDICATOR_CLASS);
   Efl_Ui_Spotlight_Manager *scroll = efl_new(EFL_UI_SPOTLIGHT_SCROLL_MANAGER_CLASS);

   over_container = efl_add(EFL_UI_SPOTLIGHT_CONTAINER_CLASS, win,
      efl_ui_spotlight_manager_set(efl_added, scroll),
      efl_ui_spotlight_indicator_set(efl_added, indicator)
   );

   for (int i = 0; i < 3; ++i)
   {
      Eo *screen = _hs_screen_new(workspaces[i]);

      efl_pack_end(over_container, screen);
   }
   return over_container;
}

static Efl_Ui_Widget*
_build_overall_structure(Efl_Ui_Win *win, Efl_Ui_Widget *homescreen)
{
   Efl_Ui_Widget *o, *start_line;

   o = efl_add(EFL_UI_BOX_CLASS, win);
   efl_pack_end(o, homescreen);

   //start line
   start_line = efl_add(EFL_UI_BOX_CLASS, win);
   efl_gfx_hint_weight_set(start_line, 1.0, 0.0);
   efl_ui_layout_orientation_set(start_line, EFL_UI_LAYOUT_ORIENTATION_HORIZONTAL);
   efl_gfx_hint_size_min_set(start_line, EINA_SIZE2D(5*150*SCALE, 150*SCALE));
   efl_gfx_hint_size_max_set(start_line, EINA_SIZE2D(-1, 150*SCALE));
   efl_pack_end(o, start_line);

   for (int i = 0; i < 5; ++i)
     {
        efl_pack_end(start_line, _create_icon(&start_line_config[i], start_line));
     }

   return o;
}

static Efl_Ui_Widget*
_build_compositor(Efl_Ui_Win *win)
{
   Efl_Ui_Widget *comp;
   Efl_Ui_Spotlight_Manager *stack;

   stack = efl_new(EFL_UI_SPOTLIGHT_FADE_MANAGER_CLASS);
   comp = efl_add(EFL_UI_SPOTLIGHT_CONTAINER_CLASS, win,
                  efl_ui_spotlight_manager_set(efl_added, stack));

   return comp;
}

static void
_home_screen_cb(void *data, const Efl_Event *cb)
{
   Efl_Canvas_Rectangle *rect;

   rect = efl_add(EFL_CANVAS_RECTANGLE_CLASS, compositor);
   efl_ui_spotlight_push(compositor, rect);
}

static void
_fake_app_cb(void *data, const Efl_Event *ev)
{
    Efl_Ui_Table *table = efl_add(EFL_UI_TABLE_CLASS, compositor);
    Efl_Canvas_Rectangle *rect = efl_add(EFL_CANVAS_RECTANGLE_CLASS, compositor);
    efl_gfx_color_set(rect, 50, 50, 50, 255);
    efl_pack_table(table, rect, 0, 0, 1, 1);
    Efl_Ui_Textbox *text = efl_add(EFL_UI_TEXTBOX_CLASS , table);
    efl_text_interactive_editable_set(text, EINA_FALSE);
    efl_text_multiline_set(text, EINA_FALSE);
    efl_text_set(text, "This is a testing application");
    efl_pack_table(table, text, 0, 0, 1, 1);
    efl_ui_spotlight_push(compositor, table);
}

EAPI_MAIN void
efl_main(void *data EINA_UNUSED, const Efl_Event *ev EINA_UNUSED)
{
   Eo *win, *over_container, *desktop, *background;

   efl_ui_theme_extension_add(efl_ui_theme_default_get(), "/home/marcel/git/efl/build/src/examples/elementary/homescreen/button_theme.edj");

   win = efl_new(EFL_UI_WIN_CLASS,
             efl_ui_win_autodel_set(efl_added, EINA_TRUE));
   efl_gfx_entity_size_set(win, EINA_SIZE2D(720*SCALE+15, 1280*SCALE));

   over_container = _build_homescreen(win);
   desktop = _build_overall_structure(win, over_container);

   compositor = _build_compositor(win);
   //efl_pack_end(compositor, desktop);
   efl_ui_spotlight_push(compositor, desktop);
   efl_gfx_entity_size_set(compositor, EINA_SIZE2D(720*SCALE, 1280*SCALE));

   background = efl_add(EFL_UI_LAYOUT_CLASS, win);
   efl_file_simple_load(background, "/home/marcel/git/efl/src/examples/elementary/homescreen/Lantern_Cluster.edj", "e/desktop/background");
   efl_content_set(win, background);
}
EFL_MAIN()
