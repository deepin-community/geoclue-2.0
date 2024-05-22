/* vim: set et ts=8 sw=8: */
/* gclue-config.c
 *
 * Copyright 2013 Red Hat, Inc.
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
 * Authors: Zeeshan Ali (Khattak) <zeeshanak@gnome.org>
 */

#include <glib/gi18n.h>
#include <config.h>
#include <string.h>

#include "gclue-config.h"

#define CONFIG_FILE_PATH SYSCONFDIR "/geoclue/geoclue.conf"
#define CONFIG_D_DIRECTORY SYSCONFDIR "/geoclue/conf.d/"

/* This class will be responsible for fetching configuration. */

struct _GClueConfigPrivate
{
        GKeyFile *key_file;

        char **agents;
        gsize num_agents;

        char *wifi_url;
        gboolean wifi_submit;
        gboolean enable_nmea_source;
        gboolean enable_3g_source;
        gboolean enable_cdma_source;
        gboolean enable_modem_gps_source;
        gboolean enable_wifi_source;
        gboolean enable_compass;
        gboolean enable_static_source;
        char *wifi_submit_url;
        char *wifi_submit_nick;
        char *nmea_socket;

        GList *app_configs;
};

G_DEFINE_TYPE_WITH_CODE (GClueConfig,
                         gclue_config,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GClueConfig))

typedef struct
{
        char *id;
        gboolean allowed;
        gboolean system;
        int* users;
        gsize num_users;
} AppConfig;

static void
app_config_free (AppConfig *app_config)
{
        g_free (app_config->id);
        g_free (app_config->users);
        g_slice_free (AppConfig, app_config);
}

static void
gclue_config_finalize (GObject *object)
{
        GClueConfigPrivate *priv;

        priv = GCLUE_CONFIG (object)->priv;

        g_clear_pointer (&priv->key_file, g_key_file_unref);
        g_clear_pointer (&priv->agents, g_strfreev);
        g_clear_pointer (&priv->wifi_url, g_free);
        g_clear_pointer (&priv->wifi_submit_url, g_free);
        g_clear_pointer (&priv->wifi_submit_nick, g_free);
        g_clear_pointer (&priv->nmea_socket, g_free);

        g_list_foreach (priv->app_configs, (GFunc) app_config_free, NULL);

        G_OBJECT_CLASS (gclue_config_parent_class)->finalize (object);
}

static void
gclue_config_class_init (GClueConfigClass *klass)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gclue_config_finalize;
}

static void
load_agent_config (GClueConfig *config, gboolean initial)
{
        GClueConfigPrivate *priv = config->priv;
        g_autoptr(GError) error = NULL;
        g_auto(GStrv) agents = NULL;
        gsize num_agents;

        if (!initial && !g_key_file_has_key (priv->key_file, "agent", "whitelist", NULL))
                return;

        agents = g_key_file_get_string_list (priv->key_file,
                                             "agent",
                                             "whitelist",
                                             &num_agents,
                                             &error);
        if (error == NULL) {
                g_clear_pointer (&priv->agents, g_strfreev);
                priv->agents = g_steal_pointer (&agents);
                priv->num_agents = num_agents;

        } else
                g_warning ("Failed to read 'agent/whitelist' key: %s",
                           error->message);
}

