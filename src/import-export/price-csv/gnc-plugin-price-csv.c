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
/** @file gnc-plugin-price-csv.c
    @brief Price csv import/export plugin
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gnc-plugin-price-csv.h"
#include "gnc-plugin-manager.h"

#include "gnc-price-import.h"
#include "gnc-price-export.h"

static void gnc_plugin_price_csv_class_init (GncPluginPriceCsvClass *klass);
static void gnc_plugin_price_csv_init (GncPluginPriceCsv *plugin);
static void gnc_plugin_price_csv_finalize (GObject *object);

/* Command callbacks */
static void gnc_plugin_price_csv_cmd_import (GtkAction *action, GncMainWindowActionData *data);
static void gnc_plugin_price_csv_cmd_export (GtkAction *action, GncMainWindowActionData *data);


#define PLUGIN_ACTIONS_NAME "gnc-plugin-price-csv-actions"
#define PLUGIN_UI_FILENAME  "gnc-plugin-price-csv-ui.xml"

static GtkActionEntry gnc_plugin_actions [] =
{
    {
	"PriceImportAction", GTK_STOCK_CONVERT, N_("Import _Prices from CSV/Fixed-Width..."), NULL,
	N_(" a CSV/Fixed-Width file"),
	G_CALLBACK (gnc_plugin_price_csv_cmd_import)
    },
    {
	"PriceExportAction", GTK_STOCK_CONVERT, N_("Export _Prices to CSV..."), NULL,
	N_(" a CSV/Fixed-Width file"),
	G_CALLBACK (gnc_plugin_price_csv_cmd_export)
    },
};
static guint gnc_plugin_n_actions = G_N_ELEMENTS (gnc_plugin_actions);

typedef struct GncPluginPriceCsvPrivate
{
    gpointer dummy;
} GncPluginPriceCsvPrivate;

#define GNC_PLUGIN_PRICE_CSV_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNC_TYPE_PLUGIN_PRICE_CSV, GncPluginPriceCsvPrivate))

static GObjectClass *parent_class = NULL;

GType
gnc_plugin_price_csv_get_type (void)
{
    static GType gnc_plugin_price_csv_type = 0;

    if (gnc_plugin_price_csv_type == 0)
    {
	static const GTypeInfo our_info =
	{
	    sizeof (GncPluginPriceCsvClass),
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) gnc_plugin_price_csv_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof (GncPluginPriceCsv),
	    0,		/* n_preallocs */
	    (GInstanceInitFunc) gnc_plugin_price_csv_init,
	};

	gnc_plugin_price_csv_type = g_type_register_static (GNC_TYPE_PLUGIN,
							    "GncPluginPriceCsv",
							    &our_info, 0);
    }

    return gnc_plugin_price_csv_type;
}

GncPlugin *
gnc_plugin_price_csv_new (void)
{
    return GNC_PLUGIN (g_object_new (GNC_TYPE_PLUGIN_PRICE_CSV, NULL));
}

static void
gnc_plugin_price_csv_class_init (GncPluginPriceCsvClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GncPluginClass *plugin_class = GNC_PLUGIN_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    object_class->finalize = gnc_plugin_price_csv_finalize;

    /* plugin info */
    plugin_class->plugin_name  = GNC_PLUGIN_PRICE_CSV_NAME;

    /* widget addition/removal */
    plugin_class->actions_name = PLUGIN_ACTIONS_NAME;
    plugin_class->actions      = gnc_plugin_actions;
    plugin_class->n_actions    = gnc_plugin_n_actions;
    plugin_class->ui_filename  = PLUGIN_UI_FILENAME;

    g_type_class_add_private(klass, sizeof(GncPluginPriceCsvPrivate));
}

static void
gnc_plugin_price_csv_init (GncPluginPriceCsv *plugin)
{
}

static void
gnc_plugin_price_csv_finalize (GObject *object)
{
    GncPluginPriceCsv *plugin;
    GncPluginPriceCsvPrivate *priv;

    g_return_if_fail (GNC_IS_PLUGIN_PRICE_CSV (object));

    plugin = GNC_PLUGIN_PRICE_CSV (object);
    priv = GNC_PLUGIN_PRICE_CSV_GET_PRIVATE(plugin);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

/************************************************************
 *              Plugin Function Implementation              *
 ************************************************************/

/************************************************************
 *                    Command Callbacks                     *
 ************************************************************/

static void
gnc_plugin_price_csv_cmd_import (GtkAction *action,
				 GncMainWindowActionData *data)
{
    gnc_file_price_import();
}

static void
gnc_plugin_price_csv_cmd_export (GtkAction *action,
				 GncMainWindowActionData *data)
{
    gnc_file_price_export();
}


/************************************************************
 *                    Plugin Bootstrapping                   *
 ************************************************************/

void
gnc_plugin_price_csv_create_plugin (void)
{
    GncPlugin *plugin = gnc_plugin_price_csv_new ();

    gnc_plugin_manager_add_plugin (gnc_plugin_manager_get (), plugin);
}
