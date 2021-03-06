/*
 * Copyright (C) 2011 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Vincent Untz <vuntz@gnome.org>
 *
 * A small part of the code is based on the original xulrunner-based
 * gnome-web-photo (src/main.cpp), which was released under LGPLv2.1+. The
 * copyright of this code was:
 *   Copyright (C) 2005 Christian Persch
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#ifdef HAVE_GTK_UNIX_PRINT
#include <gtk/gtkunixprint.h>
#endif

#ifdef HAVE_GNOME3
#include <gio/gio.h>
#include <cairo/cairo-xlib.h>
#else
#include <gconf/gconf-client.h>
#endif

#include "photo-offscreen-window.h"

#ifdef HAVE_GNOME3
#define GSETTINGS_DESKTOP_INTERFACE  "org.gnome.desktop.interface"
#define GSETTINGS_VARIABLE_FONT_KEY  "document-font-name"
#define GSETTINGS_MONOSPACE_FONT_KEY "monospace-font-name"
#else
#define GCONF_VARIABLE_FONT_KEY      "/desktop/gnome/interface/document_font_name"
#define GCONF_MONOSPACE_FONT_KEY     "/desktop/gnome/interface/monospace_font_name"
#define GCONF_EPHY_MINIMUM_FONT_SIZE "/apps/epiphany/web/minimum_font_size"
#endif

#define DEFAULT_VARIABLE_FONT     "Sans 10"
#define DEFAULT_MONOSPACE_FONT    "Monospace 10"
#define DEFAULT_MINIMUM_SIZE      7

/* Value to keep in sync with the one from cairo:
 * MAX_IMAGE_SIZE in cairo-image-surface.c */
#define MAX_SIZE        32767

#define MIN_WIDTH       64
#define MAX_WIDTH       2048
#define DEFAULT_WIDTH   1024

#define DEFAULT_THUMBNAIL_SIZE  256

typedef enum
{
  MODE_PHOTO,
  MODE_THUMBNAIL,
  MODE_PRINT,
  MODE_DEBUG,
  MODE_LAST,
  MODE_INVALID = MODE_LAST
} PhotoMode;

static struct {
  const char *name;
  PhotoMode   mode;
} modes[] = {
  { "photo",     MODE_PHOTO     },
  { "thumbnail", MODE_THUMBNAIL },
  { "print",     MODE_PRINT     },
  { "debug",     MODE_DEBUG     }
};

static PhotoMode parsed_mode = MODE_INVALID;

typedef struct {
  char          *uri;
  char          *outfile;

  PhotoMode      mode;

  char          *user_css;
  int            width;
  int            thumbnail_size;
  gboolean       print_background;
  gboolean       disable_plugins;
  char          *printer;

  int            delay;
  int            timeout;
  gboolean       force;

  gboolean       error;

  GtkWidget     *window;
  WebKitWebView *webview;
  guint          idle_id;
  guint          delay_id;
  guint          timeout_id;
} PhotoData;


/******************\
 * Setup Web View *
\******************/

static gboolean
_on_new_window (WebKitWebView             *webview,
                WebKitWebFrame            *frame,
                WebKitNetworkRequest      *request,
                WebKitWebNavigationAction *action,
                WebKitWebPolicyDecision   *decision)
{
  webkit_web_policy_decision_ignore (decision);
  return TRUE;
}

static gboolean
_on_print_requested (WebKitWebView  *webview,
                     WebKitWebFrame *frame)
{
  return TRUE;
}

static gboolean
_on_console_message (WebKitWebView *webview,
                     const gchar   *message,
                     guint          line_number,
                     const gchar   *source_id)
{
  return TRUE;
}

static gboolean
_on_script_alert (WebKitWebView  *webview,
                  WebKitWebFrame *frame,
                  const gchar    *message)
{
  return TRUE;
}

static gboolean
_on_script_confirm (WebKitWebView  *webview,
                    WebKitWebFrame *frame,
                    const gchar    *message,
                    gboolean       *confirmed)
{
  *confirmed = FALSE;
  return TRUE;
}

static gboolean
_on_script_prompt (WebKitWebView   *webview,
                   WebKitWebFrame  *frame,
                   const gchar     *message,
                   const gchar     *default_txt,
                   const gchar    **text)
{
  *text = NULL;
  return TRUE;
}

