/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat
 *               2016 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Original code written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/* Install page {{{1 */

#define PAGE_ID "install"

#include "config.h"
#include "install-resources.h"
#include "gis-install-page.h"
#include "gis-scribe.h"
#include "gis-store.h"

#include <udisks/udisks.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct _GisInstallPagePrivate {
  UDisksClient *client;
  guint pulse_id;

  GtkWidget *warning_dialog;
  gboolean   writing;
};
typedef struct _GisInstallPagePrivate GisInstallPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisInstallPage, gis_install_page, GIS_TYPE_PAGE);

#define OBJ(type,name) ((type)gtk_builder_get_object(GIS_PAGE(page)->builder,(name)))
#define WID(name) OBJ(GtkWidget*,name)

static gboolean
delete_event_cb (GtkWidget      *toplevel,
                 GdkEvent       *event,
                 GisInstallPage *self)
{
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (self);
  GtkWidget *button;
  gboolean should_propagate;
  gint response_id;

  /* If we're not writing, it's still safe to quit */
  if (!priv->writing)
    return GDK_EVENT_PROPAGATE;

  priv->warning_dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_WARNING,
                                                 GTK_BUTTONS_NONE,
                                                 _("Stop reformatting the disk?"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (priv->warning_dialog),
                                            _("The reformatting process has already begun. Cancelling "
                                              "now will leave this system unbootable."));

  /* Stop Reformatting */
  button = gtk_button_new_with_label (_("Stop Reformatting"));
  gtk_style_context_add_class (gtk_widget_get_style_context (button), "destructive-action");
  gtk_dialog_add_action_widget (GTK_DIALOG (priv->warning_dialog), button, GTK_RESPONSE_OK);

  gtk_widget_show (button);

  /* Continue Reformatting */
  button = gtk_button_new_with_label (_("Continue Reformatting"));
  gtk_dialog_add_action_widget (GTK_DIALOG (priv->warning_dialog), button, GTK_RESPONSE_CANCEL);

  gtk_widget_set_can_default (button, TRUE);
  gtk_widget_show (button);

  /* Let's avoid accidents */
  gtk_dialog_set_default_response (GTK_DIALOG (priv->warning_dialog), GTK_RESPONSE_CANCEL);
  gtk_widget_grab_focus (button);

  /* Run the dialog */
  response_id = gtk_dialog_run (GTK_DIALOG (priv->warning_dialog));
  gtk_widget_destroy (priv->warning_dialog);
  priv->warning_dialog = NULL;

  should_propagate = response_id == GTK_RESPONSE_OK ? GDK_EVENT_PROPAGATE : GDK_EVENT_STOP;

  return should_propagate;
}

static void
gis_install_page_unmount_image_partition (GisPage *page)
{
  GisInstallPage *install = GIS_INSTALL_PAGE (page);
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (install);
  GDBusObjectManager *manager = udisks_client_get_object_manager(priv->client);
  GList *objects = g_dbus_object_manager_get_objects(manager);
  GList *l;
  const gchar *label;

  if (gis_store_is_live_install ())
    label = "eoslive";
  else
    label = "eosimages";

  for (l = objects; l != NULL; l = l->next)
    {
      UDisksFilesystem *fs;
      UDisksBlock *block = udisks_object_peek_block (UDISKS_OBJECT (l->data));

      if (block == NULL)
        continue;

      if (!g_str_equal (label, udisks_block_get_id_label (block)))
        continue;

      fs = udisks_object_peek_filesystem (UDISKS_OBJECT (l->data));
      udisks_filesystem_call_unmount_sync (fs, g_variant_new ("a{sv}", NULL), NULL, NULL);
    }
}

static void
gis_install_page_stop_pulsing (GisInstallPage *page)
{
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (page);
  GtkProgressBar *progress = OBJ (GtkProgressBar*, "install_progress");

  if (priv->pulse_id)
    {
      gtk_widget_remove_tick_callback ((GtkWidget *) progress, priv->pulse_id);
      priv->pulse_id = 0;
    }
}

