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
/** @file gnc-price-model.h
    @brief CSV import logic
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/

#ifndef GNC_PRICE_MODEL_H
#define GNC_PRICE_MODEL_H

#include "config.h"

#include "stf/stf-parse.h"

#include "gnc-engine.h"

/** Enumeration for column types. These are the different types of
 * columns that can exist in a CSV/Fixed-Width file. There should be
 * no two columns with the same type except for the GNC_PRICE_NONE
 * type. */
enum GncPriceColumnType
{
    GNC_PRICE_NONE,
    GNC_PRICE_DATE,
    GNC_PRICE_TIME,
    GNC_PRICE_NAMESPACE,
    GNC_PRICE_MNEMONIC,
    GNC_PRICE_PRICE,
    GNC_PRICE_CURRENCY,
    GNC_PRICE_TYPE,
    GNC_PRICE_NUM_COL_TYPES,
};

/** Enumeration for error types. These are the different types of
 * errors that various functions used for the CSV/Fixed-Width importer
 * can have. */
enum GncPriceErrorType
{
    GNC_PRICE_FILE_OPEN_ERR,
    GNC_PRICE_ENCODING_ERR
};

/** Struct for containing a string. This struct simply contains
 * pointers to the beginning and end of a string. We need this because
 * the STF code that gnc_price_parse calls requires these pointers. */
typedef struct
{
    char* begin;
    char* end;
} GncPriceStr;

extern const int num_date_formats;
/* A set of date formats that the user sees. */
extern const gchar* date_format_user[];

/* This array contains all of the different strings for different column types. */
extern gchar* gnc_price_column_type_strs[];

/** Struct containing data for parsing a CSV/Fixed-Width file. */
typedef struct
{
    gchar* encoding;
    GMappedFile* raw_mapping; /**< The mapping containing raw_str */
    GncPriceStr raw_str; /**< Untouched data from the file as a string */
    GncPriceStr file_str; /**< raw_str translated into UTF-8 */
    GPtrArray* orig_lines; /**< file_str parsed into a two-dimensional array of strings */
    GArray* orig_row_lengths; /**< The lengths of rows in orig_lines
			       * before error messages are appended */
    int orig_max_row; /**< Holds the maximum value in orig_row_lengths */
    GStringChunk* chunk; /**< A chunk of memory in which the contents of orig_lines is stored */
    StfParseOptions_t* options; /**< Options controlling how file_str should be parsed */
    GArray* column_types; /**< Array of values from the GncPriceColumnType enumeration */
    GList* error_lines; /**< List of row numbers in orig_lines that have errors */
    GList* prices; /**< List of GncPriceLines created using orig_lines and column_types */
    int date_format; /**< The format of the text in the date columns from date_format_internal. */
    gboolean skip_headerline; /**< Whether to skip the first line of the file. */
} GncPriceParseData;

typedef struct
{
    int nb_imported;
    int nb_skipped;
    int nb_securities_created;
} GncPriceImportReport;

GncPriceParseData* gnc_price_new_parse_data(void);

void gnc_price_parse_data_free(GncPriceParseData* parse_data);

int gnc_price_load_file(GncPriceParseData* parse_data, const char* filename,
			GError** error);

int gnc_price_convert_encoding(GncPriceParseData* parse_data, const char* encoding, GError** error);

int gnc_price_parse(GncPriceParseData* parse_data, gboolean guessColTypes, GError** error);

void gnc_price_parse_to_prices(GncPriceParseData* parse_data, gboolean redo_errors);

void gnc_price_do_import(GList* prices, GncPriceImportReport *report);

#endif