static gboolean
_on_geolocation (WebKitWebView                   *webview,
                 WebKitWebFrame                  *frame,
                 WebKitGeolocationPolicyDecision *decision)
{
  webkit_geolocation_policy_deny (decision);
  return TRUE;
}

static void
_prepare_web_view (WebKitWebView *webview)
{
  g_signal_connect (webview, "new-window-policy-decision-requested",
                    G_CALLBACK (_on_new_window), NULL);
  g_signal_connect (webview, "print-requested",
                    G_CALLBACK (_on_print_requested), NULL);
  g_signal_connect (webview, "console-message",
                    G_CALLBACK (_on_console_message), NULL);
  g_signal_connect (webview, "script-alert",
                    G_CALLBACK (_on_script_alert), NULL);
  g_signal_connect (webview, "script-confirm",
                    G_CALLBACK (_on_script_confirm), NULL);
  g_signal_connect (webview, "script-prompt",
                    G_CALLBACK (_on_script_prompt), NULL);
  g_signal_connect (webview, "geolocation-policy-decision-requested",
                    G_CALLBACK (_on_geolocation), NULL);
}


/******************\
 * Setup Settings *
\******************/

static void
_parse_font (const char  *font_descr,
             const char  *default_descr,
             char       **font_name,
             int         *font_size)
{
  PangoFontDescription* pango_descr;

  if (!font_descr)
    font_descr = default_descr;

  pango_descr = pango_font_description_from_string (font_descr);

  if (!pango_descr && default_descr) {
    _parse_font (default_descr, NULL, font_name, font_size);
    return;
  }

  *font_name = g_strdup (pango_font_description_get_family (pango_descr));
  *font_size = pango_font_description_get_size (pango_descr);

  if (pango_font_description_get_size_is_absolute (pango_descr) == FALSE)
    *font_size /= PANGO_SCALE;

  pango_font_description_free (pango_descr);
}

static void
_prepare_web_settings (WebKitWebSettings *settings,
                       const char        *user_css,
                       gboolean           print_background,
                       gboolean           disable_plugins)
{
#ifdef HAVE_GNOME3
  GSettings *gsettings;
#else
  GConfClient *client;
#endif
  char *value;
  char *font_name = NULL;
  int   font_size = 0;

  /* Various settings */

  g_object_set (G_OBJECT (settings),
                /* printing settings */
                "print-backgrounds", print_background,
                /* enable/disable plugins */
                "enable-plugins", !disable_plugins,
                /* don't save anything from this to the global history */
                "enable-private-browsing", TRUE,
                /* shouldn't be needed */
                "enable-html5-database", FALSE,
                "enable-html5-local-storage", FALSE,
                /* no automatic popup or other similar behavior */
                "javascript-can-open-windows-automatically", FALSE,
                "auto-resize-window", FALSE,
                /* ensure secure settings */
                "javascript-can-access-clipboard", FALSE,
                "enable-universal-access-from-file-uris", FALSE,
                /* custom css */
                "user-stylesheet-uri", user_css,
                NULL);

  /* Fetch fonts from user config */

#ifdef HAVE_GNOME3
  gsettings = g_settings_new (GSETTINGS_DESKTOP_INTERFACE);
#else
  client = gconf_client_get_default ();
#endif

#ifdef HAVE_GNOME3
  value = g_settings_get_string (gsettings, GSETTINGS_VARIABLE_FONT_KEY);
#else
  value = gconf_client_get_string (client, GCONF_VARIABLE_FONT_KEY, NULL);
#endif
  _parse_font (value, DEFAULT_VARIABLE_FONT, &font_name, &font_size);
  g_free (value);

  g_object_set (G_OBJECT (settings),
                "default-font-family", font_name,
                "default-font-size", font_size,
                "sans-serif-font-family", font_name,
                NULL);
  g_free (font_name);

#ifdef HAVE_GNOME3
  value = g_settings_get_string (gsettings, GSETTINGS_MONOSPACE_FONT_KEY);
#else
  value = gconf_client_get_string (client, GCONF_MONOSPACE_FONT_KEY, NULL);
#endif
  _parse_font (value, DEFAULT_MONOSPACE_FONT, &font_name, &font_size);
  g_free (value);

  g_object_set (G_OBJECT (settings),
                "monospace-font-family", font_name,
                "default-monospace-font-size", font_size,
                NULL);
  g_free (font_name);

#ifdef HAVE_GNOME3
  /* We can't assume the GSettings schemas for epiphany are installed */
  font_size = DEFAULT_MINIMUM_SIZE;
#else
  font_size = gconf_client_get_int (client, GCONF_EPHY_MINIMUM_FONT_SIZE, NULL);
#endif
  if (font_size == 0)
    font_size = DEFAULT_MINIMUM_SIZE;

  g_object_set (G_OBJECT (settings),
                "minimum-font-size", font_size,
                NULL);

#ifdef HAVE_GNOME3
  g_object_unref (gsettings);
#else
  g_object_unref (client);
#endif
}


