#define APPLICATION_ID "com.github.ToshioCP.menu1"
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

static void quit_activated(GSimpleAction *action, GVariant *parameter, GApplication *application) {
   g_application_quit (application);
}

static void help_activated(GSimpleAction *action, GVariant *parameter, GApplication *application) {
   g_print ("help\n");
}

static void info_activated(GSimpleAction *action, GVariant *parameter, GApplication *application) {
   g_print ("info\n");
}

static void app_activate (GApplication *application) {
   GtkApplication *app = GTK_APPLICATION (application);
   GtkWidget *win = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (win), "menu1");
   gtk_window_set_default_size (GTK_WINDOW (win), 400, 300);

   gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (win), TRUE);
   gtk_window_present (GTK_WINDOW (win));
}

void subMenu (GMenu *vMenu, const char *name, const char *app, const gchar *iconName) {
   GMenuItem *menu_item = g_menu_item_new (name, app);
   GtkWidget *icon = gtk_image_new_from_icon_name(iconName);
   // GVariant *icon_variant = g_variant_new("o", G_OBJECT(icon));
   // g_menu_item_set_attribute(G_MENU_ITEM(menu_item), "icon", "v", icon_variant);

   g_menu_append_item (vMenu, menu_item);
   g_object_unref (menu_item);
}

/*! submenu with label and icon 
static GtkWidget *mySubMenu (const gchar *str, const gchar *iconName) {
   GtkWidget *my_item= gtk_menu_item_new();
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *icon = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_MENU);
   GtkWidget *label = gtk_label_new(str);
   gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

   GtkWidget *spacer = gtk_label_new(" ");
   gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(my_item), box);
   return my_item;
}
*/


static void app_startup (GApplication *application) {
   GtkApplication *app = GTK_APPLICATION (application);

   GSimpleAction *act_quit = g_simple_action_new ("quit", NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_quit));
   g_signal_connect (act_quit, "activate", G_CALLBACK (quit_activated), application);

   GSimpleAction *act_help = g_simple_action_new ("help", NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_help));
   g_signal_connect (act_help, "activate", G_CALLBACK (help_activated), application);

   GSimpleAction *act_info = g_simple_action_new ("info", NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_info));
   g_signal_connect (act_info, "activate", G_CALLBACK (info_activated), application);

   GMenu *menubar = g_menu_new ();
   GMenuItem *file_menu = g_menu_item_new("_Grib", NULL);
   GMenuItem *polar_menu = g_menu_item_new("_Polar", NULL);
   GMenuItem *scenario_menu = g_menu_item_new("_Scenario", NULL);
   GMenuItem *dump_menu = g_menu_item_new("_Display", NULL);
   GMenuItem *poi_menu = g_menu_item_new("PO_I", NULL);
   GMenuItem *help_menu = g_menu_item_new("_Help", NULL);


   GMenu *file_menu_v = g_menu_new ();
   subMenu (file_menu_v, "Quit", "app.quit", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Ex", "app.quit", "applications-engineering-symbolic");
   

   GMenu *help_menu_v = g_menu_new ();
   subMenu (help_menu_v, "Help", "app.help", "help-browser-symbolic");
   subMenu (help_menu_v, "Info", "app.info", "help-about-symbolic");

  //g_menu_item_set_submenu (menu_item_menu, G_MENU_MODEL (menu));
   g_menu_item_set_submenu (file_menu, G_MENU_MODEL (file_menu_v));
   g_menu_item_set_submenu (polar_menu, G_MENU_MODEL (file_menu_v));
   g_menu_item_set_submenu (scenario_menu, G_MENU_MODEL (file_menu_v));
   g_menu_item_set_submenu (dump_menu, G_MENU_MODEL (file_menu_v));
   g_menu_item_set_submenu (poi_menu, G_MENU_MODEL (file_menu_v));
   g_object_unref (file_menu_v);
   g_menu_item_set_submenu (help_menu, G_MENU_MODEL (help_menu_v));
   g_object_unref (help_menu_v);

   g_menu_append_item (menubar, file_menu);
   g_menu_append_item (menubar, polar_menu);
   g_menu_append_item (menubar, scenario_menu);
   g_menu_append_item (menubar, dump_menu);
   g_menu_append_item (menubar, poi_menu);
   g_menu_append_item (menubar, help_menu);

   GtkWidget *run_button = gtk_button_new_from_icon_name ("system-run");
   //GtkToolItem *run_button = gtk_tool_button_new(NULL, NULL);
   //gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(run_button), "system-run"); 

   g_object_unref (file_menu);
   g_object_unref (polar_menu);
   g_object_unref (dump_menu);
   g_object_unref (poi_menu);
   g_object_unref (help_menu);

   gtk_application_set_menubar (GTK_APPLICATION (app), G_MENU_MODEL (menubar));
}


int main (int argc, char **argv) {
  GtkApplication *app;
  int stat;

  app = gtk_application_new (APPLICATION_ID, 0);
  g_signal_connect (app, "startup", G_CALLBACK (app_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);

  stat =g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);
  return stat;
}