static gboolean
gis_install_page_teardown (GisPage *page)
{
  GisInstallPage *install = GIS_INSTALL_PAGE (page);
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (install);
  GtkProgressBar *progress = OBJ (GtkProgressBar*, "install_progress");

  gis_install_page_stop_pulsing (install);

  gis_install_page_unmount_image_partition (page);

  gtk_progress_bar_set_fraction (progress, 1.0);

  /*
   * If there's a message dialog asking whether the user wants to quit, and
   * we finished writing, hide and ignore that dialog.
   */
  if (priv->warning_dialog)
    {
      gtk_dialog_response (GTK_DIALOG (priv->warning_dialog), GTK_RESPONSE_CANCEL);
      gtk_widget_destroy (priv->warning_dialog);
      priv->warning_dialog = NULL;
    }

  g_signal_handlers_disconnect_by_func (gtk_widget_get_toplevel (GTK_WIDGET (page)),
                                        delete_event_cb,
                                        page);

  gis_assistant_next_page (gis_driver_get_assistant (page->driver));

  return FALSE;
}

static gboolean
gis_install_page_pulse_progress (GtkProgressBar *bar)
{
  gtk_progress_bar_pulse (bar);

  return TRUE;
}

static void
gis_install_page_ensure_pulsing (GisInstallPage *page,
                                 GtkProgressBar *progress)
{
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (page);

  if (priv->pulse_id == 0)
    {
      gtk_progress_bar_set_pulse_step (progress, 1. / 60.);
      priv->pulse_id = gtk_widget_add_tick_callback (
          GTK_WIDGET (progress),
          (GtkTickCallback) gis_install_page_pulse_progress,
          NULL, NULL);
    }
}

static gboolean
gis_install_page_is_efi_system (GisPage *page)
{
  return g_file_test ("/sys/firmware/efi", G_FILE_TEST_IS_DIR);
}

static void
gis_install_page_update_step (GisInstallPage *page,
                              guint           step)
{
  g_autofree gchar *msg = g_strdup_printf (_("Step %d of %d"), step, 2);

  gtk_label_set_text (OBJ (GtkLabel*, "install_label"), msg);
}


static void
gis_install_page_step_cb (GObject    *object,
                          GParamSpec *pspec,
                          gpointer    data)
{
  GisInstallPage *install = GIS_INSTALL_PAGE (data);
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (install);
  GisScribe *scribe = GIS_SCRIBE (object);
  guint step = gis_scribe_get_step (scribe);

  gis_install_page_update_step (install, step);

  if (step == 2)
    priv->writing = TRUE;
}

static void
gis_install_page_progress_cb (GObject    *object,
                              GParamSpec *pspec,
                              gpointer    data)
{
  GisPage *page = GIS_PAGE (data);
  GisScribe *scribe = GIS_SCRIBE (object);
  gdouble progress = gis_scribe_get_progress (scribe);
  GtkProgressBar *bar = OBJ (GtkProgressBar*, "install_progress");

  if (progress < 0)
    gis_install_page_ensure_pulsing (GIS_INSTALL_PAGE (page), bar);
  else
    gtk_progress_bar_set_fraction (bar, progress);
}

static void
gis_install_page_write_cb (GObject      *source,
                           GAsyncResult *result,
                           gpointer      data)
{
  GisPage *page = GIS_PAGE (data);
  GisScribe *scribe = GIS_SCRIBE (source);
  g_autoptr(GError) error = NULL;

  if (!gis_scribe_write_finish (scribe, result, &error))
    gis_store_set_error (error);

  gis_install_page_teardown (page);
}

static void
gis_install_page_open_for_restore_cb (GObject      *source,
                                      GAsyncResult *result,
                                      gpointer      data)
{
  GisPage *page = GIS_PAGE (data);
  UDisksBlock *block = UDISKS_BLOCK (source);
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) fd_index = NULL;
  gint fd = -1;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) image = NULL;
  g_autoptr(GFile) signature = NULL;
  g_autoptr(GisScribe) scribe = NULL;

  if (!udisks_block_call_open_for_restore_finish (block, &fd_index, &fd_list,
                                                  result, &error))
    {
      goto error;
    }

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), &error);
  if (fd < 0)
    {
      g_prefix_error (&error,
                      "Error extracting fd with handle %d from D-Bus message: ",
                      g_variant_get_handle (fd_index));
      goto error;
    }

  image = g_object_ref (gis_store_get_object (GIS_STORE_IMAGE));
  signature = g_file_new_for_path (gis_store_get_image_signature ());
  scribe = gis_scribe_new (image,
                           gis_store_get_required_size (),
                           signature,
                           udisks_block_get_device (block),
                           fd,
                           gis_install_page_is_efi_system (page));
  g_signal_connect (scribe, "notify::step",
                    (GCallback) gis_install_page_step_cb, page);
  g_signal_connect (scribe, "notify::progress",
                    (GCallback) gis_install_page_progress_cb, page);

  gis_scribe_write_async (scribe,
                          NULL,
                          gis_install_page_write_cb,
                          page);
  return;