/*****************\
 * General Setup *
\*****************/

static void
_prepare_webkit (WebKitWebView     *webview,
                 WebKitWebSettings *settings,
                 const char        *user_css,
                 gboolean           print_background,
                 gboolean           disable_plugins)
{
  SoupSession* session = webkit_get_default_session();

  /* We don't want auth dialogs */
  soup_session_remove_feature_by_type (session, WEBKIT_TYPE_SOUP_AUTH_DIALOG);

  _prepare_web_view (webview);
  _prepare_web_settings (settings, user_css, print_background, disable_plugins);
}


/************\
 *   Core   *
\************/

static void
_write_photo (PhotoData *data)
{
#ifdef HAVE_GNOME3
  cairo_surface_t *surface;
  cairo_status_t   status;

  surface = gtk_offscreen_window_get_surface (GTK_OFFSCREEN_WINDOW (data->window));
  status = cairo_surface_write_to_png (surface, data->outfile);

  switch (status) {
    case CAIRO_STATUS_SUCCESS:
      break;
    default:
      data->error = TRUE;
      /* Translators: first %s is a URI */
      g_printerr (_("Error while saving '%s': %s\n"),
                  data->uri, cairo_status_to_string (status));
      break;
  }
#else
  GdkPixbuf *pixbuf;
  GError    *error = NULL;

  pixbuf = gtk_offscreen_window_get_pixbuf (GTK_OFFSCREEN_WINDOW (data->window));
  gdk_pixbuf_save (pixbuf, data->outfile, "png", &error, NULL);
  g_object_unref (pixbuf);

  if (error) {
    data->error = TRUE;
    /* Translators: first %s is a URI */
    g_printerr (_("Error while saving '%s': %s\n"),
                data->uri, error->message);
    g_error_free (error);
  }
#endif
}

static void
_write_thumbnail (PhotoData *data)
{
#ifdef HAVE_GNOME3
  cairo_surface_t *surface;
  cairo_surface_t *thumb_surface;
  cairo_t         *cr;
  cairo_status_t   status;
  int width;
  int height;
  int thumb_width;
  int thumb_height;

  surface = gtk_offscreen_window_get_surface (GTK_OFFSCREEN_WINDOW (data->window));
  width = cairo_xlib_surface_get_width (surface);
  height = cairo_xlib_surface_get_height (surface);

  /* Too tall? It'll be a square */
  if (height > width) {
    height = width;
  }

  thumb_width = data->thumbnail_size;
  thumb_height = (data->thumbnail_size * height) / (double) width;

  thumb_surface = cairo_surface_create_similar (surface, CAIRO_CONTENT_COLOR,
                                                thumb_width, thumb_height);

  cr = cairo_create (thumb_surface);
  cairo_scale (cr,
               data->thumbnail_size / (double) width,
               data->thumbnail_size / (double) width);
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);

  status = cairo_surface_write_to_png (thumb_surface, data->outfile);

  cairo_surface_destroy (thumb_surface);

  switch (status) {
    case CAIRO_STATUS_SUCCESS:
      break;
    default:
      data->error = TRUE;
      /* Translators: first %s is a URI */
      g_printerr (_("Error while thumbnailing '%s': %s\n"),
                  data->uri, cairo_status_to_string (status));
      break;
  }
