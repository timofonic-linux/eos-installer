/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Language page {{{1 */

#include "config.h"
#include "gis-language-page.h"

#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <gtk/gtk.h>

#include "cc-common-language.h"
#include "gdm-languages.h"

#define OBJ(type,name) ((type)gtk_builder_get_object(builder,(name)))
#define WID(name) OBJ(GtkWidget*,name)

typedef struct _LanguageData LanguageData;

struct _LanguageData {
  SetupData *setup;

  GtkWidget *show_all;
  GtkWidget *page;
  GtkTreeModel *liststore;
  gchar *locale_id;
};

enum {
  COL_LOCALE_ID,
  COL_LOCALE_NAME,
  COL_IS_EXTRA,
  NUM_COLS,
};

static void
sync_language (LanguageData *data)
{
  setlocale (LC_MESSAGES, data->locale_id);

  /* XXX more to do */
  g_object_set_data (G_OBJECT (data->page), "gis-page-title", _("Welcome"));
}

static gint
sort_languages (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      data)
{
  char *la, *lb;
  gboolean iea, ieb;
  gint result;

  gtk_tree_model_get (model, a,
                      COL_LOCALE_NAME, &la,
                      COL_IS_EXTRA, &iea,
                      -1);
  gtk_tree_model_get (model, b,
                      COL_LOCALE_NAME, &lb,
                      COL_IS_EXTRA, &ieb,
                      -1);

  if (iea != ieb) {
    return ieb - iea;
  } else {
    result = strcmp (la, lb);
  }

  g_free (la);
  g_free (lb);

  return result;
}

static char *
lgettext (char *locale_id,
          char *string)
{
  char *orig_locale_id = setlocale (LC_MESSAGES, locale_id);
  char *result = gettext(string);
  setlocale (LC_MESSAGES, orig_locale_id);
  return result;
}

static char *
use_language (char *locale_id)
{
  char *use, *language;

  /* Translators: the parameter here is your language's name, like
   * "Use English", "Deutsch verwenden", etc. */
  use = N_("Use %s");
  use = lgettext (locale_id, use);

  language = gdm_get_language_from_name (locale_id, locale_id);

  return g_strdup_printf (use, language);
}

static void
select_locale_id (GtkTreeView *treeview,
                  char        *locale_id)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean cont;

  model = gtk_tree_view_get_model (treeview);
  cont = gtk_tree_model_get_iter_first (model, &iter);
  while (cont) {
    char *iter_locale_id;

    gtk_tree_model_get (model, &iter,
                        COL_LOCALE_ID, &iter_locale_id,
                        -1);

    if (iter_locale_id == NULL)
      continue;

    if (g_str_equal (locale_id, iter_locale_id)) {
      GtkTreeSelection *selection;
      selection = gtk_tree_view_get_selection (treeview);
      gtk_tree_selection_select_iter (selection, &iter);
      g_free (iter_locale_id);
      break;
    }

    g_free (iter_locale_id);
    cont = gtk_tree_model_iter_next (model, &iter);
  }
}

static void
select_current_locale (GtkTreeView *treeview)
{
  gchar *current_language = cc_common_language_get_current_language ();
  select_locale_id (treeview, current_language);
  g_free (current_language);
}

static void
add_languages (GtkListStore *liststore,
               char        **locale_ids,
               GHashTable   *initial)
{
  while (*locale_ids) {
    gchar *locale_id;
    gchar *locale_name;
    gboolean is_extra;
    GtkTreeIter iter;

    locale_id = *locale_ids;

    if (!cc_common_language_has_font (locale_id))
      continue;

    is_extra = (g_hash_table_lookup (initial, locale_id) != NULL);
    locale_name = use_language (locale_id);

    gtk_list_store_insert_with_values (liststore, &iter, -1,
                                       COL_LOCALE_ID, locale_id,
                                       COL_LOCALE_NAME, locale_name,
                                       COL_IS_EXTRA, is_extra,
                                       -1);

    locale_ids ++;
  }
}

static void
add_all_languages (GtkListStore *liststore)
{
  char **locale_ids = gdm_get_all_language_names ();
  GHashTable *initial =  cc_common_language_get_initial_languages ();

  add_languages (liststore, locale_ids, initial);
}

static gboolean
language_visible (GtkTreeModel *model,
                  GtkTreeIter  *iter,
                  gpointer      user_data)
{
  LanguageData *data = user_data;
  gboolean is_extra;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->show_all)))
    return TRUE;

  gtk_tree_model_get (model, iter,
                      COL_IS_EXTRA, &is_extra,
                      -1);

  return is_extra;
}

void
gis_prepare_language_page (SetupData *setup)
{
  LanguageData *data;
  GisAssistant *assistant = gis_get_assistant (setup);
  GtkBuilder *builder = gis_builder ("gis-language-page");
  GtkListStore *liststore;
  GtkTreeModel *filter;
  GtkTreeView *treeview;

  liststore = gtk_list_store_new (NUM_COLS,
                                  G_TYPE_STRING,
                                  G_TYPE_STRING,
                                  G_TYPE_BOOLEAN);

  data = g_slice_new0 (LanguageData);
  data->setup = setup;
  data->locale_id = cc_common_language_get_current_language ();
  data->page = WID ("language-page");
  data->show_all = WID ("language-show-all");
  data->liststore = GTK_TREE_MODEL (liststore);
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (liststore),
                                           sort_languages, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (liststore),
                                        GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                        GTK_SORT_ASCENDING);

  treeview = OBJ (GtkTreeView *, "language-list");

  filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (liststore), NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (filter),
                                          language_visible, data, NULL);
  gtk_tree_view_set_model (treeview, filter);

  add_all_languages (GTK_LIST_STORE (data->liststore));

  g_signal_connect_swapped (data->show_all, "toggled",
                            G_CALLBACK (gtk_tree_model_filter_refilter),
                            filter);

  gis_assistant_add_page (assistant, data->page);
  gis_assistant_set_page_complete (assistant, data->page, TRUE);

  sync_language (data);
  select_current_locale (treeview);

  g_object_unref (builder);
}