error:
  gis_store_set_error (error);
  gis_install_page_teardown (page);
}

static void
gis_install_page_prepare_write (GisPage *page)
{
  GisInstallPage *install = GIS_INSTALL_PAGE (page);
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) fd_index = NULL;
  UDisksBlock *block = UDISKS_BLOCK(gis_store_get_object(GIS_STORE_BLOCK_DEVICE));

  if (block == NULL)
    {
      g_autoptr(GError) error = g_error_new (GIS_INSTALL_ERROR, 0,
                                             _("Internal error"));
      g_warning ("gis_store_get_object(GIS_STORE_BLOCK_DEVICE) == NULL");
      gis_store_set_error (error);
      gis_install_page_teardown (GIS_PAGE (page));
    }
  else
    {
      udisks_block_call_open_for_restore (block,
                                          g_variant_new ("a{sv}", NULL), /* options */
                                          NULL, /* fd_list */
                                          NULL, /* cancellable */
                                          gis_install_page_open_for_restore_cb,
                                          install);
    }
}

static void
gis_install_page_shown (GisPage *page)
{
  GisInstallPage *install = GIS_INSTALL_PAGE (page);
  GisInstallPagePrivate *priv = gis_install_page_get_instance_private (install);

  gis_install_page_update_step (install, 1);
  priv->client = UDISKS_CLIENT (gis_store_get_object (GIS_STORE_UDISKS_CLIENT));

  if (gis_store_get_error () != NULL)
    {
      gis_assistant_next_page (gis_driver_get_assistant (page->driver));
      return;
    }

  /*
   * When the installer is in the middle of the copy operation, we have
   * to show a dialog asking the user if she ~really~ wants to quit the
   * application in the middle of a potentially dangerous operation.
   */
  g_signal_connect (gtk_widget_get_toplevel (GTK_WIDGET (page)),
                    "delete-event",
                    G_CALLBACK (delete_event_cb),
                    page);

  gis_install_page_prepare_write (page);
}

static void
gis_install_page_constructed (GObject *object)
{
  GisInstallPage *page = GIS_INSTALL_PAGE (object);

  G_OBJECT_CLASS (gis_install_page_parent_class)->constructed (object);

  gtk_container_add (GTK_CONTAINER (page), WID ("install-page"));

  gtk_progress_bar_set_fraction (OBJ (GtkProgressBar*, "install_progress"), 0.0);

  gtk_overlay_add_overlay (OBJ (GtkOverlay*, "graphics_overlay"), WID ("infolabels"));

  // XXX: FOR DEBUGGING, hide the buttons in final
  gis_page_set_complete (GIS_PAGE (page), FALSE);

  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_install_page_locale_changed (GisPage *page)
{
  gis_page_set_title (page, _("Reformatting"));
}

static void
gis_install_page_class_init (GisInstallPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  page_class->page_id = PAGE_ID;
  page_class->hide_forward_button = TRUE;
  page_class->hide_backward_button = TRUE;
  page_class->hide_window_controls = TRUE;
  page_class->locale_changed = gis_install_page_locale_changed;
  page_class->shown = gis_install_page_shown;
  object_class->constructed = gis_install_page_constructed;
}

static void
gis_install_page_init (GisInstallPage *page)
{
  g_resources_register (install_get_resource ());
}

void
gis_prepare_install_page (GisDriver *driver)
{
  gis_driver_add_page (driver,
                       g_object_new (GIS_TYPE_INSTALL_PAGE,
                                     "driver", driver,
                                     NULL));
}
