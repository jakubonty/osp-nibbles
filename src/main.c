/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 *   Gnome Nibbles: Gnome Worm Game
 *   Written by Sean MacIsaac <sjm@acm.org>, Ian Peters <itp@gnu.org>,
 *              Guillaume Beland <guillaume.beland@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "main.h"
#include "properties.h"
#include "gnibbles.h"
#include "bonus.h"
#include "boni.h"
#include "preferences.h"
#include "scoreboard.h"
#include "warp.h"
#include "games-gridframe.h"
#include "games-pause-action.h"
#include "games-fullscreen-action.h"
#include "games-scores.h"

#include <clutter-gtk/clutter-gtk.h>
#include <clutter/clutter.h>

#include "board.h"
#include "worm.h"

#define DEFAULT_WIDTH 650
#define DEFAULT_HEIGHT 520

GSettings *settings;
GSettings *worm_settings[NUMWORMS];
GtkWidget *window;
GtkWidget *statusbar;
GtkWidget *notebook;
GtkWidget *chat = NULL;

static const GamesScoresCategory scorecats[] = {
{ "4.0", NC_("game speed", "Beginner")            },
{ "3.0", NC_("game speed", "Slow")                },
{ "2.0", NC_("game speed", "Medium")              },
{ "1.0", NC_("game speed", "Fast")                },
{ "4.1", NC_("game speed", "Beginner with Fakes") },
{ "3.1", NC_("game speed", "Slow with Fakes")     },
{ "2.1", NC_("game speed", "Medium with Fakes")   },
{ "1.1", NC_("game speed", "Fast with Fakes")     }
};

GamesScores *highscores;

GnibblesProperties *properties;

GnibblesBoard *board;

GnibblesScoreboard *scoreboard;

GtkWidget *clutter_widget;
ClutterActor *stage;

extern GnibblesWorm *worms[];
extern GnibblesBoni *boni;

gint main_id = 0;
gint dummy_id = 0;
gint keyboard_id = 0;
gint add_bonus_id = 0;
gint restart_id = 0;

gint current_level;

static gint add_bonus_cb (gpointer data);

static gint end_game_cb (GtkAction * action, gpointer data);

static GtkAction *new_game_action;
GtkAction *pause_action;
static GtkAction *end_game_action;
static GtkAction *preferences_action;
static GtkAction *scores_action;
static GtkAction *fullscreen_action;

static void
hide_cursor (void)
{
  clutter_stage_hide_cursor (CLUTTER_STAGE (stage));
}

static void
show_cursor (void)
{
  clutter_stage_show_cursor (CLUTTER_STAGE (stage));
}

gint
game_running (void)
{
  return (main_id || dummy_id || restart_id || games_pause_action_get_is_paused (GAMES_PAUSE_ACTION (pause_action)));
}

/* Avoid a race condition where a redraw is attempted
 * between the window being destroyed and the destroy
 * event being sent. */
static gint
delete_cb (GtkWidget * widget, gpointer data)
{
  if (main_id)
    g_source_remove (main_id);
  if (dummy_id)
    g_source_remove (dummy_id);
  if (restart_id)
    g_source_remove (restart_id);

  return FALSE;
}

static void
quit_cb (GObject * object, gpointer data)
{
  gtk_widget_destroy (window);
}

static void
about_cb (GtkAction * action, gpointer data)
{
  const gchar *authors[] = { "Sean MacIsaac", "Ian Peters", "Andreas Røsdal",
                             "Guillaume Beland", NULL };

  const gchar *documenters[] = { "Kevin Breit", NULL };

  gtk_show_about_dialog (GTK_WINDOW (window),
       "program-name", _("Nibbles"),
       "version", VERSION,
       "copyright",
       "Copyright \xc2\xa9 1999-2008 Sean MacIsaac, Ian Peters, Andreas Røsdal"
       " 2009 Guillaume Beland",

       "license-type", GTK_LICENSE_GPL_2_0, 
       "comments", _("A worm game for GNOME.\n\nNibbles is a part of GNOME Games."),
       "authors", authors,
       "documenters", documenters, 
       "translator-credits", _("translator-credits"), 
       "logo-icon-name", "gnome-nibbles", 
       "website", "http://www.gnome.org/projects/gnome-games/",
       "website-label", _("GNOME Games web site"),
       "wrap-license", TRUE, NULL);
}

static gboolean
key_press_cb (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  hide_cursor ();

  if (!(event->type == CLUTTER_KEY_PRESS))
    return FALSE;

  return gnibbles_keypress_worms (event->key.keyval);
}

