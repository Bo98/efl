//Compile with:
//gcc -o notify_example_01 notify_example_01.c -g `pkg-config --cflags --libs elementary`

#include <Elementary.h>

static void _hide(void *data, Evas_Object *obj, void *event_info);

EAPI_MAIN int
elm_main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   Evas_Object *win, *bx, *notify, *content;

   win = elm_win_util_standard_add("notify", "Notify");
   elm_win_autodel_set(win, EINA_TRUE);
   elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

   bx = elm_box_add(win);
   elm_box_horizontal_set(bx, EINA_FALSE);
   evas_object_size_hint_weight_set(bx, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, bx);
   evas_object_show(bx);

   content = elm_label_add(win);
   elm_object_text_set(content, "default");
   evas_object_show(content);

   notify = elm_notify_add(win);
   elm_notify_allow_events_set(notify, EINA_FALSE);
   elm_notify_timeout_set(notify, 5);
   elm_object_content_set(notify, content);
   evas_object_size_hint_weight_set(notify, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_show(notify);
   elm_box_pack_end(bx, notify);
   evas_object_smart_callback_add(notify, "block,clicked", _hide, NULL);

   content = elm_label_add(win);
   elm_object_text_set(content, "bottom");
   evas_object_show(content);

   notify = elm_notify_add(win);
   elm_notify_align_set(notify, 0.5, 1.0);
   elm_object_content_set(notify, content);
   evas_object_size_hint_weight_set(notify, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_show(notify);
   elm_box_pack_end(bx, notify);

   content = elm_label_add(win);
   elm_object_text_set(content, "center");
   evas_object_show(content);

   notify = elm_notify_add(win);
   elm_notify_align_set(notify, 0.5, 0.5);
   elm_object_content_set(notify, content);
   evas_object_size_hint_weight_set(notify, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_show(notify);
   elm_box_pack_end(bx, notify);

   evas_object_resize(win, 100, 200);
   evas_object_show(win);

   elm_run();

   return 0;
}
ELM_MAIN()

static void
_hide(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   evas_object_hide(obj);
}
