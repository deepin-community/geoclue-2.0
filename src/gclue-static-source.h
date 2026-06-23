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

#ifndef GCLUE_STATIC_SOURCE_H
#define GCLUE_STATIC_SOURCE_H

#include <glib.h>
#include "gclue-location-source.h"

G_BEGIN_DECLS

#define GCLUE_TYPE_STATIC_SOURCE gclue_static_source_get_type ()

G_DECLARE_FINAL_TYPE (GClueStaticSource,
                      gclue_static_source,
                      GCLUE, STATIC_SOURCE,
                      GClueLocationSource)

GClueStaticSource *gclue_static_source_get_singleton (GClueAccuracyLevel level);

G_END_DECLS

#endif /* GCLUE_STATIC_SOURCE_H */