#else
  GdkPixbuf *pixbuf;
  GdkPixbuf *thumb_pixbuf;
  GError    *error = NULL;
  int width;
  int height;
  int thumb_width;
  int thumb_height;

  pixbuf = gtk_offscreen_window_get_pixbuf (GTK_OFFSCREEN_WINDOW (data->window));
  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  /* Too tall? It'll be a square */
  if (height > width) {
    GdkPixbuf *subpixbuf;

    subpixbuf = gdk_pixbuf_new_subpixbuf (pixbuf, 0, 0, width, width);
    g_object_unref (pixbuf);

    pixbuf = subpixbuf;
    height = width;
  }

  thumb_width = data->thumbnail_size;
  thumb_height = (data->thumbnail_size * height) / (double) width;

  thumb_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
                                          thumb_width, thumb_height,
                                          GDK_INTERP_BILINEAR);

  gdk_pixbuf_save (thumb_pixbuf, data->outfile, "png", &error, NULL);

  g_object_unref (pixbuf);
  g_object_unref (thumb_pixbuf);

  if (error) {
    data->error = TRUE;
    /* Translators: first %s is a URI */
    g_printerr (_("Error while thumbnailing '%s': %s\n"),
                data->uri, error->message);
    g_error_free (error);
  }
#endif
}

static void
_print_photo (PhotoData *data)
{
  GtkPrintOperation *operation;
  WebKitWebFrame    *main_frame;
  GError            *error = NULL;

  main_frame = webkit_web_view_get_main_frame (data->webview);

#ifdef HAVE_GTK_UNIX_PRINT
  if (data->printer) {
    GtkPrintSettings  *settings;

    operation = gtk_print_operation_new ();
    settings = gtk_print_settings_new ();
    gtk_print_settings_set_printer (settings, data->printer);
    gtk_print_operation_set_print_settings (operation, settings);

    error = NULL;
    webkit_web_frame_print_full (main_frame, operation,
                                 GTK_PRINT_OPERATION_ACTION_PRINT, &error);

    g_object_unref (operation);

    if (error) {
      data->error = TRUE;
      /* Translators: first %s is a URI, second %s is a printer name */
      g_printerr (_("Error while printing '%s' on '%s': %s\n"),
                  data->uri, data->printer, error->message);
      g_error_free (error);
    }
  }
#endif

  if (data->outfile) {
    operation = gtk_print_operation_new ();
    gtk_print_operation_set_export_filename (operation, data->outfile);

    error = NULL;
    webkit_web_frame_print_full (main_frame, operation,
                                 GTK_PRINT_OPERATION_ACTION_EXPORT, &error);

    g_object_unref (operation);

    if (error) {
      data->error = TRUE;
      /* Translators: first %s is a URI */
      g_printerr (_("Error while printing '%s': %s\n"),
                  data->uri, error->message);
      g_error_free (error);
    }
  }
}

static gboolean
_on_window_deleted (GtkWidget *widget,
                    PhotoData *data)
{
  /* Don't destroy the window, it will be done while we go through the exit
   * path. This avoids having to check if it has already been destroyed during
   * that exit path. */

  gtk_main_quit ();

  return TRUE;
}

static void
_display_page (PhotoData *data)
{
  /* Make sure we display what we currently have, and don't do anything
   * additional */
  webkit_web_view_stop_loading (data->webview);

  g_signal_connect (data->window, "delete-event",
                    G_CALLBACK (_on_window_deleted), data);
  gtk_window_present (GTK_WINDOW (data->window));

  gtk_main ();
}