static gboolean
configure_event_cb (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
  int tilesize, ts_x, ts_y;
  int i;

  /* Compute the new tile size based on the size of the
   * drawing area, rounded down. */
  ts_x = event->width / BOARDWIDTH;
  ts_y = event->height / BOARDHEIGHT;
  if (ts_x * BOARDWIDTH > event->width)
    ts_x--;
  if (ts_y * BOARDHEIGHT > event->height)
    ts_y--;
  tilesize = MIN (ts_x, ts_y);

  if(tilesize != properties->tilesize)
  {	
    gnibbles_load_pixmap (tilesize);

    clutter_actor_set_size (CLUTTER_ACTOR (stage),
                          BOARDWIDTH * tilesize,
                          BOARDHEIGHT * tilesize);

    gnibbles_board_rescale (board, tilesize);

    if (game_running ()) {
      gnibbles_boni_rescale (boni, tilesize);

      for (i=0; i<properties->numworms; i++)
        gnibbles_worm_rescale (worms[i], tilesize);

      if (warpmanager)
        gnibbles_warpmanager_rescale (warpmanager, tilesize);
    }

    properties->tilesize = tilesize;
    gnibbles_properties_set_tile_size (tilesize);
  }

  return FALSE;
}

static gboolean
new_game_2_cb (GtkWidget * widget, gpointer data)
{
  if (!games_pause_action_get_is_paused (GAMES_PAUSE_ACTION (pause_action))) {
    if (!keyboard_id)
      keyboard_id = g_signal_connect (G_OBJECT (stage),
                                      "key-press-event",
                                      G_CALLBACK (key_press_cb), NULL);
    if (!main_id) {
      main_id = g_timeout_add (GAMEDELAY * properties->gamespeed,
                               (GSourceFunc) main_loop, NULL);
    }
    if (!add_bonus_id) {
      add_bonus_id = g_timeout_add (BONUSDELAY *
                                    properties->gamespeed,
                                    (GSourceFunc) add_bonus_cb, NULL);
    }
  }

  dummy_id = 0;

  return FALSE;
}

gboolean
new_game (void)
{
  int i;
  gtk_action_set_sensitive (pause_action, TRUE);
  gtk_action_set_sensitive (end_game_action, TRUE);
  gtk_action_set_sensitive (new_game_action, FALSE);
  gtk_action_set_sensitive (preferences_action, TRUE);

  if (game_running ()) {
    main_id = 0;
  }

  if (!properties->random) {
    current_level = properties->startlevel;
  } else {
    current_level = rand () % MAXLEVEL + 1;
  }

  gnibbles_init ();
  gnibbles_board_level_new (board, current_level);
  gnibbles_board_level_add_bonus (board, 1);

  for (i = 0; i < properties->numworms; i++) {
    if (!clutter_actor_get_stage (worms[i]->actors))
      clutter_container_add_actor (CLUTTER_CONTAINER (stage), worms[i]->actors);
    gnibbles_worm_show (worms[i]);
  }

  games_pause_action_set_is_paused (GAMES_PAUSE_ACTION (pause_action), FALSE);

  if (restart_id) {
    g_source_remove (restart_id);
    restart_id = 0;
  }

  if (add_bonus_id) {
    g_source_remove (add_bonus_id);
    add_bonus_id = 0;
  }

  if (dummy_id)
    g_source_remove (dummy_id);

  dummy_id = g_timeout_add_seconds (1, (GSourceFunc) new_game_2_cb, NULL);

  return TRUE;
}

static void
new_game_cb (GtkAction * action, gpointer data)
{
  new_game ();
}

static void
pause_game_cb (GtkAction * action, gpointer data)
{
  if (games_pause_action_get_is_paused (GAMES_PAUSE_ACTION (action))) {
    if (main_id || restart_id || dummy_id) {
      if (main_id) {
        g_source_remove (main_id);
        main_id = 0;
      }
      if (keyboard_id) {
        g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);
        keyboard_id = 0;
      }
      if (add_bonus_id) {
        g_source_remove (add_bonus_id);
        add_bonus_id = 0;
      }
    }
  }
  else {
    dummy_id = g_timeout_add (500, (GSourceFunc) new_game_2_cb, NULL);
  }
}

static void
show_scores_cb (GtkAction * action, gpointer data)
{
  gnibbles_show_scores (window, 0);
}

