/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/
/** @file gnc-plugin-price-csv.h
    @brief Price csv import/export plugin
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/

#ifndef __GNC_PLUGIN_PRICE_CSV_H
#define __GNC_PLUGIN_PRICE_CSV_H

#include <gtk/gtk.h>

#include "gnc-plugin.h"

G_BEGIN_DECLS

/* type macros */
#define GNC_TYPE_PLUGIN_PRICE_CSV            (gnc_plugin_price_csv_get_type ())
#define GNC_PLUGIN_PRICE_CSV(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNC_TYPE_PLUGIN_PRICE_CSV, GncPluginPriceCsv))
#define GNC_PLUGIN_PRICE_CSV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GNC_TYPE_PLUGIN_PRICE_CSV, GncPluginPriceCsvClass))
#define GNC_IS_PLUGIN_PRICE_CSV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNC_TYPE_PLUGIN_PRICE_CSV))
#define GNC_IS_PLUGIN_PRICE_CSV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GNC_TYPE_PLUGIN_PRICE_CSV))
#define GNC_PLUGIN_PRICE_CSV_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GNC_TYPE_PLUGIN_PRICE_CSV, GncPluginPriceCsvClass))

#define GNC_PLUGIN_PRICE_CSV_NAME "gnc-plugin-price-csv"

/* typedefs & structures */
typedef struct
{
    GncPlugin gnc_plugin;
} GncPluginPriceCsv;

typedef struct
{
    GncPluginClass gnc_plugin;
} GncPluginPriceCsvClass;

/* function prototypes */
GType      gnc_plugin_price_csv_get_type (void);

GncPlugin *gnc_plugin_price_csv_new      (void);

void       gnc_plugin_price_csv_create_plugin (void);

G_END_DECLS

#endif /* __GNC_PLUGIN_PRICE_CSV_H */
