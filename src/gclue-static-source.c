/* vim: set et ts=8 sw=8: */
/*
 * Copyright © 2022,2023 Oracle and/or its affiliates.
 *
 * Geoclue is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Geoclue is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Geoclue; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include <gio/gio.h>
#include "gclue-location.h"
#include "gclue-static-source.h"
#include "config.h"
#include "gclue-enum-types.h"

#define GEO_FILE_NAME "geolocation"
#define GEO_FILE_PATH SYSCONFDIR "/" GEO_FILE_NAME

/* Rate limit of geolocation file monitoring.
 * In milliseconds.
 */
#define GEO_FILE_MONITOR_RATE_LIMIT 2500

struct _GClueStaticSource {
        /* <private> */
        GClueLocationSource parent_instance;
};

typedef struct {
        GClueLocation *location;
        guint location_set_timer;

        GFileMonitor *monitor;

        GCancellable *cancellable;
        gboolean file_open_quiet;
        GFileInputStream *file_stream;
        GDataInputStream *data_stream;
        enum { L_LAT = 0, L_LON, L_ALT, L_ACCURACY } file_line;
        gdouble latitude;
        gdouble longitude;
        gdouble altitude;
} GClueStaticSourcePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GClueStaticSource,
                            gclue_static_source,
                            GCLUE_TYPE_LOCATION_SOURCE)

static void
file_read_next_line (GClueStaticSource *source);

static GClueStaticSourcePrivate *
get_priv (GClueStaticSource *source)
{
        return gclue_static_source_get_instance_private (source);
}

static void
update_accuracy (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);
        GClueAccuracyLevel level_old, level_new;

        if (!priv->location) {
                level_new = GCLUE_ACCURACY_LEVEL_NONE;
        } else {
                gboolean scramble_location;

                g_object_get (G_OBJECT(source), "scramble-location",
                              &scramble_location, NULL);
                if (scramble_location) {
                        level_new = GCLUE_ACCURACY_LEVEL_CITY;
                } else {
                        level_new = GCLUE_ACCURACY_LEVEL_EXACT;
                }
        }

        level_old = gclue_location_source_get_available_accuracy_level
                (GCLUE_LOCATION_SOURCE (source));
        if (level_new == level_old)
                return;

        g_debug ("Available accuracy level from %s: %u",
                 G_OBJECT_TYPE_NAME (source), level_new);
        g_object_set (G_OBJECT (source),
                      "available-accuracy-level", level_new,
                      NULL);
}


static gboolean
on_location_set_timer (gpointer user_data)
{
        GClueStaticSource *source = GCLUE_STATIC_SOURCE (user_data);
        GClueStaticSourcePrivate *priv = get_priv (source);
        g_autoptr(GClueLocation) prev_location = NULL;

        priv->location_set_timer = 0;

        g_assert (priv->location);
        prev_location = g_steal_pointer (&priv->location);
        priv->location = gclue_location_duplicate_fresh (prev_location);
        gclue_location_source_set_location
                (GCLUE_LOCATION_SOURCE (source), priv->location);

        return G_SOURCE_REMOVE;
}

static void
location_set_refresh_timer (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);

        if (!priv->location) {
                if (priv->location_set_timer) {
                        g_debug ("Removing static location set timer due to no location");
                        g_clear_handle_id (&priv->location_set_timer,
                                           g_source_remove);
                }

                return;
        }

        if (priv->location_set_timer) {
                return;
        }

        g_debug ("Scheduling static location set timer");
        priv->location_set_timer = g_idle_add (on_location_set_timer,
                                               source);
}

static void
location_updated (GClueStaticSource *source)
{
        /* Update accuracy first so locators can connect or disconnect
         * from our source accordingly before getting the new location.
         */
        update_accuracy (source);

        location_set_refresh_timer (source);
}

static void
close_file (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);

        if (!priv->cancellable)
                return;

        g_cancellable_cancel (priv->cancellable);

        g_clear_object (&priv->data_stream);
        g_clear_object (&priv->file_stream);
        g_clear_object (&priv->cancellable);
}

static void
close_file_clear_location (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);

        close_file (source);

        if (!priv->location)
                return;

        g_debug ("Static source clearing location");
        g_clear_object (&priv->location);
        location_updated (source);
}