void
end_game (void)
{
  if (main_id) {
    g_source_remove (main_id);
    main_id = 0;
  }

  if (keyboard_id) {
    g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);
    keyboard_id = 0;
  }

  if (add_bonus_id) {
    g_source_remove (add_bonus_id);
    add_bonus_id = 0;
  }

  if (dummy_id) {
    g_source_remove (dummy_id);
    dummy_id = 0;
  }

  if (restart_id) {
    g_source_remove (restart_id);
    restart_id = 0;
  }

  animate_end_game ();

  gtk_action_set_sensitive (pause_action, FALSE);
  gtk_action_set_sensitive (end_game_action, FALSE);
  gtk_action_set_sensitive (new_game_action, TRUE);
  gtk_action_set_sensitive (preferences_action, TRUE);

  games_pause_action_set_is_paused (GAMES_PAUSE_ACTION (pause_action), FALSE);
}

static gboolean
end_game_cb (GtkAction * action, gpointer data)
{
  end_game ();
  return FALSE;
}

static gboolean
add_bonus_cb (gpointer data)
{
  gnibbles_board_level_add_bonus (board, 0);
  return TRUE;
}

static gboolean
restart_game (gpointer data)
{
  int i;

  gnibbles_board_level_new (board, current_level);
  gnibbles_board_level_add_bonus (board, 1);

  for (i = 0; i < properties->numworms; i++) {
    if (!clutter_actor_get_stage (worms[i]->actors))
      clutter_container_add_actor (CLUTTER_CONTAINER (stage), worms[i]->actors);
    gnibbles_worm_show (worms[i]);
  }

  for (i = 0; i < properties->human; i++)
    worms[i]->human = TRUE;

  dummy_id = g_timeout_add_seconds (1, (GSourceFunc) new_game_2_cb, NULL);
  restart_id = 0;

  return FALSE;
}

void
animate_end_game (void)
{
  int i;
  for (i = 0; i < properties->numworms; i++) {
    clutter_actor_animate (worms[i]->actors, CLUTTER_EASE_IN_QUAD, 500,
                           "opacity", 0,
                           "scale-x", 0.4, "scale-y", 0.4,
                           "fixed::scale-center-x",
                           (gfloat) worms[i]->xhead * properties->tilesize,
                           "fixed::scale-center-y",
                           (gfloat) worms[i]->yhead * properties->tilesize,
                           NULL);
  }

  for ( i = 0; i < boni->numbonuses; i++) {
    clutter_actor_animate (boni->bonuses[i]->actor, CLUTTER_EASE_IN_QUAD, 500,
                           "opacity", 0,
                           "scale-x", 0.4, "scale-y", 0.4,
                           "fixed::scale-gravity", CLUTTER_GRAVITY_CENTER,
                           NULL);

  }

  clutter_actor_animate (board->level, CLUTTER_EASE_IN_QUAD, 700,
                           "scale-x", 0.4, "scale-y", 0.4,
                           "fixed::scale-gravity", CLUTTER_GRAVITY_CENTER,
                           "opacity", 0,
                           NULL);
}

gboolean
main_loop (gpointer data)
{
  gint status;
  gint tmp, winner;
  gchar *str = NULL;

  status = gnibbles_move_worms ();
  gnibbles_scoreboard_update (scoreboard);

  if (status == VICTORY) {
    end_game ();
    winner = gnibbles_get_winner ();

    if (winner == -1)
      return FALSE;

    str = g_strdup_printf (_("Game over! The game has been won by %s!"),
                             names[winner]);
    g_free (str);

    if (keyboard_id) {
      g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);
      keyboard_id = 0;
    }
    if (main_id) {
      g_source_remove (main_id);
      main_id = 0;
    }
    if (add_bonus_id)
      g_source_remove (add_bonus_id);

    add_bonus_id = 0;

    animate_end_game ();
    gnibbles_log_score (window);

    return FALSE;
  }

  if (status == GAMEOVER) {

    if (keyboard_id) {
      g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);
      keyboard_id = 0;
    }
    main_id = 0;
    if (add_bonus_id)
      g_source_remove (add_bonus_id);

    add_bonus_id = 0;

    animate_end_game ();
    gnibbles_log_score (window);

    return FALSE;
  }

  if (status == NEWROUND) {
    if (keyboard_id) {
      g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);
      keyboard_id = 0;
    }
    if (add_bonus_id)
      g_source_remove (add_bonus_id);

    if (main_id) {
      g_source_remove (main_id);
      main_id = 0;
    }
    add_bonus_id = 0;

    animate_end_game ();
    restart_id = g_timeout_add_seconds (1, (GSourceFunc) restart_game, NULL);

    return FALSE;
  }

  if (boni->numleft == 0) {
    if (restart_id)
      return TRUE;

    if (keyboard_id)
      g_signal_handler_disconnect (G_OBJECT (stage), keyboard_id);

    keyboard_id = 0;

    if (add_bonus_id)
      g_source_remove (add_bonus_id);

    add_bonus_id = 0;
    if (main_id) {
      g_source_remove (main_id);
      main_id = 0;
    }
    if ((current_level < MAXLEVEL) && (!properties->random)) {
      current_level++;
    } else if (properties->random) {
      tmp = rand () % MAXLEVEL + 1;
      while (tmp == current_level)
        tmp = rand () % MAXLEVEL + 1;
      current_level = tmp;
    }
    animate_end_game ();
    restart_id = g_timeout_add_seconds (1, (GSourceFunc) restart_game, NULL);
    return FALSE;
  }

  return TRUE;
}

