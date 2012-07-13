/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "config.h"
#include "gis-summary-page.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include <gdm-greeter-client.h>

#define OBJ(type,name) ((type)gtk_builder_get_object(builder,(name)))
#define WID(name) OBJ(GtkWidget*,name)

static GdmGreeterClient *
connect_to_slave (void)
{
  GError *error = NULL;
  gboolean res;
  GdmGreeterClient *greeter_client;

  greeter_client = gdm_greeter_client_new ();

  res = gdm_greeter_client_open_connection (greeter_client, &error);

  if (!res) {
    g_warning ("Failed to open connection to slave: %s", error->message);
    g_error_free (error);
    g_object_unref (greeter_client);
    return NULL;
  }

  return greeter_client;
}

static void
on_ready_for_auto_login (GdmGreeterClient *client,
                         const char       *service_name,
                         SetupData        *setup)
{
  /* const gchar *username; */

  /* username = act_user_get_user_name (gis_get_act_user (setup)); */

  /* g_debug ("Initiating autologin for %s", username); */
  /* gdm_greeter_client_call_begin_auto_login (client, username); */
  /* gdm_greeter_client_call_start_session_when_ready (client, */
  /*                                                   service_name, */
  /*                                                   TRUE); */
}

static void
begin_autologin (SetupData *setup)
{
  GdmGreeterClient *greeter_client = connect_to_slave ();

  if (greeter_client == NULL) {
    g_warning ("No slave connection; not initiating autologin");
    return;
  }

  g_debug ("Preparing to autologin");

  g_signal_connect (greeter_client,
                    "ready",
                    G_CALLBACK (on_ready_for_auto_login),
                    setup);
  gdm_greeter_client_call_start_conversation (greeter_client, "gdm-autologin");
}

static void
byebye_cb (GtkButton *button, SetupData *setup)
{
  begin_autologin (setup);
}

static void
tour_cb (GtkButton *button, SetupData *setup)
{
  /* the tour is triggered by /tmp/run-welcome-tour */
  g_file_set_contents ("/tmp/run-welcome-tour", "yes", -1, NULL);
  begin_autologin (setup);
}

void
gis_prepare_summary_page (SetupData *setup)
{
  GtkWidget *button;
  GKeyFile *overrides = gis_get_overrides (setup);
  gchar *s;
  GisAssistant *assistant = gis_get_assistant (setup);
  GtkBuilder *builder = gis_builder ("gis-summary-page");

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-title",
                                    NULL, NULL);
  if (s)
    gtk_label_set_text (GTK_LABEL (WID ("summary-title")), s);
  g_free (s);

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-details",
                                    NULL, NULL);
  if (s) {
    gtk_label_set_text (GTK_LABEL (WID ("summary-details")), s);
  }
  g_free (s);

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-details2",
                                    NULL, NULL);
  if (s)
    gtk_label_set_text (GTK_LABEL (WID ("summary-details2")), s);
  g_free (s);

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-start-button",
                                    NULL, NULL);
  if (s)
    gtk_button_set_label (GTK_BUTTON (WID ("summary-start-button")), s);
  g_free (s);

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-tour-details",
                                    NULL, NULL);
  if (s)
    gtk_label_set_text (GTK_LABEL (WID ("summary-tour-details")), s);
  g_free (s);

  s = g_key_file_get_locale_string (overrides,
                                    "Summary", "summary-tour-button",
                                    NULL, NULL);
  if (s)
    gtk_button_set_label (GTK_BUTTON (WID ("summary-tour-button")), s);
  g_free (s);

  button = WID("summary-start-button");
  g_signal_connect (button, "clicked",
                    G_CALLBACK (byebye_cb), setup);
  button = WID("summary-tour-button");
  g_signal_connect (button, "clicked",
                    G_CALLBACK (tour_cb), setup);

  g_object_set_data (OBJ (GObject *, "summary-page"), "gis-page-title", _("Thank You"));
  g_object_set_data (OBJ (GObject *, "summary-page"), "gis-summary", GUINT_TO_POINTER (TRUE));
  gis_assistant_add_page (assistant, WID ("summary-page"));
  gis_assistant_set_page_complete (assistant, WID ("summary-page"), TRUE);
}