static void
gclue_static_source_finalize (GObject *gstatic)
{
        GClueStaticSource *source = GCLUE_STATIC_SOURCE (gstatic);
        GClueStaticSourcePrivate *priv = get_priv (source);

        G_OBJECT_CLASS (gclue_static_source_parent_class)->finalize (gstatic);

        close_file (source);

        g_clear_object (&priv->location);
        g_clear_handle_id (&priv->location_set_timer, g_source_remove);

        g_clear_object (&priv->monitor);
}

static GClueLocationSourceStartResult
gclue_static_source_start (GClueLocationSource *source)
{
        GClueLocationSourceClass *base_class;
        GClueLocationSourceStartResult base_result;

        g_return_val_if_fail (GCLUE_IS_STATIC_SOURCE (source),
                              GCLUE_LOCATION_SOURCE_START_RESULT_FAILED);

        base_class = GCLUE_LOCATION_SOURCE_CLASS (gclue_static_source_parent_class);
        base_result = base_class->start (source);
        if (base_result != GCLUE_LOCATION_SOURCE_START_RESULT_OK)
                return base_result;

        /* Set initial location */
        location_set_refresh_timer (GCLUE_STATIC_SOURCE (source));

        return base_result;
}

static void
gclue_static_source_class_init (GClueStaticSourceClass *klass)
{
        GClueLocationSourceClass *source_class = GCLUE_LOCATION_SOURCE_CLASS (klass);
        GObjectClass *gstatic_class = G_OBJECT_CLASS (klass);

        gstatic_class->finalize = gclue_static_source_finalize;

        source_class->start = gclue_static_source_start;
}

static void
on_line_read (GObject *object,
              GAsyncResult *result,
              gpointer user_data)
{
        GDataInputStream *data_stream = G_DATA_INPUT_STREAM (object);
        GClueStaticSource *source;
        GClueStaticSourcePrivate *priv;
        g_autoptr(GError) error = NULL;
        g_autofree char *line = NULL;
        char *comment_start;
        gdouble accuracy;

        line = g_data_input_stream_read_line_finish (data_stream, result,
                                                     NULL, &error);
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                return;
        }

        source = GCLUE_STATIC_SOURCE (user_data);
        priv = get_priv (source);

        if (line == NULL) {
                if (error != NULL) {
                        g_warning ("Static source error when reading file: %s",
                                   error->message);
                } else {
                        g_warning ("Static source unexpected EOF reading file (truncated?)");
                }

                close_file_clear_location (source);
                return;
        }

        comment_start = strchr (line, '#');
        if (comment_start) {
                *comment_start = '\0';
        }

        g_strstrip (line);

        if (strlen (line) == 0) {
                file_read_next_line (source);
                return;
        }

        do {
                gdouble * const coords[] = {
                        /* L_LAT */ &priv->latitude,
                        /* L_LON */ &priv->longitude,
                        /* L_ALT */ &priv->altitude,
                        /* L_ACCURACY */ &accuracy,
                };
                char *endptr;

                g_assert (priv->file_line >= L_LAT &&
                          priv->file_line <= L_ACCURACY);

                *coords[priv->file_line] = g_ascii_strtod (line, &endptr);
                if (errno != 0 || *endptr != '\0') {
                        g_warning ("Static source invalid line %d '%s'",
                                   priv->file_line, line);
                        close_file_clear_location (source);
                        return;
                }
        } while (FALSE);

        if (priv->file_line < L_ACCURACY) {
                priv->file_line++;
                file_read_next_line (source);
                return;
        }

        close_file (source);

        g_debug ("Static source read a new location");
        g_clear_object (&priv->location);
        priv->location = gclue_location_new_full (priv->latitude,
                                                  priv->longitude,
                                                  accuracy,
                                                  GCLUE_LOCATION_SPEED_UNKNOWN,
                                                  GCLUE_LOCATION_HEADING_UNKNOWN,
                                                  priv->altitude,
                                                  0, "Static location");
        g_assert (priv->location);
        location_updated (source);
}

static void
file_read_next_line (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);

        g_assert (priv->data_stream);
        g_data_input_stream_read_line_async (priv->data_stream,
                                             G_PRIORITY_DEFAULT,
                                             priv->cancellable,
                                             on_line_read, source);
}