static gboolean
show_cursor_cb (GtkWidget * widget, GdkEventMotion *event, gpointer data)
{
  show_cursor ();
  return FALSE;
}

static void
help_cb (GtkAction * action, gpointer data)
{
  GError *error = NULL;

  gtk_show_uri (gtk_widget_get_screen (GTK_WIDGET (window)), "help:gnome-nibbles", gtk_get_current_event_time (), &error);
  if (error)
    g_warning ("Failed to show help: %s", error->message);
  g_clear_error (&error);
}

static const GtkActionEntry action_entry[] = {
  {"GameMenu", NULL, N_("_Game")},
  {"ViewMenu", NULL, N_("_View")},
  {"SettingsMenu", NULL, N_("_Settings")},
  {"HelpMenu", NULL, N_("_Help")},
  {"NewGame", NULL, N_("_New Game"), NULL, NULL,
   G_CALLBACK (new_game_cb)},
  {"EndGame", NULL, N_("_End Game"), NULL, NULL,
   G_CALLBACK (end_game_cb)},
  {"Scores", NULL, N_("_Scores"), NULL, NULL,
   G_CALLBACK (show_scores_cb)},
  {"Quit", GTK_STOCK_QUIT, NULL, NULL, NULL, G_CALLBACK (quit_cb)},
  {"Preferences", GTK_STOCK_PREFERENCES, NULL, NULL, NULL,
   G_CALLBACK (gnibbles_preferences_cb)},
  {"Contents", NULL, N_("_Contents"), NULL, NULL, G_CALLBACK (help_cb)},
  {"About", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK (about_cb)}
};

static const char ui_description[] =
  "<ui>"
  "  <menubar name='MainMenu'>"
  "    <menu action='GameMenu'>"
  "      <menuitem action='NewGame'/>"
  "      <menuitem action='EndGame'/>"
  "      <separator/>"
  "      <menuitem action='Pause'/>"
  "      <separator/>"
  "      <menuitem action='Scores'/>"
  "      <separator/>"
  "      <menuitem action='Quit'/>"
  "    </menu>"
  "    <menu action='ViewMenu'>"
  "      <menuitem action='Fullscreen'/>"
  "    </menu>"
  "    <menu action='SettingsMenu'>"
  "      <menuitem action='Preferences'/>"
  "    </menu>"
  "    <menu action='HelpMenu'>"
  "      <menuitem action='Contents'/>"
  "      <menuitem action='About'/>"
  "    </menu>"
  "  </menubar>"
  "</ui>";

static void
create_menus (GtkUIManager * ui_manager)
{
  GtkActionGroup *action_group;

  action_group = gtk_action_group_new ("MenuActions");

  gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (action_group, action_entry,
                                G_N_ELEMENTS (action_entry), window);

  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
  gtk_ui_manager_add_ui_from_string (ui_manager, ui_description, -1, NULL);

  new_game_action = gtk_action_group_get_action (action_group, "NewGame");
  scores_action = gtk_action_group_get_action (action_group, "Scores");
  end_game_action = gtk_action_group_get_action (action_group, "EndGame");
  pause_action = GTK_ACTION (games_pause_action_new ("Pause"));
  g_signal_connect (G_OBJECT (pause_action), "state-changed", G_CALLBACK (pause_game_cb), NULL);
  gtk_action_group_add_action_with_accel (action_group, pause_action, NULL);

  preferences_action = gtk_action_group_get_action (action_group,
                                                    "Preferences");
  fullscreen_action = GTK_ACTION (games_fullscreen_action_new ("Fullscreen", GTK_WINDOW(window)));
  gtk_action_group_add_action_with_accel (action_group, fullscreen_action, NULL);

}