static void
load_app_configs (GClueConfig *config)
{
        const char *known_groups[] = { "agent", "wifi", "3g", "cdma",
                                       "modem-gps", "network-nmea", "compass",
                                       "static-source", NULL };
        GClueConfigPrivate *priv = config->priv;
        gsize num_groups = 0, i;
        g_auto(GStrv) groups = NULL;

        groups = g_key_file_get_groups (priv->key_file, &num_groups);
        if (num_groups == 0)
                return;

        for (i = 0; i < num_groups; i++) {
                AppConfig *app_config = NULL;
                g_autofree int *users = NULL;
                GList *node;
                gsize num_users = 0, j;
                gboolean allowed, system;
                gboolean ignore = FALSE;
                gboolean new_app_config = TRUE;
                gboolean has_allowed = FALSE;
                gboolean has_system = FALSE;
                gboolean has_users = FALSE;
                g_autoptr(GError) error = NULL;

                for (j = 0; known_groups[j] != NULL; j++)
                        if (strcmp (groups[i], known_groups[j]) == 0) {
                                ignore = TRUE;

                                break;
                        }

                if (ignore)
                        continue;

                /* Check if entry is new or is overwritten */
                for (node = priv->app_configs; node != NULL; node = node->next) {
                        if (strcmp (((AppConfig *) node->data)->id, groups[i]) == 0) {
                                app_config = (AppConfig *) node->data;
                                new_app_config = FALSE;

                                break;
                        }
                }

                allowed = g_key_file_get_boolean (priv->key_file,
                                                  groups[i],
                                                  "allowed",
                                                  &error);
                has_allowed = (error == NULL);
                if (error != NULL && new_app_config)
                        goto error_out;
                g_clear_error (&error);

                system = g_key_file_get_boolean (priv->key_file,
                                                 groups[i],
                                                 "system",
                                                 &error);
                has_system = (error == NULL);
                if (error != NULL && new_app_config)
                        goto error_out;
                g_clear_error (&error);

                users = g_key_file_get_integer_list (priv->key_file,
                                                     groups[i],
                                                     "users",
                                                     &num_users,
                                                     &error);
                has_users = (error == NULL);
                if (error != NULL && new_app_config)
                        goto error_out;
                g_clear_error (&error);


                /* New app config, without erroring out above */
                if (new_app_config) {
                        app_config = g_slice_new0 (AppConfig);
                        priv->app_configs = g_list_prepend (priv->app_configs, app_config);
                        app_config->id = g_strdup (groups[i]);
                }

                /* New app configs will have all of them, overwrites only some */
                if (has_allowed)
                        app_config->allowed = allowed;

                if (has_system)
                        app_config->system = system;

                if (has_users) {
                        g_free (app_config->users);
                        app_config->users = g_steal_pointer (&users);
                        app_config->num_users = num_users;
                }

                continue;
error_out:
                g_warning ("Failed to load configuration for app '%s': %s",
                           groups[i],
                           error->message);
        }
}

static gboolean
load_enable_source_config (GClueConfig *config,
                           const char  *source_name,
                           gboolean initial,
                           gboolean enabled)
{
        GClueConfigPrivate *priv = config->priv;
        g_autoptr(GError) error = NULL;
        gboolean enable;

        /* Source should be initially enabled by default */
        if (!g_key_file_has_key (priv->key_file, source_name, "enable", NULL))
                return initial? TRUE: enabled;

        enable = g_key_file_get_boolean (priv->key_file,
                                         source_name,
                                         "enable",
                                         &error);
        if (error != NULL) {
                g_debug ("Failed to get config %s/enable:"
                         " %s",
                         source_name,
                         error->message);

                /* Keep the previous enable state if something went wrong */
                return enabled;
        }

        return enable;
}

#define DEFAULT_WIFI_URL "https://location.services.mozilla.com/v1/geolocate?key=" MOZILLA_API_KEY
#define DEFAULT_WIFI_SUBMIT_URL "https://location.services.mozilla.com/v2/geosubmit?key=" MOZILLA_API_KEY
#define DEFAULT_WIFI_SUBMIT_NICK "geoclue"