static void
on_file_open (GObject *source_object,
              GAsyncResult *res,
              gpointer user_data)
{
        GFile *file = G_FILE (source_object);
        GClueStaticSource *source;
        GClueStaticSourcePrivate *priv;
        g_autoptr(GFileInputStream) file_stream = NULL;
        g_autoptr(GError) error = NULL;

        file_stream = g_file_read_finish (file, res, &error);
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                return;
        }

        source = GCLUE_STATIC_SOURCE (user_data);
        priv = get_priv (source);

        if (error != NULL) {
                if (!priv->file_open_quiet) {
                        g_autofree char *parsename = NULL;

                        parsename = g_file_get_parse_name (file);
                        g_warning ("Static source failed to open '%s': %s",
                                   parsename, error->message);
                }

                close_file_clear_location (source);
                return;
        }

        g_return_if_fail (file_stream != NULL);
        g_assert (!priv->file_stream);
        priv->file_stream = g_steal_pointer (&file_stream);

        g_assert (!priv->data_stream);
        priv->data_stream = g_data_input_stream_new
                (G_INPUT_STREAM (priv->file_stream));

        priv->file_line = L_LAT;
        file_read_next_line (source);
}

static void
open_file (GClueStaticSource *source, GFile *file, gboolean quiet)
{
        GClueStaticSourcePrivate *priv = get_priv (source);

        close_file (source);

        priv->cancellable = g_cancellable_new ();
        priv->file_open_quiet = quiet;
        g_file_read_async (file, G_PRIORITY_DEFAULT, priv->cancellable,
                           on_file_open, source);
}

static void
on_monitor_event (GFileMonitor *monitor,
                  GFile *file,
                  GFile *other_file,
                  GFileMonitorEvent event_type,
                  gpointer user_data)
{
        GClueStaticSource *source = GCLUE_STATIC_SOURCE (user_data);
        g_autofree char *basename = NULL;

        if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
            event_type != G_FILE_MONITOR_EVENT_DELETED) {
                return;
        }

        g_return_if_fail (file != NULL);
        basename = g_file_get_basename (file);
        if (basename == NULL ||
            strcmp (basename, GEO_FILE_NAME) != 0) {
                return;
        }

        if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
                g_debug ("Static source trying to re-load since " GEO_FILE_PATH " has changed");
                open_file (source, file, FALSE);
        } else { /* G_FILE_MONITOR_EVENT_DELETED */
                g_debug ("Static source flushing location since " GEO_FILE_PATH " was deleted");
                close_file_clear_location (source);
        }
}

static void
check_monitor (GClueStaticSource *source)
{
        GClueStaticSourcePrivate *priv = get_priv (source);
        g_autoptr(GFile) geo_file = NULL;
        g_autoptr(GError) error = NULL;

        if (priv->monitor)
                return;

        geo_file = g_file_new_for_path (GEO_FILE_PATH);
        priv->monitor = g_file_monitor_file (geo_file, G_FILE_MONITOR_NONE,
                                             NULL, &error);
        if (error != NULL) {
                g_warning ("Static source failed to monitor '" GEO_FILE_PATH "': %s",
                           error->message);
                g_clear_object (&priv->monitor);
                return;
        }

        g_assert (priv->monitor);
        g_file_monitor_set_rate_limit (priv->monitor,
                                       GEO_FILE_MONITOR_RATE_LIMIT);
        g_signal_connect_object (G_OBJECT (priv->monitor), "changed",
                                 G_CALLBACK (on_monitor_event),
                                 source, 0);

        g_debug ("Static source monitoring '" GEO_FILE_PATH "', trying initial load");
        open_file (source, geo_file, TRUE);
}

static void
gclue_static_source_init (GClueStaticSource *source)
{
        check_monitor (source);
}

/**
 * gclue_static_source_get_singleton:
 *
 * Get the #GClueStaticSource singleton, for the specified max accuracy
 * level @level.
 *
 * Returns: (transfer full): a new ref to #GClueStaticSource. Use g_object_unref()
 * when done.
 **/
GClueStaticSource *
gclue_static_source_get_singleton (GClueAccuracyLevel level)
{
        static GClueStaticSource *source[] = { NULL, NULL };
        gboolean is_exact;
        int i;

        g_return_val_if_fail (level >= GCLUE_ACCURACY_LEVEL_CITY, NULL);
        is_exact = level == GCLUE_ACCURACY_LEVEL_EXACT;

        i = is_exact ? 0 : 1;
        if (source[i] == NULL) {
                source[i] = g_object_new (GCLUE_TYPE_STATIC_SOURCE,
                                          "compute-movement", FALSE,
                                          "scramble-location", !is_exact,
                                          NULL);
                g_object_add_weak_pointer (G_OBJECT (source[i]),
                                           (gpointer) &source[i]);
        } else {
                g_object_ref (source[i]);
                check_monitor (source[i]);
        }

        return source[i];
}
