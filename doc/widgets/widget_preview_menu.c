#include "widget_preview_tmpl_head.c"

Evas_Object *o = elm_menu_add(win);
evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
elm_win_resize_object_add(win, o);
evas_object_show(o);

elm_menu_item_add(o, NULL, "file", "item", NULL, NULL);
elm_menu_item_add(o, NULL, NULL, "item", NULL, NULL);

#include "widget_preview_tmpl_foot.c"
