/* vim: set et ts=8 sw=8: */
/*
 * Copyright 2014 Red Hat, Inc.
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

#ifndef GCLUE_3G_TOWER_H
#define GCLUE_3G_TOWER_H

G_BEGIN_DECLS

typedef enum {
  GCLUE_TOWER_TEC_UNKNOWN = 0,
  GCLUE_TOWER_TEC_2G = 1,
  GCLUE_TOWER_TEC_3G = 2,
  GCLUE_TOWER_TEC_4G = 3,
  GCLUE_TOWER_TEC_NO_FIX = 99,
} GClueTowerTec;
# define GCLUE_TOWER_TEC_MAX_VALID GCLUE_TOWER_TEC_4G

typedef struct _GClue3GTower GClue3GTower;

#define GCLUE_3G_TOWER_OPERATOR_CODE_STR_LEN 6
#define GCLUE_3G_TOWER_COUNTRY_CODE_STR_LEN 3

struct _GClue3GTower {
        gchar   opc[GCLUE_3G_TOWER_OPERATOR_CODE_STR_LEN + 1];
        gulong  lac;
        gulong  cell_id;
        GClueTowerTec tec;
};

G_END_DECLS

#endif /* GCLUE_3G_TOWER_H */