static void
_do_action (PhotoData *data)
{
  switch (data->mode) {
    case MODE_PHOTO:
      _write_photo (data);
      break;
    case MODE_THUMBNAIL:
      _write_thumbnail (data);
      break;
    case MODE_PRINT:
      _print_photo (data);
      break;
    case MODE_DEBUG:
      _display_page (data);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static gboolean
_on_delay (PhotoData *data)
{
  data->delay_id = 0;

  _do_action (data);
  gtk_main_quit ();

  return FALSE;
}

static gboolean
_on_web_view_load_error (WebKitWebView  *webview,
                         WebKitWebFrame *frame,
                         const char     *uri,
                         GError         *error,
                         PhotoData      *data)
{
  /* Cancelling is explicitly done by us, so we don't do anything here */
  if (error->domain == WEBKIT_NETWORK_ERROR &&
      error->code == WEBKIT_NETWORK_ERROR_CANCELLED)
    return TRUE;

  data->error = TRUE;
  /* Translators: first %s is a URI */
  g_printerr (_("Error while loading '%s': %s\n"), uri, error->message);

  gtk_main_quit ();

  return TRUE;
}

static gboolean
_web_view_loaded_idle (PhotoData *data)
{
  data->idle_id = 0;
  if (data->timeout_id > 0) {
    g_source_remove (data->timeout_id);
    data->timeout_id = 0;
  }

  if (data->delay > 0)
    data->delay_id = g_timeout_add_seconds (data->delay,
                                            (GSourceFunc) _on_delay, data);
  else
    _on_delay (data);

  return FALSE;
}

static void
_on_web_view_load_status (WebKitWebView *webview,
                          GParamSpec    *pspec,
                          PhotoData     *data)
{
  switch (webkit_web_view_get_load_status (webview)) {
    case WEBKIT_LOAD_FINISHED:
      /* For local files, we finish the load so fast that the page is not even
       * rendered. Going back to the idle loop fixes this. */
      g_assert (data->idle_id == 0);
      data->idle_id = g_idle_add ((GSourceFunc) _web_view_loaded_idle, data);
      break;

    case WEBKIT_LOAD_FAILED:
      /* Ignore since we'll have the load-error event */
      break;

    default:
      break;
  }
}

static gboolean
_on_timeout (PhotoData *data)
{
  gboolean quit = TRUE;

  data->timeout_id = 0;
  if (data->idle_id > 0) {
    g_source_remove (data->idle_id);
    data->idle_id = 0;
  }

  if (data->force) {
    switch (webkit_web_view_get_load_status (data->webview)) {
      case WEBKIT_LOAD_FIRST_VISUALLY_NON_EMPTY_LAYOUT:
      case WEBKIT_LOAD_FINISHED:
        /* Translators: first %s is a URI */
        g_printerr (_("Timed out while loading '%s'. Outputting current view...\n"), data->uri);

        if (data->delay > 0) {
          quit = FALSE;
          data->delay_id = g_timeout_add_seconds (data->delay,
                                                  (GSourceFunc) _on_delay, data);
        } else
          _do_action (data);

        break;

      default:
        data->error = TRUE;
        /* Translators: first %s is a URI */
        g_printerr (_("Timed out while loading '%s'. Nothing to output...\n"), data->uri);
        break;
    }
  } else {
    data->error = TRUE;
    /* Translators: first %s is a URI */
    g_printerr (_("Timed out while loading '%s'.\n"), data->uri);
  }

  /* We have to do it after checking the load status */
  webkit_web_view_stop_loading (data->webview);

  if (quit)
    gtk_main_quit ();

  return FALSE;
}

static GtkWidget *
_create_web_window (PhotoData *data)
{
  GtkWidget         *window;
  GtkWidget         *webview;
  WebKitWebSettings *settings;
  int                max_height;

  if (data->mode != MODE_DEBUG)
    window = photo_offscreen_window_new ();
  else
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  data->window = window;

  /* We don't specify a height: if we did so, we'd be forcing the height of the
   * output, which is not desirable since the page could be less tall than
   * that.
   * But on the other hand, if the page is too tall (eg, planet.gnome.org), we
   * can't handle it completely and we run out of memory before being able to
   * use it. This is why we use PhotoOffscreenWindow, which limits the maximum
   * height of the page.
   * This means we won't have the whole page "displayed" in the offscreen
   * window if it's too tall. This is not an issue for MODE_THUMBNAIL (we don't
   * need the whole page), nor for MODE_PRINT (the print operation is not
   * related to what is displayed). So it only affects MODE_PHOTO. But it's
   * better than not getting anything anyway. */
  gtk_widget_set_size_request (window, data->width, -1);

  if (data->mode != MODE_DEBUG) {
    /* For thumbnails, there is no point in having a height larger than the
     * width, so minimize directly what we need */
    max_height = MAX_SIZE;
    if (data->mode == MODE_THUMBNAIL)
      max_height = MIN (data->width, max_height);
    photo_offscreen_window_set_max_height (PHOTO_OFFSCREEN_WINDOW (window),
                                           max_height);
  }

  webview = webkit_web_view_new ();
  data->webview = WEBKIT_WEB_VIEW (webview);

  settings = webkit_web_settings_new ();

  _prepare_webkit (data->webview, settings,
                   data->user_css,
                   data->print_background,
                   data->disable_plugins);

  webkit_web_view_set_settings (data->webview, settings);

  gtk_container_add (GTK_CONTAINER(window), webview);
  gtk_widget_show_all (window);

  /* In MODE_DEBUG, we only want to show the window when it's ready. We still
   * use gtk_widget_show_all() before as it seems to make webkit happier. */
  if (data->mode == MODE_DEBUG)
    gtk_widget_hide (window);

  g_signal_connect (webview, "load-error",
                    G_CALLBACK (_on_web_view_load_error), data);
  g_signal_connect (webview, "notify::load-status",
                    G_CALLBACK (_on_web_view_load_status), data);

  if (data->timeout > 0)
    data->timeout_id = g_timeout_add_seconds (data->timeout,
                                              (GSourceFunc) _on_timeout, data);

  webkit_web_view_open (data->webview, data->uri);

  return window;
}


/************************\
 * Command-line options *
\************************/

static gboolean
_parse_mode (const gchar  *option_name,
             const gchar  *value,
             gpointer      data,
             GError      **error)
{
  g_assert (value != NULL);

  guint i;
  for (i = 0; i <= G_N_ELEMENTS (modes); i++) {
    if (g_ascii_strcasecmp (value, modes[i].name) == 0) {
      parsed_mode = modes[i].mode;
      break;
    }
  }

  if (i == MODE_INVALID) {
    *error = g_error_new (G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                          _("Unknown mode '%s'"), value);
    return FALSE;
  }

  return TRUE;
}

static void
_print_synopsis (void)
{
  const char *name = g_get_prgname ();
  g_assert (name != NULL);

  g_print (_("Usage: %s [--mode=photo|thumbnail|print] [...]\n"), name);

  switch (parsed_mode) {
    case MODE_PHOTO:
      g_print (_("Usage: %s [-c CSSFILE] [-d DELAY] [-t TIMEOUT] [--force] [-w WIDTH] [--file] URI|FILE OUTFILE\n"), name);
      break;
    case MODE_THUMBNAIL:
      g_print (_("Usage: %s [-c CSSFILE] [-d DELAY] [-t TIMEOUT] [--force] [-w WIDTH] [-s THUMBNAILSIZE] [--file] URI|FILE OUTFILE\n"), name);
      break;
    case MODE_PRINT:
#ifdef HAVE_GTK_UNIX_PRINT
      g_print (_("Usage: %s [-c CSSFILE] [-d DELAY] [-t TIMEOUT] [--force] [-w WIDTH] [--print-background] [--file] URI|FILE OUTFILE|--printer=PRINTER\n"), name);
#else
      g_print (_("Usage: %s [-c CSSFILE] [-d DELAY] [-t TIMEOUT] [--force] [-w WIDTH] [--print-background] [--file] URI|FILE OUTFILE\n"), name);
#endif
      break;
    default:
      break;
  }
}

#ifdef HAVE_GTK_UNIX_PRINT
static gboolean
_validate_printer (GtkPrinter *printer,
                   PhotoData  *data)
{
  const char *name;

  name = gtk_printer_get_name (printer);
  if (g_strcmp0 (data->printer, name) == 0) {
    data->error = FALSE;
    return TRUE;
  }

  return FALSE;
}

static gboolean
_print_printer_name (GtkPrinter *printer,
                     PhotoData  *data)
{
  GtkPrintBackend *backend;
  const char      *backend_name;

  /* Don't list the "Print to File" printer */
  backend = gtk_printer_get_backend (printer);
  backend_name = G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (backend));
  if (g_strcmp0 (backend_name, "GtkPrintBackendFile") == 0)
    return FALSE;

  if (gtk_printer_is_active (printer))
    /* Translators: the leading spaces are a way to add tabulation in some text
     * printed on the terminal; %s is the name of a printer. */
    g_printerr (_("  %s\n"), gtk_printer_get_name (printer));
  else
    /* Translators: the leading spaces are a way to add tabulation in some text
     * printed on the terminal; %s is the name of a printer; "active" applies
     * to the printer. */
    g_printerr (_("  %s (not active)\n"), gtk_printer_get_name (printer));

  return FALSE;
}
#endif

int
main (int    argc,
      char **argv)
{
  GOptionContext  *context;
  GError          *error = NULL;
  char           **arguments = NULL;
  gboolean         is_file = FALSE;
  GtkWidget       *window;
  char            *user_css_path = NULL;
  int              required_argc;

  PhotoData data = { NULL, NULL,
                     MODE_INVALID,
                     /* thumbnail_size set to -1 to be able to check if option
                      * was passed or not */
                     NULL, DEFAULT_WIDTH, -1, FALSE, FALSE, NULL,
                     0, 60, FALSE,
                     FALSE,
                     NULL, NULL, 0, 0, 0 };

  const GOptionEntry main_options[] =
  {
    { "mode", 'm', 0, G_OPTION_ARG_CALLBACK, (void*) _parse_mode,
      N_("Operation mode [photo|thumbnail|print]"), NULL },
    { "user-css", 'c', 0, G_OPTION_ARG_FILENAME, &user_css_path,
      N_("User style sheet to use for the page (default: " PKGDATADIR "/style.css)"),
      /* Translators: CSSFILE will appear in the help, as in: --user-css=CSSFILE */
      N_("CSSFILE") },
    { "delay", 'd', 0, G_OPTION_ARG_INT, &data.delay,
      N_("Delay in seconds to wait after page is loaded, or 0 to disable delay (default: 0)"),
      /* Translators: T will appear in the help, as in: --delay=D */
      N_("D") },
    { "timeout", 't', 0, G_OPTION_ARG_INT, &data.timeout,
      N_("Timeout in seconds to wait to load the page, or 0 to disable timeout (default: 60)"),
      /* Translators: T will appear in the help, as in: --timeout=T */
      N_("T") },
    { "force", 'f', 0, G_OPTION_ARG_NONE, &data.force,
      N_("Force output when timeout expires, even if the page is not fully loaded"), NULL },
    { "width", 'w', 0, G_OPTION_ARG_INT, &data.width,
      N_("Desired width of the web page (default: 1024)"),
      /* Translators: W will appear in the help, as in: --width=W */
      N_("W") },
    { "thumbnail-size", 's', 0, G_OPTION_ARG_INT, &data.thumbnail_size,
      N_("Thumbnail size (default: 256)"),
      /* Translators: S will appear in the help, as in: --thumbnail-size=S */
      N_("S") },
#ifdef HAVE_GTK_UNIX_PRINT
    { "printer", 'p', 0, G_OPTION_ARG_STRING, &data.printer,
      N_("Print page on PRINTER (default: none, save as PDF)"),
      /* Translators: PRINTER will appear in the help, as in: --printer=PRINTER */
      N_("PRINTER") },
#endif
    { "print-background", 0, 0, G_OPTION_ARG_NONE, &data.print_background,
      N_("Print background images and colours (default: false)"), NULL },
    { "disable-plugins", 0, 0, G_OPTION_ARG_NONE, &data.disable_plugins,
      N_("Disable embedded plugins in the rendering engine (default: enable plugins)"), NULL },
    { "file", 0, 0, G_OPTION_ARG_NONE, &is_file,
      N_("Argument is a file and not a URI"), NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &arguments, "", NULL },
    { NULL }
  };

#ifdef ENABLE_NLS
  /* Initialize the i18n stuff */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  /* Now parse the arguments */
  context = g_option_context_new (NULL);
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
  g_option_context_add_main_entries (context, main_options, GETTEXT_PACKAGE);

  /* This will initialize GTK+ too */
  g_option_context_add_group (context, gtk_get_option_group (TRUE));

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    g_error_free (error);
    g_option_context_free (context);
    _print_synopsis ();
    return 1;
  }
  g_option_context_free (context);

  data.mode = parsed_mode;

  /* Mode not explicitly specified, derive from invocation filename */
  if (data.mode == MODE_INVALID) {
    const char *program = g_get_prgname ();
    g_assert (program != NULL);

    if (g_ascii_strcasecmp (program, "gnome-web-thumbnail") == 0) {
      data.mode = MODE_THUMBNAIL;
    } else if (g_ascii_strcasecmp (program, "gnome-web-print") == 0) {
      data.mode = MODE_PRINT;
    } else {
      data.mode = MODE_PHOTO;
    }
  }

  /* Renice when thumbnailing, since it is likely that we are called multiple
   * times in sequence in order to thumbnail a bunch of HTML files. */
  if (data.mode == MODE_THUMBNAIL) {
    nice (5);
  }

  /* Check user css */
  if (user_css_path) {
    GFile *file;
    char  *path;

    file = g_file_new_for_commandline_arg (user_css_path);
    data.user_css = g_file_get_uri (file);
    path = g_file_get_path (file);

    if (g_file_is_native (file) &&
        !g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
      g_object_unref (file);
      g_free (path);
      /* Translators: %s is a filename or a URI */
      g_printerr (_("Specified user stylesheet ('%s') does not exist!\n"),
                  user_css_path);
      _print_synopsis ();
      return 1;
    }

    g_object_unref (file);
    g_free (path);
  } else {
    GFile *file;
    char  *path;

    path = g_build_filename (PKGDATADIR, "style.css", NULL);
    file = g_file_new_for_path (path);
    data.user_css = g_file_get_uri (file);
    g_free (path);
    g_object_unref (file);
  }

  /* Check delay */
  if (data.delay < 0) {
    g_printerr (_("--delay cannot be negative!\n"));
    _print_synopsis ();
    return 1;
  }

  /* Check timeout */
  if (data.timeout < 0) {
    g_printerr (_("--timeout cannot be negative!\n"));
    _print_synopsis ();
    return 1;
  }

  /* Check thumbnail size */
  if (data.mode == MODE_THUMBNAIL) {
    if (data.thumbnail_size == -1)
      data.thumbnail_size = DEFAULT_THUMBNAIL_SIZE;

    if (data.thumbnail_size != 32 &&
        data.thumbnail_size != 64 &&
        data.thumbnail_size != 96 &&
        data.thumbnail_size != 128 &&
        data.thumbnail_size != 256) {
      g_printerr (_("--size can only be 32, 64, 96, 128 or 256!\n"));
      return 1;
    }
  } else if (data.thumbnail_size != -1) {
    g_printerr (_("--size is only available in thumbnail mode!\n"));
    _print_synopsis ();
    return 1;
  }

  g_assert (data.thumbnail_size < MAX_SIZE);

  /* Check width */
  if (data.width < MIN_WIDTH || data.width > MAX_WIDTH) {
    g_printerr (_("--width out of bounds; must be between %d and %d!\n"), MIN_WIDTH, MAX_WIDTH);
    return 1;
  }

  if (data.mode == MODE_THUMBNAIL && (data.width % 32) != 0) {
    g_printerr (_("--width must be a multiple of 32 in thumbnail mode!\n"));
    return 1;
  }

  g_assert (data.width < MAX_SIZE);

  /* Check --print-background */
  if (data.mode != MODE_PRINT && data.print_background) {
    g_printerr (_("--print-background is only available in print mode!\n"));
    _print_synopsis ();
    return 1;
  }

#ifdef HAVE_GTK_UNIX_PRINT
  /* Check --printer */
  if (data.mode != MODE_PRINT && data.printer) {
    g_printerr (_("--printer is only available in print mode!\n"));
    _print_synopsis ();
    return 1;
  } else if (data.mode == MODE_PRINT && data.printer) {
    data.error = TRUE;
    gtk_enumerate_printers ((GtkPrinterFunc) _validate_printer,
                            &data, NULL, TRUE);
    if (data.error) {
      /* Translators: %s is the name of a printer */
      g_printerr (_("'%s' is not a valid printer!\n"), data.printer);
      g_printerr ("\n");
      g_printerr (_("List of printers:\n"));
      gtk_enumerate_printers ((GtkPrinterFunc) _print_printer_name,
                              &data, NULL, TRUE);
      return 1;
    }
  }
#endif

  switch (data.mode) {
    case MODE_PHOTO:
    case MODE_THUMBNAIL:
      /* we only need a URI and a output file */
      required_argc = 2;
      break;
    case MODE_PRINT:
      /* we need a URI, and if --printer is not passed, a output file */
#ifdef HAVE_GTK_UNIX_PRINT
      if (data.printer)
        required_argc = 1;
      else
#endif
        required_argc = 2;
      break;
    case MODE_DEBUG:
      /* we only need a URI */
      required_argc = 1;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  /* Check uri/output */
  if (!arguments || g_strv_length (arguments) < required_argc) {
    g_printerr (_("Missing arguments!\n"));
    _print_synopsis ();
    return 1;
  }

  if (g_strv_length (arguments) > required_argc) {
    g_printerr (_("Too many arguments!\n"));
    _print_synopsis ();
    return 1;
  }

  /* Replace filename with URI */
  if (is_file) {
    GFile *file;

    file = g_file_new_for_commandline_arg (arguments[0]);

    g_free (arguments[0]);
    arguments[0] = g_file_get_uri (file);

    g_object_unref (file);
  }

  /* Now let's do the work! */
  data.uri = arguments[0];
  if (required_argc == 2)
    data.outfile = arguments[1];

  window = _create_web_window (&data);

  gtk_main ();

  gtk_widget_destroy (window);

  return data.error ? 1 : 0;
}