static void
load_wifi_config (GClueConfig *config, gboolean initial)
{
        GClueConfigPrivate *priv = config->priv;
        g_autoptr(GError) error = NULL;
        g_autofree char *wifi_url = NULL;
        g_autofree char *wifi_submit_url = NULL;
        g_autofree char *wifi_submit_nick = NULL;
        guint wifi_submit_nick_length;

        priv->enable_wifi_source =
                load_enable_source_config (config, "wifi", initial,
                                           priv->enable_wifi_source);

        if (initial || g_key_file_has_key (priv->key_file, "wifi", "url", NULL)) {
                wifi_url = g_key_file_get_string (priv->key_file,
                                                  "wifi",
                                                  "url",
                                                  &error);
                if (error == NULL) {
                        g_clear_pointer (&priv->wifi_url, g_free);
                        priv->wifi_url = g_steal_pointer (&wifi_url);
                } else if (initial) {
                        g_debug ("Using the default locate URL: %s", error->message);
                        g_clear_pointer (&priv->wifi_url, g_free);
                        priv->wifi_url = g_strdup (DEFAULT_WIFI_URL);
                } else
                        g_warning ("Failed to get config \"wifi/url\": %s", error->message);

                g_clear_error (&error);
        }

        if (initial || g_key_file_has_key (priv->key_file, "wifi", "submit-data", NULL)) {
                priv->wifi_submit = g_key_file_get_boolean (priv->key_file,
                                                            "wifi",
                                                            "submit-data",
                                                            &error);
                if (error != NULL) {
                        g_warning ("Failed to get config \"wifi/submit-data\": %s",
                                   error->message);
                        return;
                }
                g_clear_error (&error);
        }

        if (initial || g_key_file_has_key (priv->key_file, "wifi", "submission-url", NULL)) {
                wifi_submit_url = g_key_file_get_string (priv->key_file,
                                                         "wifi",
                                                         "submission-url",
                                                         &error);

                if (error == NULL) {
                        g_clear_pointer (&priv->wifi_submit_url, g_free);
                        priv->wifi_submit_url = g_steal_pointer (&wifi_submit_url);
                } else if (initial) {
                        g_debug ("Using the default submission URL: %s", error->message);
                        g_clear_pointer (&priv->wifi_submit_url, g_free);
                        priv->wifi_submit_url = g_strdup (DEFAULT_WIFI_SUBMIT_URL);
                } else
                        g_warning ("Failed to get config \"wifi/submission-url\": %s", error->message);

                g_clear_error (&error);
        }

        if (initial || g_key_file_has_key (priv->key_file, "wifi", "submission-nick", NULL)) {
                wifi_submit_nick = g_key_file_get_string (priv->key_file,
                                                          "wifi",
                                                          "submission-nick",
                                                          &error);

                if (error == NULL) {
                        /* Submission nickname must be 2-32 characters long */
                        wifi_submit_nick_length = strlen (wifi_submit_nick);
                        if (wifi_submit_nick_length >= 2 && wifi_submit_nick_length <= 32) {
                                g_clear_pointer (&priv->wifi_submit_nick, g_free);
                                priv->wifi_submit_nick = g_steal_pointer (&wifi_submit_nick);
                        } else {
                                g_warning ("Submission nick must be between 2-32 characters long");

                                if (initial) {
                                        g_debug ("Using the default submission nick: %s", error->message);
                                        g_clear_pointer (&priv->wifi_submit_nick, g_free);
                                        priv->wifi_submit_nick = g_strdup (DEFAULT_WIFI_SUBMIT_NICK);
                                }
                        }
                } else if (initial) {
                        g_debug ("Using the default submission nick: %s", error->message);
                        g_clear_pointer (&priv->wifi_submit_nick, g_free);
                        priv->wifi_submit_nick = g_strdup (DEFAULT_WIFI_SUBMIT_NICK);
                } else
                        g_warning ("Failed to get config \"wifi/submission-nick\": %s", error->message);
        }
}

static void
load_3g_config (GClueConfig *config, gboolean initial)
{
        config->priv->enable_3g_source =
                load_enable_source_config (config, "3g", initial,
                                           config->priv->enable_3g_source);
}

static void
load_cdma_config (GClueConfig *config, gboolean initial)
{
        config->priv->enable_cdma_source =
                load_enable_source_config (config, "cdma", initial,
                                           config->priv->enable_cdma_source);
}

static void
load_modem_gps_config (GClueConfig *config, gboolean initial)
{
        config->priv->enable_modem_gps_source =
                load_enable_source_config (config, "modem-gps", initial,
                                           config->priv->enable_modem_gps_source);
}

