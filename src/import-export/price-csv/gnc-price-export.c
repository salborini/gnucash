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
/** @file gnc-price-export.c
    @brief Price Export GUI code
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/
#include "config.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <errno.h>
#ifndef HAVE_LOCALTIME_R
#include "localtime_r.h"
#endif

#include "gnc-file.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "gnc-window.h"
#include "gnc-session.h"

#ifndef HAVE_LOCALTIME_R
# include "localtime_r.h"
#endif

#include "gnc-price-export.h"

/* use same directory for both import and export */
#define GCONF_SECTION "dialogs/import/price"

static QofLogModule log_module = GNC_MOD_IMPORT;

/* rolling out our own function as advised by the HACK ALERT in gnc-date.h */
static char *format_timespec(Timespec ts)
{
    char buff[MAX_DATE_LENGTH];
    int flen;
    int day, month, year, hour, min, sec;
    struct tm ltm;
    time_t t = ts.tv_sec + (time_t)(ts.tv_nsec / 1000000000.0);
    localtime_r (&t, &ltm);
    day = ltm.tm_mday;
    month = ltm.tm_mon + 1;
    year = ltm.tm_year + 1900;
    hour = ltm.tm_hour;
    min = ltm.tm_min;
    sec = ltm.tm_sec;
    /* this is the iso format, with seconds */
    g_snprintf (buff, MAX_DATE_LENGTH,
		"%04d-%02d-%02d,%02d:%02d:%02d",
		year, month, day, hour, min, sec);
    return g_strdup (buff);
}

static gboolean price_printer (GNCPrice *p, gpointer user_data)
{
    FILE *pFile = (FILE *)user_data;
    const gnc_commodity *cm, *currency;
    const char *namespacestr, *codestr, *typestr, *sourcestr, *currencystr;
    char *datetimestr, *pricestr;
    gnc_numeric price;

    cm = gnc_price_get_commodity (p);
    currency = gnc_price_get_currency (p);
    namespacestr = gnc_commodity_get_namespace (cm);
    codestr = gnc_commodity_get_mnemonic (cm);
    datetimestr = format_timespec (gnc_price_get_time (p));
    typestr = gnc_price_get_typestr (p);
    price = gnc_price_get_value (p);
    pricestr = gnc_numeric_to_string (price);
    if (!typestr) typestr = "";
    sourcestr = gnc_price_get_source (p);
    currencystr = gnc_commodity_get_mnemonic (currency);
    price = gnc_price_get_value (p);

    fprintf (pFile, "%s,%s,%s,=%s,%s,%s,%s\n", namespacestr, codestr,
	     datetimestr, pricestr, currencystr, typestr, sourcestr);

    g_free (datetimestr);
    g_free (pricestr);
    return TRUE;
}

static gboolean do_export (GNCPriceDB *db, const char *filename)
{
    FILE *pFile;

    pFile = g_fopen (filename,"w");
    if (!pFile)
    {
	return FALSE;
    }
    fprintf (pFile, "%s,%s,%s,%s,%s,%s,%s,%s\n", _("Namespace"), _("Security"),
	     _("Date"), _("Time"), _("Price"), _("Currency"), _("Type"), _("Source"));
    gnc_pricedb_foreach_price (db, &price_printer, pFile, TRUE);
    fclose (pFile);

    return TRUE;
}

/** Lets the user export prices to a CSV file. */
void gnc_file_price_export ()
{
    char *filename;
    gboolean ok;
    char *default_dir;
    QofBook *current_book;
    GNCPriceDB *db;
    GtkFileFilter *filter;
    struct stat statbuf;

    ENTER(" ");
    current_book = qof_session_get_book (gnc_get_current_session());
    db = gnc_pricedb_get_db (current_book);

    if (!gnc_pricedb_get_num_prices (db))
    {
	/* no accounts file is currently opened, or there are no
	 * prices in it to export */
	gnc_error_dialog (NULL, _("Nothing to export"));
	LEAVE(" ");
	return;
    }
    default_dir = gnc_get_default_directory (GCONF_SECTION);
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "*.csv");
    gtk_file_filter_add_pattern(filter, "*.[Cc][Ss][Vv]");
    filename = gnc_file_dialog (_("Export"), g_list_prepend(NULL, filter),
				default_dir, GNC_FILE_DIALOG_EXPORT);
    g_free(default_dir);
    if (!filename) return;

    /* Remember the directory of the selected file as the default. */
    default_dir = g_path_get_dirname(filename);
    gnc_set_default_directory(GCONF_SECTION, default_dir);
    g_free(default_dir);

    if (g_stat (filename, &statbuf) == 0)
    {
	const char *format = _("The file %s already exists. "
			       "Are you sure you want to overwrite it?");
	/* if user says cancel, we should break out */
	if (!gnc_verify_dialog (NULL, FALSE, format, filename ))
	{
	    g_free (filename);
	    LEAVE(" ");
	    return;
	}
    }

    gnc_set_busy_cursor (NULL, TRUE);
    gnc_window_show_progress (_("Exporting prices..."), 0.0);

    ok = do_export (db, filename);
    g_free (filename);
    gnc_window_show_progress (NULL, -1.0);
    gnc_unset_busy_cursor (NULL);

    if (!ok)
    {
	/* %s is the strerror(3) error string of the error that occurred. */
	const char *format = _("There was an error saving the file.\n\n%s");
	gnc_error_dialog (NULL, format, strerror(errno));
	LEAVE(" ");
	return;
    }

    LEAVE(" ");
}
