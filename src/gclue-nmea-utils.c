/* vim: set et ts=8 sw=8: */
/*
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
#include "gclue-nmea-utils.h"

/**
 * gclue_nmea_type_is:
 * @msg: NMEA sentence
 * @nmeatype: A three character NMEA sentence type string ("GGA", "RMC" etc.)
 *
 * Returns: whether given NMEA sentence is of the given type
 **/
gboolean
gclue_nmea_type_is (const char *msg, const char *nmeatype)
{
        g_assert (strnlen (nmeatype, 4) < 4);

        return strnlen (msg, 7) > 6 &&
                g_str_has_prefix (msg, "$") &&
                g_str_has_prefix (msg+3, nmeatype);
}

/**
 * gclue_nmea_timestamp_to_timespan
 * @timestamp: NMEA timestamp string
 *
 * Parse the NMEA timestamp string, which is a field in NMEA sentences
 * like GGA and RMC.
 *
 * Returns: a GTimeSpan (gint64) value of microseconds since midnight,
 * or -1, if reading fails.
 */
GTimeSpan
gclue_nmea_timestamp_to_timespan (const gchar *timestamp)
{
        gint its, hours, minutes;
        gdouble ts, seconds_f;
        gchar *endptr;

        if (!timestamp || !*timestamp)
            return -1;

        ts = g_ascii_strtod (timestamp, &endptr);
        if (endptr != timestamp + g_utf8_strlen(timestamp, 12) ||
            ts < 0.0 ||
            ts >= 235960.0)
                return -1;

        its = (gint) ts;  /* Truncate towards zero */
        hours = its / 10000;
        minutes = (its - 10000 * hours) / 100;
        seconds_f = ts - 10000 * hours - 100 * minutes;  /* Seconds plus fraction */

        return (GTimeSpan) G_USEC_PER_SEC * (3600 * hours +
                                               60 * minutes +
                                                    seconds_f);
}