static void
load_network_nmea_config (GClueConfig *config, gboolean initial)
{
        g_autoptr(GError) error = NULL;
        g_autofree char* nmea_socket = NULL;

        config->priv->enable_nmea_source =
                load_enable_source_config (config, "network-nmea", initial,
                                           config->priv->enable_nmea_source);

        if (g_key_file_has_key (config->priv->key_file, "network-nmea", "nmea-socket", NULL)) {
                nmea_socket = g_key_file_get_string (config->priv->key_file,
                                                     "network-nmea",
                                                     "nmea-socket",
                                                     &error);
                if (error == NULL) {
                        g_clear_pointer (&config->priv->nmea_socket, g_free);
                        config->priv->nmea_socket = g_steal_pointer (&nmea_socket);
                } else
                        g_warning ("Failed to get config \"nmea-socket\": %s", error->message);
        }
}

static void
load_compass_config (GClueConfig *config, gboolean initial)
{
        config->priv->enable_compass =
                load_enable_source_config (config, "compass", initial,
                                           config->priv->enable_compass);
}

static void
load_static_source_config (GClueConfig *config, gboolean initial)
{
        config->priv->enable_static_source =
                load_enable_source_config (config, "static-source", initial,
                                           config->priv->enable_static_source);
}

static void
load_config_file (GClueConfig *config, const char *path, gboolean initial) {
        g_autoptr(GError) error = NULL;

        g_debug ("Loading config: %s", path);
        g_key_file_load_from_file (config->priv->key_file,
                                   path,
                                   0,
                                   &error);
        if (error != NULL) {
                g_critical ("Failed to load configuration file '%s': %s",
                            path, error->message);
                return;
        }

        load_agent_config (config, initial);
        load_app_configs (config);
        load_wifi_config (config, initial);
        load_3g_config (config, initial);
        load_cdma_config (config, initial);
        load_modem_gps_config (config, initial);
        load_network_nmea_config (config, initial);
        load_compass_config (config, initial);
        load_static_source_config (config, initial);
}

static void
files_element_clear (void *element)
{
        gchar **file_name = element;
        g_free (*file_name);
}

static gint
sort_files (gconstpointer a, gconstpointer b)
{
        char *str_a = *(char **)a;
        char *str_b = *(char **)b;

        return g_strcmp0 (str_a, str_b);
}

static char *
redact_api_key (char *url)
{
        char *match;

        if (!url)
                return NULL;

        match = g_strrstr (url, "key=");
        if (match && match > url && (*(match - 1) == '?' || *(match - 1) == '&')
            && *(match + 4) != '\0') {
                GString *s;

                s = g_string_new (url);
                g_string_replace (s, match + 4, "<redacted>", 1);
                return g_string_free (s, FALSE);
        } else {
                return g_strdup (url);
        }
}