static void
setup_window (void)
{
  GtkWidget *vbox;
  GtkWidget *packing;
  GtkWidget *menubar;

  GtkUIManager *ui_manager;
  GtkAccelGroup *accel_group;
  ClutterColor stage_color = {0x00,0x00,0x00,0xff};

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  clutter_widget = gtk_clutter_embed_new ();
  stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (clutter_widget));

  clutter_stage_set_color (CLUTTER_STAGE(stage), &stage_color);

  clutter_actor_set_size (CLUTTER_ACTOR (stage),
                          properties->tilesize * BOARDWIDTH,
                          properties->tilesize * BOARDHEIGHT);
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), FALSE);

  board = gnibbles_board_new ();

  gtk_window_set_title (GTK_WINDOW (window), _("Nibbles"));

  gtk_window_set_default_size (GTK_WINDOW (window),
                               DEFAULT_WIDTH, DEFAULT_HEIGHT);
  //games_conf_add_window (GTK_WINDOW (window), KEY_PREFERENCES_GROUP);

  g_signal_connect (G_OBJECT (window), "destroy",
                    G_CALLBACK (gtk_main_quit), NULL);
  g_signal_connect (G_OBJECT (window), "delete_event",
                    G_CALLBACK (delete_cb), NULL);

  gtk_widget_realize (window);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  ui_manager = gtk_ui_manager_new ();
  create_menus (ui_manager);
  notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);

  accel_group = gtk_ui_manager_get_accel_group (ui_manager);
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  menubar = gtk_ui_manager_get_widget (ui_manager, "/MainMenu");

  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);

  packing = games_grid_frame_new (BOARDWIDTH, BOARDHEIGHT);
  gtk_widget_show (packing);

  gtk_container_add (GTK_CONTAINER (packing), clutter_widget);

  g_signal_connect (G_OBJECT (clutter_widget), "configure_event",
                    G_CALLBACK (configure_event_cb), NULL);

  g_signal_connect (G_OBJECT (window), "focus_out_event",
                    G_CALLBACK (show_cursor_cb), NULL);

  gtk_box_pack_start (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), packing, NULL);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), MAIN_PAGE);

  statusbar = gtk_statusbar_new ();
  gtk_box_pack_start (GTK_BOX (vbox), statusbar, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_widget_show_all (window);

  scoreboard = gnibbles_scoreboard_new (statusbar);
}

int
main (int argc, char **argv)
{
  GOptionContext *context;
  gboolean retval;
  int i;
  GError *error = NULL;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  games_scores_startup ();

  g_set_application_name (_("Nibbles"));

  context = g_option_context_new (NULL);
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));

  retval = g_option_context_parse (context, &argc, &argv, &error);
  g_option_context_free (context);
  if (!retval) {
    g_print ("%s\n", error->message);
    g_error_free (error);
    exit (1);
  }
  
  if (gtk_clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS) {
    GtkWidget *dialog = gtk_message_dialog_new (NULL,
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_NONE,
                                                "%s", "Unable to initialize Clutter.");
    gtk_window_set_title (GTK_WINDOW (dialog), g_get_application_name ());
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    exit (1);
  }

  settings = g_settings_new ("org.gnome.nibbles");
  for (i = 0; i < NUMWORMS; i++)
  {
    gchar *name = g_strdup_printf ("org.gnome.nibbles.worm%d", i);
    worm_settings[i] = g_settings_new (name);
    g_free (name);
  }

  gtk_window_set_default_icon_name ("gnome-nibbles");
  srand (time (NULL));

  highscores = games_scores_new ("gnome-nibbles",
                                 scorecats, G_N_ELEMENTS (scorecats),
                                 "game speed", NULL,
                                 0 /* default category */,
                                 GAMES_SCORES_STYLE_PLAIN_DESCENDING);

  properties = gnibbles_properties_new ();
  setup_window ();
  gnibbles_load_pixmap (properties->tilesize);

  gtk_action_set_sensitive (pause_action, FALSE);
  gtk_action_set_sensitive (end_game_action, FALSE);
  gtk_action_set_visible (new_game_action, TRUE);

  gtk_main ();

  gnibbles_properties_destroy (properties);

  return 0;
}