static void
gclue_config_print (GClueConfig *config)
{
        GList *node;
        AppConfig *app_config = NULL;
        g_autofree char *redacted_locate_url = NULL;
        g_autofree char *redacted_submit_url = NULL;
        gsize i;

        g_debug ("GeoClue configuration:");
        if (config->priv->num_agents > 0) {
                g_debug ("Agents:");
                for (i = 0; i < config->priv->num_agents; i++)
                        g_debug ("\t%s", config->priv->agents[i]);
        } else
                g_debug ("Agents: none");
        g_debug ("Network NMEA source: %s",
                 config->priv->enable_nmea_source? "enabled": "disabled");
        g_debug ("Network NMEA socket: %s",
                 config->priv->nmea_socket == NULL? "none": config->priv->nmea_socket);
        g_debug ("3G source: %s",
                 config->priv->enable_3g_source? "enabled": "disabled");
        g_debug ("CDMA source: %s",
                 config->priv->enable_cdma_source? "enabled": "disabled");
        g_debug ("Modem GPS source: %s",
                 config->priv->enable_modem_gps_source? "enabled": "disabled");
        g_debug ("WiFi source: %s",
                 config->priv->enable_wifi_source? "enabled": "disabled");
        redacted_locate_url = redact_api_key (config->priv->wifi_url);
        g_debug ("WiFi locate URL: %s",
                 redacted_locate_url == NULL ? "none" : redacted_locate_url);
        redacted_submit_url = redact_api_key (config->priv->wifi_submit_url);
        g_debug ("WiFi submit URL: %s",
                 redacted_submit_url == NULL ? "none" : redacted_submit_url);
        g_debug ("WiFi submit data: %s",
                 config->priv->wifi_submit? "enabled": "disabled");
        g_debug ("WiFi submission nickname: %s",
                 config->priv->wifi_submit_nick == NULL? "none": config->priv->wifi_submit_nick);
        g_debug ("Static source: %s",
                 config->priv->enable_static_source? "enabled": "disabled");
        g_debug ("Compass: %s",
                 config->priv->enable_compass? "enabled": "disabled");
        g_debug ("Application configs:");
        for (node = config->priv->app_configs; node != NULL; node = node->next) {
                app_config = (AppConfig *) node->data;
                g_debug ("\tID: %s", app_config->id);
                g_debug ("\t\tAllowed: %s", app_config->allowed? "yes": "no");
                g_debug ("\t\tSystem: %s", app_config->system? "yes": "no");
                if (app_config->num_users > 0) {
                        g_debug ("\t\tUsers:");
                        for (i = 0; i < app_config->num_users; i++)
                                g_debug ("\t\t\t%d", app_config->users[i]);
                } else
                        g_debug ("\t\tUsers: all");
        }
}

static void
gclue_config_init (GClueConfig *config)
{
        g_autoptr(GDir) dir = NULL;
        g_autoptr(GError) error = NULL;
        g_autoptr(GArray) files = NULL;
        char *name;
        gsize i;

        config->priv = gclue_config_get_instance_private (config);
        config->priv->key_file = g_key_file_new ();

        /* Load config file from default path, log all missing parameters */
        load_config_file (config, CONFIG_FILE_PATH, TRUE);

        /*
         * Apply config overwrites from conf.d style config files,
         * files are sorted alphabetically, example: '90-config.conf'
         * will overwrite '50-config.conf'.
         */
        dir = g_dir_open (CONFIG_D_DIRECTORY, 0, &error);

        if (error != NULL) {
                g_warning ("Failed to open %s: %s",
                           CONFIG_D_DIRECTORY, error->message);
                goto out;
        }

        files = g_array_new (FALSE, FALSE, sizeof(char *));
        g_array_set_clear_func (files, files_element_clear);

        while ((name = g_strdup (g_dir_read_name (dir)))) {
                if (g_str_has_suffix (name, ".conf"))
                        g_array_append_val (files, name);
        }

        g_array_sort (files, sort_files);

        for (i = 0; i < files->len; i++) {
                g_autofree char *path = NULL;

                path = g_build_filename (CONFIG_D_DIRECTORY,
                                         g_array_index (files, char *, i),
                                         NULL);
                load_config_file (config, path, FALSE);
        }
out:
        gclue_config_print (config);
}

GClueConfig *
gclue_config_get_singleton (void)
{
        static GClueConfig *config = NULL;

        if (config == NULL)
                config = g_object_new (GCLUE_TYPE_CONFIG, NULL);

        return config;
}

gboolean
gclue_config_is_agent_allowed (GClueConfig     *config,
                               const char      *desktop_id,
                               GClueClientInfo *agent_info)
{
        gsize i;

        for (i = 0; i < config->priv->num_agents; i++) {
                if (g_strcmp0 (desktop_id, config->priv->agents[i]) == 0)
                        return TRUE;
        }

        return FALSE;
}

gsize
gclue_config_get_num_allowed_agents (GClueConfig *config)
{
        return config->priv->num_agents;
}

GClueAppPerm
gclue_config_get_app_perm (GClueConfig     *config,
                           const char      *desktop_id,
                           GClueClientInfo *app_info)
{
        GClueConfigPrivate *priv = config->priv;
        GList *node;
        AppConfig *app_config = NULL;
        gsize i;
        guint64 uid;

        g_return_val_if_fail (desktop_id != NULL, GCLUE_APP_PERM_DISALLOWED);

        for (node = priv->app_configs; node != NULL; node = node->next) {
                if (strcmp (((AppConfig *) node->data)->id, desktop_id) == 0) {
                        app_config = (AppConfig *) node->data;

                        break;
                }
        }

        if (app_config == NULL) {
                g_debug ("'%s' not in configuration", desktop_id);

                return GCLUE_APP_PERM_ASK_AGENT;
        }

        if (!app_config->allowed) {
                g_debug ("'%s' disallowed by configuration", desktop_id);

                return GCLUE_APP_PERM_DISALLOWED;
        }

        if (app_config->num_users == 0)
                return GCLUE_APP_PERM_ALLOWED;

        uid = gclue_client_info_get_user_id (app_info);

        for (i = 0; i < app_config->num_users; i++) {
                if (app_config->users[i] == uid)
                        return GCLUE_APP_PERM_ALLOWED;
        }

        return GCLUE_APP_PERM_DISALLOWED;
}

gboolean
gclue_config_is_system_component (GClueConfig *config,
                                  const char  *desktop_id)
{
        GClueConfigPrivate *priv = config->priv;
        GList *node;
        AppConfig *app_config = NULL;

        g_return_val_if_fail (desktop_id != NULL, FALSE);

        for (node = priv->app_configs; node != NULL; node = node->next) {
                if (strcmp (((AppConfig *) node->data)->id, desktop_id) == 0) {
                        app_config = (AppConfig *) node->data;

                        break;
                }
        }

        return (app_config != NULL && app_config->system);
}

const char *
gclue_config_get_nmea_socket (GClueConfig *config)
{
        return config->priv->nmea_socket;
}

const char *
gclue_config_get_wifi_url (GClueConfig *config)
{
        return config->priv->wifi_url;
}

const char *
gclue_config_get_wifi_submit_url (GClueConfig *config)
{
        return config->priv->wifi_submit_url;
}

const char *
gclue_config_get_wifi_submit_nick (GClueConfig *config)
{
        return config->priv->wifi_submit_nick;
}

void
gclue_config_set_wifi_submit_nick (GClueConfig *config,
                                   const char  *nick)
{
        g_clear_pointer (&config->priv->wifi_submit_nick, g_free);
        config->priv->wifi_submit_nick = g_strdup (nick);
}

gboolean
gclue_config_get_wifi_submit_data (GClueConfig *config)
{
        return config->priv->wifi_submit;
}

void
gclue_config_set_wifi_submit_data (GClueConfig *config,
                                   gboolean     submit)
{
        config->priv->wifi_submit = submit;
}

gboolean
gclue_config_get_enable_wifi_source (GClueConfig *config)
{
        return config->priv->enable_wifi_source;
}

gboolean
gclue_config_get_enable_3g_source (GClueConfig *config)
{
        return config->priv->enable_3g_source;
}

gboolean
gclue_config_get_enable_modem_gps_source (GClueConfig *config)
{
        return config->priv->enable_modem_gps_source;
}

gboolean
gclue_config_get_enable_cdma_source (GClueConfig *config)
{
        return config->priv->enable_cdma_source;
}

gboolean
gclue_config_get_enable_nmea_source (GClueConfig *config)
{
        return config->priv->enable_nmea_source;
}

void
gclue_config_set_nmea_socket (GClueConfig *config,
                              const char  *nmea_socket)
{
        g_clear_pointer (&config->priv->nmea_socket, g_free);
        config->priv->nmea_socket = g_strdup (nmea_socket);
}

gboolean
gclue_config_get_enable_compass (GClueConfig *config)
{
        return config->priv->enable_compass;
}

gboolean
gclue_config_get_enable_static_source (GClueConfig *config)
{
        return config->priv->enable_static_source;
}
