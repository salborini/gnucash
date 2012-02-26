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
/** @file gnc-price-model.c
    @brief CSV import logic
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/
#include "gnc-price-model.h"

#include <glib/gi18n.h>

#include <goffice/goffice-features.h>
#if (GO_VERSION_EPOCH == 0) && (GO_VERSION_MAJOR == 7) && (GO_VERSION_MINOR == 8)
/* For libgoffice-0.7.8, disable its internal inclusion of <regutf8.h>
   so to avoid clashing symbol definitions with <regex.h> */
# define GO_REGUTF8_H
#endif
#include <goffice/utils/go-glib-extras.h>

#include <string.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#ifndef HAVE_LOCALTIME_R
#include "localtime_r.h"
#endif

#include "gnc-session.h"
#include "gnc-ui-util.h"

static QofLogModule log_module = GNC_MOD_IMPORT;

const int num_date_formats = 5;

const gchar* date_format_user[] = {
    N_("y-m-d"),
    N_("d-m-y"),
    N_("m-d-y"),
    N_("d-m"),
    N_("m-d")
};

/* This array contains all of the different strings for different column types. */
gchar* gnc_price_column_type_strs[GNC_PRICE_NUM_COL_TYPES] = {
    N_("None"),
    N_("Date"),
    N_("Time"),
    N_("Namespace"),
    N_("Security"),
    N_("Price"),
    N_("Currency"),
    N_("Type")
};

/** A set of sensible defaults for parsing CSV files.
 * @return StfParseOptions_t* for parsing a file with comma separators
 */
static StfParseOptions_t* default_parse_options(void)
{
    StfParseOptions_t* options = stf_parse_options_new();
    stf_parse_options_set_type(options, PARSE_TYPE_CSV);
    stf_parse_options_csv_set_separators(options, ",", NULL);
    return options;
}

/** Struct pairing a price with a line number. This struct is used to
 * keep the prices in order. When rows are separated into "valid" and
 * "error" lists (in case some of the rows have cells that are
 * unparseable), we want the user to still be able to "correct" the
 * error list. If we keep the line numbers of valid prices, we can
 * then put prices created from the newly corrected rows into the
 * right places. We cannot use a GNCPrice* as manipulating these objects
 * set the book dirty. */
typedef struct
{
    int line_no;
    int date_format;
    char *namespace;
    char *mnemonic;
    char *currency;
    struct tm *datetm;
    struct tm *timetm;
    char *type;
    gnc_numeric *value;
} GncPriceLine;

static GncPriceLine* gnc_priceline_new(int date_format)
{
    GncPriceLine *pl = g_new(GncPriceLine, 1);
    pl->namespace = pl->mnemonic = pl->currency = pl->type = NULL;
    pl->datetm = NULL;
    pl->timetm = NULL;
    pl->value = NULL;
    pl->line_no = -1;
    pl->date_format = date_format;
    return pl;
}

static void gnc_priceline_free(GncPriceLine *pl)
{
    g_free(pl->namespace);
    g_free(pl->mnemonic);
    g_free(pl->currency);
    g_free(pl->datetm);
    g_free(pl->timetm);
    g_free(pl->type);
    g_free(pl->value);
}

/** Parses a string into a date, given a format. The format must
 * include the year. This function should only be called by
 * parse_date.
 * @param date_str The string containing a date being parsed
 * @param format An index specifying a format in date_format_user
 * @return The parsed value of date_str on success or -1 on failure
 */
static gboolean extract_date_with_year(const char* date_str, GncPriceLine *price)
{
    time_t rawtime; /* The integer time */
    struct tm *retvalue = g_new(struct tm, 1);
    struct tm test_retvalue;

    int i, j, mem_length, orig_year = -1, orig_month = -1, orig_day = -1;

    /* Buffer for containing individual parts (e.g. year, month, day) of a date */
    char date_segment[5];

    /* The compiled regular expression */
    regex_t preg = {0};

    /* An array containing indices specifying the matched substrings in date_str */
    regmatch_t pmatch[4] = { {0}, {0}, {0}, {0} };

    /* The regular expression for parsing dates */
    /* TODO update to add hour/min */
    const char* regex = "^ *([0-9]+) *[-/.'] *([0-9]+) *[-/.'] *([0-9]+).*$|^ *([0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]).*$";

    /* We get our matches using the regular expression. */
    regcomp(&preg, regex, REG_EXTENDED);
    regexec(&preg, date_str, 4, pmatch, 0);
    regfree(&preg);

    /* If there wasn't a match, there was an error. */
    if (pmatch[0].rm_eo == 0)
	return -1;

    /* If this is a string without separators ... */
    if (pmatch[1].rm_so == -1)
    {
	/* ... we will fill in the indices based on the user's selection. */
	int k = 0; /* k traverses date_str by keeping track of where separators "should" be. */
	j = 1; /* j traverses pmatch. */
	for (i = 0; date_format_user[price->date_format][i]; i++)
	{
	    char segment_type = date_format_user[price->date_format][i];
	    /* Only do something if this is a meaningful character */
	    if (segment_type == 'y' || segment_type == 'm' || segment_type == 'd')
	    {
		pmatch[j].rm_so = k;
		switch (segment_type)
		{
		case 'm':
		case 'd':
		    k += 2;
		    break;

		case 'y':
		    k += 4;
		    break;
		}

		pmatch[j].rm_eo = k;
		j++;
	    }
	}
    }

    /* Put some sane values in retvalue by using the current time for
     * the non-year-month-day parts of the date. */
    time(&rawtime);
    localtime_r(&rawtime, retvalue);

    /* j traverses pmatch (index 0 contains the entire string, so we
     * start at index 1 for the first meaningful match). */
    j = 1;
    /* Go through the date format and interpret the matches in order of
     * the sections in the date format. */
    for (i = 0; date_format_user[price->date_format][i]; i++)
    {
	char segment_type = date_format_user[price->date_format][i];
	/* Only do something if this is a meaningful character */
	if (segment_type == 'y' || segment_type == 'm' || segment_type == 'd')
	{
	    /* Copy the matching substring into date_segment so that we can
	     * convert it into an integer. */
	    mem_length = pmatch[j].rm_eo - pmatch[j].rm_so;
	    memcpy(date_segment, date_str + pmatch[j].rm_so, mem_length);
	    date_segment[mem_length] = '\0';

	    /* Set the appropriate member of retvalue. Save the original
	     * values so that we can check if the change when we use mktime
	     * below. */
	    switch (segment_type)
	    {
	    case 'y':
		retvalue->tm_year = atoi(date_segment);

		/* Handle two-digit years. */
		if (retvalue->tm_year < 100)
		{
		    /* We allow two-digit years in the range 1969 - 2068. */
		    if (retvalue->tm_year < 69)
			retvalue->tm_year += 100;
		}
		else
		    retvalue->tm_year -= 1900;
		orig_year = retvalue->tm_year;
		break;

	    case 'm':
		orig_month = retvalue->tm_mon = atoi(date_segment) - 1;
		break;

	    case 'd':
		orig_day = retvalue->tm_mday = atoi(date_segment);
		break;
	    }
	    j++;
	}
    }
    /* Convert back to an integer. If mktime leaves retvalue unchanged,
     * everything is okay; otherwise, an error has occurred. */
    /* We have to use a "test" date value to account for changes in
     * daylight savings time, which can cause a date change with mktime
     * near midnight, causing the code to incorrectly think a date is
     * incorrect. */
    test_retvalue = *retvalue;
    mktime(&test_retvalue);
    retvalue->tm_isdst = test_retvalue.tm_isdst;
    rawtime = mktime(retvalue);
    if (retvalue->tm_mday == orig_day &&
	retvalue->tm_mon == orig_month &&
	retvalue->tm_year == orig_year)
    {
	price->datetm = retvalue;
	return TRUE;
    }
    else
    {
	g_free(retvalue);
	return FALSE;
    }
}

/** Parses a string into a date, given a format. The format cannot
 * include the year. This function should only be called by
 * parse_date.
 * @param date_str The string containing a date being parsed
 * @param format An index specifying a format in date_format_user
 * @return The parsed value of date_str on success or -1 on failure
 */
static gboolean extract_date_without_year(const char* date_str, GncPriceLine *price)
{
    time_t rawtime; /* The integer time */
    struct tm *retvalue = g_new(struct tm, 1);
    struct tm test_retvalue;

    int i, j, mem_length, orig_year = -1, orig_month = -1, orig_day = -1;

    /* Buffer for containing individual parts (e.g. year, month, day) of a date */
    gchar* date_segment;

    /* The compiled regular expression */
    regex_t preg = {0};

    /* An array containing indices specifying the matched substrings in date_str */
    regmatch_t pmatch[3] = { {0}, {0}, {0} };

    /* The regular expression for parsing dates */
    const char* regex = "^ *([0-9]+) *[-/.'] *([0-9]+).*$";

    /* We get our matches using the regular expression. */
    regcomp(&preg, regex, REG_EXTENDED);
    regexec(&preg, date_str, 3, pmatch, 0);
    regfree(&preg);

    /* If there wasn't a match, there was an error. */
    if (pmatch[0].rm_eo == 0)
    {
	g_free(retvalue);
	return FALSE;
    }

    /* Put some sane values in retvalue by using the current time for
     * the non-year-month-day parts of the date. */
    time(&rawtime);
    localtime_r(&rawtime, retvalue);
    orig_year = retvalue->tm_year;

    /* j traverses pmatch (index 0 contains the entire string, so we
     * start at index 1 for the first meaningful match). */
    j = 1;
    /* Go through the date format and interpret the matches in order of
     * the sections in the date format. */
    for (i = 0; date_format_user[price->date_format][i]; i++)
    {
	char segment_type = date_format_user[price->date_format][i];
	/* Only do something if this is a meaningful character */
	if (segment_type == 'm' || segment_type == 'd')
	{
	    /* Copy the matching substring into date_segment so that we can
	     * convert it into an integer. */
	    mem_length = pmatch[j].rm_eo - pmatch[j].rm_so;
	    date_segment = g_new(gchar, mem_length+1);
	    memcpy(date_segment, date_str + pmatch[j].rm_so, mem_length);
	    date_segment[mem_length] = '\0';

	    /* Set the appropriate member of retvalue. Save the original
	     * values so that we can check if the change when we use mktime
	     * below. */
	    switch (segment_type)
	    {
	    case 'm':
		orig_month = retvalue->tm_mon = atoi(date_segment) - 1;
		break;

	    case 'd':
		orig_day = retvalue->tm_mday = atoi(date_segment);
		break;
	    }
	    g_free(date_segment);
	    j++;
	}
    }
    /* Convert back to an integer. If mktime leaves retvalue unchanged,
     * everything is okay; otherwise, an error has occurred. */
    /* We have to use a "test" date value to account for changes in
     * daylight savings time, which can cause a date change with mktime
     * near midnight, causing the code to incorrectly think a date is
     * incorrect. */
    test_retvalue = *retvalue;
    mktime(&test_retvalue);
    retvalue->tm_isdst = test_retvalue.tm_isdst;
    rawtime = mktime(retvalue);
    if (retvalue->tm_mday == orig_day &&
	retvalue->tm_mon == orig_month &&
	retvalue->tm_year == orig_year)
    {
	price->datetm = retvalue;
	return TRUE;
    }
    else
    {
	g_free(retvalue);
	return FALSE;
    }
}

/** Parses a string into a date, given a format. This function
 * requires only knowing the order in which the year, month and day
 * appear. For example, 01-02-2003 will be parsed the same way as
 * 01/02/2003.
 * @param date_str The string containing a date being parsed
 * @param format An index specifying a format in date_format_user
 * @return The parsed value of date_str on success or -1 on failure
 */
static gboolean extract_date(const char* date_str, GncPriceLine *price)
{
    if (strchr(date_format_user[price->date_format], 'y'))
	return extract_date_with_year(date_str, price);
    else
	return extract_date_without_year(date_str, price);
}

static gboolean extract_time(const char* time_str, GncPriceLine *price)
{
    struct tm *retvalue = g_new(struct tm, 1);
    /* Buffer for containing individual parts */
    gchar segment[3];

    /* The compiled regular expression */
    regex_t preg = {0};

    /* An array containing indices specifying the matched substrings in time_str */
    regmatch_t pmatch[4] = { {0}, {0}, {0}, {0} };
    int j, mem_length;

    /* The regular expression for parsing times */
    const char* regex = "^ *([0-9]{1,2}):([0-9]{1,2}):?([0-9]{0,2})";

    /* We get our matches using the regular expression. */
    regcomp(&preg, regex, REG_EXTENDED);
    regexec(&preg, time_str, 4, pmatch, 0);
    regfree(&preg);

    /* If there wasn't a match, there was an error. */
    if (pmatch[0].rm_eo == 0)
    {
	g_free(retvalue);
	return FALSE;
    }

    for (j=1; j<4; ++j)
    {
	/* Copy the matching substring into date_segment so that we can
	 * convert it into an integer. */
	mem_length = pmatch[j].rm_eo - pmatch[j].rm_so;
	if (mem_length > 2) continue; /* should not happen, my groups are smaller */
	memcpy(segment, time_str + pmatch[j].rm_so, mem_length);
	segment[mem_length] = '\0';
	switch (j)
	{
	    case 1:
		retvalue->tm_hour = atoi(segment);
		break;
	    case 2:
		retvalue->tm_min = atoi(segment);
		break;
	    case 3:
		retvalue->tm_sec = atoi(segment);
		break;
	}
    }
    price->timetm = retvalue;
    return TRUE;
}

static gboolean extract_value(char* str, GncPriceLine *price)
{
    char *endptr, *startptr;
    gnc_numeric *numeric;
    double value;
    startptr = str;
    /* skip leading '=' if any */
    if (startptr && startptr[0] == '=') ++startptr;

    /* Try to parse the string as a double */
    value = strtod(startptr, &endptr);
    if (endptr == startptr + strlen(startptr))
    {
	if (fabs(value) > 0.00001)
	{
	    price->value = g_new(gnc_numeric, 1);
	    *(price->value) = double_to_gnc_numeric(
		value, GNC_DENOM_AUTO, GNC_HOW_DENOM_REDUCE | GNC_HOW_RND_NEVER);
	}
	return TRUE;
    }

    /* Try to parse it as a gnc_numeric */
    numeric = g_new(gnc_numeric, 1);
    if (string_to_gnc_numeric(startptr, numeric))
    {
	price->value = numeric;
	return TRUE;
    }
    g_free(numeric);
    return FALSE;
}

/** Sets the value of the property by parsing str. Note: this should
 * only be called once on an instance of PriceProperty, as calling it
 * more than once can cause memory leaks.
 * @param prop The property being set
 * @param str The string to be parsed
 * @return TRUE on success, FALSE on failure
 */
static gboolean gnc_priceline_set(GncPriceLine* price, char* str, char columntype)
{
    switch (columntype)
    {
    case GNC_PRICE_DATE:
	return extract_date(str, price);
    case GNC_PRICE_TIME:
	return extract_time(str, price);
    case GNC_PRICE_NAMESPACE:
	price->namespace = g_strdup(str);
	return TRUE;
    case GNC_PRICE_MNEMONIC:
	price->mnemonic = g_strdup(str);
	return TRUE;
    case GNC_PRICE_CURRENCY:
	price->currency = g_strdup(str);
	return TRUE;
    case GNC_PRICE_TYPE:
	price->type = g_strdup(str);
	return TRUE;
    case GNC_PRICE_PRICE:
	return extract_value(str, price);
    }
    return FALSE; /* We should never actually get here. */
}

/** Tests a GncPriceLine is usable.
 * Essential properties are "Date", "Namespace", "Security", "Price", "Currency".
 * Additionnally, if "Time" is provided, it needs to be valid.
 * @param list The list we are checking
 * @param error Contains an error message on failure
 * @return TRUE if there are enough essentials; FALSE otherwise
 */
static gboolean gnc_priceline_verify_essentials(GncPriceLine *pl, gchar** error)
{
    int i;
    /* possible_errors lists the ways in which a list can fail this test. */
    enum PossibleErrorTypes {NO_DATE, NO_NAMESPACE, NO_MNEMONIC, NO_PRICE,
			     INVALID_DATETIME, NUM_OF_POSSIBLE_ERRORS};
    gchar* possible_errors[NUM_OF_POSSIBLE_ERRORS] =
    {
	N_("No date column."),
	N_("No namespace column."),
	N_("No security code column."),
	N_("No price column."),
	N_("Invalid date/time."),
    };
    int possible_error_lengths[NUM_OF_POSSIBLE_ERRORS] = {0};
    GList *errors_list = NULL;
 
    /* Go through each of the properties and erase possible errors. */
    if (pl->datetm) possible_errors[NO_DATE] = NULL;
    if (pl->namespace) possible_errors[NO_NAMESPACE] = NULL;
    if (pl->mnemonic) possible_errors[NO_MNEMONIC] = NULL;
    if (pl->value) possible_errors[NO_PRICE] = NULL;
    if (pl->datetm && pl->timetm)
    {
	time_t tm;
	pl->datetm->tm_hour = pl->timetm->tm_hour;
	pl->datetm->tm_min = pl->timetm->tm_min;
	pl->datetm->tm_sec = pl->timetm->tm_sec;
	pl->datetm->tm_isdst = -1;
	tm = mktime(pl->datetm);
	if (tm != -1)
	{
	    possible_errors[INVALID_DATETIME] = NULL;
	}
    }
    else
	possible_errors[INVALID_DATETIME] = NULL;

    /* Accumulate a list of the actual errors. */
    for (i = 0; i < NUM_OF_POSSIBLE_ERRORS; i++)
    {
	if (possible_errors[i] != NULL)
	{
	    errors_list = g_list_append(errors_list, GINT_TO_POINTER(i));
	    /* Since we added an error, we want to also store its length for
	     * when we construct the full error string. */
	    possible_error_lengths[i] = strlen(_(possible_errors[i]));
	}
    }

    /* If there are no errors, we can quit now. */
    if (errors_list == NULL)
	return TRUE;
    else
    {
	/* full_error_size is the full length of the error message. */
	int full_error_size = 0, string_length = 0;
	GList* errors_list_begin = errors_list;
	gchar *error_message, *error_message_begin;

	/* Find the value for full_error_size. */
	while (errors_list)
	{
	    /* We add an extra 1 to account for spaces in between messages. */
	    full_error_size += possible_error_lengths[GPOINTER_TO_INT(errors_list->data)] + 1;
	    errors_list = g_list_next(errors_list);
	}
	errors_list = errors_list_begin;

	/* Append the error messages one after another. */
	error_message = error_message_begin = g_new(gchar, full_error_size);
	while (errors_list)
	{
	    i = GPOINTER_TO_INT(errors_list->data);
	    string_length = possible_error_lengths[i];

	    /* Copy the error message and put a space after it. */
	    strncpy(error_message, _(possible_errors[i]), string_length);
	    error_message += string_length;
	    *error_message = ' ';
	    error_message++;

	    errors_list = g_list_next(errors_list);
	}
	*error_message = '\0'; /* Replace the last space with the null byte. */
	g_list_free(errors_list_begin);

	*error = error_message_begin;
	return FALSE;
    }
}

/** Constructor for GncPriceParseData.
 * @return Pointer to a new GncPriceParseData
 */
GncPriceParseData* gnc_price_new_parse_data(void)
{
    GncPriceParseData* parse_data = g_new(GncPriceParseData, 1);
    parse_data->encoding = "UTF-8";
    /* All of the data pointers are initially NULL. This is so that, if
     * gnc_price_parse_data_free is called before all of the data is
     * initialized, only the data that needs to be freed is freed. */
    parse_data->raw_str.begin = parse_data->raw_str.end = NULL;
    parse_data->file_str.begin = parse_data->file_str.end = NULL;
    parse_data->orig_lines = NULL;
    parse_data->orig_row_lengths = NULL;
    parse_data->column_types = NULL;
    parse_data->error_lines = parse_data->prices = NULL;
    parse_data->options = default_parse_options();
    parse_data->date_format = -1;
    parse_data->chunk = g_string_chunk_new(100 * 1024);
    parse_data->skip_headerline = FALSE;
    return parse_data;
}

/** Destructor for GncPriceParseData.
 * @param parse_data Parse data whose memory will be freed
 */
void gnc_price_parse_data_free(GncPriceParseData* parse_data)
{
    /* All non-NULL pointers have been initialized and must be freed. */

    if (parse_data->raw_mapping != NULL)
	g_mapped_file_free(parse_data->raw_mapping);

    if (parse_data->file_str.begin != NULL)
	g_free(parse_data->file_str.begin);

    if (parse_data->orig_lines != NULL)
	stf_parse_general_free(parse_data->orig_lines);

    if (parse_data->orig_row_lengths != NULL)
	g_array_free(parse_data->orig_row_lengths, FALSE);

    if (parse_data->options != NULL)
	stf_parse_options_free(parse_data->options);

    if (parse_data->column_types != NULL)
	g_array_free(parse_data->column_types, TRUE);

    if (parse_data->error_lines != NULL)
	g_list_free(parse_data->error_lines);

    if (parse_data->prices != NULL)
    {
	GList* prices = parse_data->prices;
	/* We have to free the GncPriceLine's that are at each node in
	 * the list before freeing the entire list. */
	do
	{
	    gnc_priceline_free((GncPriceLine*)prices->data);
	    prices = g_list_next(prices);
	}
	while (prices != NULL);
	g_list_free(parse_data->prices);
    }

    g_free(parse_data->chunk);
    g_free(parse_data);
}

/** Converts raw file data using a new encoding. This function must be
 * called after gnc_price_load_file only if gnc_price_load_file guessed
 * the wrong encoding.
 * @param parse_data Data that is being parsed
 * @param encoding Encoding that data should be translated using
 * @param error Will point to an error on failure
 * @return 0 on success, 1 on failure
 */
int gnc_price_convert_encoding(GncPriceParseData* parse_data, const char* encoding,
GError** error)
{
    gsize bytes_read, bytes_written;

    /* If parse_data->file_str has already been initialized it must be
     * freed first. (This should always be the case, since
     * gnc_price_load_file should always be called before this
     * function.) */
    if (parse_data->file_str.begin != NULL)
	g_free(parse_data->file_str.begin);

    /* Do the actual translation to UTF-8. */
    parse_data->file_str.begin = g_convert(parse_data->raw_str.begin,
    parse_data->raw_str.end - parse_data->raw_str.begin,
    "UTF-8", encoding, &bytes_read, &bytes_written,
    error);
    /* Handle errors that occur. */
    if (parse_data->file_str.begin == NULL)
	return 1;

    /* On success, save the ending pointer of the translated data and
     * the encoding type and return 0. */
    parse_data->file_str.end = parse_data->file_str.begin + bytes_written;
    parse_data->encoding = (gchar*)encoding;
    return 0;
}

/** Loads a file into a GncPriceParseData. This is the first function
 * that must be called after createing a new GncPriceParseData. If this
 * fails because the file couldn't be opened, no more functions can be
 * called on the parse data until this succeeds (or until it fails
 * because of an encoding guess error). If it fails because the
 * encoding could not be guessed, gnc_price_convert_encoding must be
 * called until it succeeds.
 * @param parse_data Data that is being parsed
 * @param filename Name of the file that should be opened
 * @param error Will contain an error if there is a failure
 * @return 0 on success, 1 on failure
 */
int gnc_price_load_file(GncPriceParseData* parse_data, const char* filename,
			GError** error)
{
    const char* guess_enc;

    /* Get the raw data first and handle an error if one occurs. */
    parse_data->raw_mapping = g_mapped_file_new(filename, FALSE, error);
    if (parse_data->raw_mapping == NULL)
    {
	/* TODO Handle file opening errors more specifically,
	 * e.g. inexistent file versus no read permission. */
	parse_data->raw_str.begin = NULL;
	g_set_error(error, 0, GNC_PRICE_FILE_OPEN_ERR, "%s", _("File opening failed."));
	return 1;
    }

    /* Copy the mapping's contents into parse-data->raw_str. */
    parse_data->raw_str.begin = g_mapped_file_get_contents(parse_data->raw_mapping);
    parse_data->raw_str.end = parse_data->raw_str.begin + g_mapped_file_get_length(parse_data->raw_mapping);

    /* Make a guess at the encoding of the data. */
    guess_enc = go_guess_encoding((const char*)(parse_data->raw_str.begin),
    (size_t)(parse_data->raw_str.end - parse_data->raw_str.begin),
    "UTF-8", NULL);
    if (guess_enc == NULL)
    {
	g_set_error(error, 0, GNC_PRICE_ENCODING_ERR, "%s", _("Unknown encoding."));
	return 1;
    }

    /* Convert using the guessed encoding into parse_data->file_str and
     * handle any errors that occur. */
    gnc_price_convert_encoding(parse_data, guess_enc, error);
    if (parse_data->file_str.begin == NULL)
    {
	g_set_error(error, 0, GNC_PRICE_ENCODING_ERR, "%s", _("Unknown encoding."));
	return 1;
    }
    else
	return 0;
}

/** Parses a file into cells. This requires having an encoding that
 * works (see gnc_price_convert_encoding). parse_data->options should be
 * set according to how the user wants before calling this
 * function. (Note: this function must be called with guessColTypes as
 * TRUE before it is ever called with it as FALSE.)
 * When guessColTypes is TRUE, parse_data->skip_headerline will be updated 
 * to true if a header is found.
 * @param parse_data Data that is being parsed
 * @param guessColTypes TRUE to guess what the types of columns are based on the cell contents
 * @param error Will contain an error if there is a failure
 * @return 0 on success, 1 on failure
 */
int gnc_price_parse(GncPriceParseData* parse_data, gboolean guessColTypes, GError** error)
{
    /* max_cols is the number of columns in the row with the most columns. */
    int i, max_cols = 0;

    if (parse_data->orig_lines != NULL)
    {
	stf_parse_general_free(parse_data->orig_lines);
    }

    /* If everything is fine ... */
    if (parse_data->file_str.begin != NULL)
    {
	/* Do the actual parsing. */
	parse_data->orig_lines = stf_parse_general(parse_data->options, parse_data->chunk,
	parse_data->file_str.begin,
	parse_data->file_str.end);
    }
    /* If we couldn't get the encoding right, we just want an empty array. */
    else
    {
	parse_data->orig_lines = g_ptr_array_new();
    }

    /* Record the original row lengths of parse_data->orig_lines. */
    if (parse_data->orig_row_lengths != NULL)
	g_array_free(parse_data->orig_row_lengths, FALSE);

    parse_data->orig_row_lengths =
	g_array_sized_new(FALSE, FALSE, sizeof(int), parse_data->orig_lines->len);
    g_array_set_size(parse_data->orig_row_lengths, parse_data->orig_lines->len);
    parse_data->orig_max_row = 0;
    for (i = 0; i < parse_data->orig_lines->len; i++)
    {
	int length = ((GPtrArray*)parse_data->orig_lines->pdata[i])->len;
	parse_data->orig_row_lengths->data[i] = length;
	if (length > parse_data->orig_max_row)
	    parse_data->orig_max_row = length;
    }

    /* If it failed, generate an error. */
    if (parse_data->orig_lines == NULL)
    {
	g_set_error(error, 0, 0, "Parsing failed.");
	return 1;
    }

    /* Now that we have data, let's set max_cols. */
    for (i = 0; i < parse_data->orig_lines->len; i++)
    {
	if (max_cols < ((GPtrArray*)(parse_data->orig_lines->pdata[i]))->len)
	    max_cols = ((GPtrArray*)(parse_data->orig_lines->pdata[i]))->len;
    }

    if (guessColTypes)
    {
	int nbguesses = 0;
	/* Free parse_data->column_types if it's already been created. */
	if (parse_data->column_types != NULL)
	    g_array_free(parse_data->column_types, TRUE);

	/* Create parse_data->column_types and fill it with guesses based
	 * on the contents of each column. */
	parse_data->column_types = g_array_sized_new(FALSE, FALSE, sizeof(int),
	max_cols);
	g_array_set_size(parse_data->column_types, max_cols);
	for (i = 0; i < parse_data->column_types->len; i++)
	{
	    /* try to look at header line */
	    /* max_cols > 0 implies there is at least one line */
	    GPtrArray *headerline = (GPtrArray*)g_ptr_array_index(parse_data->orig_lines, 0);
	    /* default is none */
	    parse_data->column_types->data[i] = GNC_PRICE_NONE;
	    if (i < headerline->len)
	    {
		const gchar* header = (const gchar*)g_ptr_array_index(headerline, i);
		int j;
		for (j=0; j<GNC_PRICE_NUM_COL_TYPES; ++j)
		{
		    if (g_strcmp0(header, gnc_price_column_type_strs[j]) == 0)
		    {
			parse_data->column_types->data[i] = j;
			++nbguesses;
			break;
		    }
		}
	    }
	}
	if (nbguesses > 0)
	{
	    /* There is obviously a header line */
	    parse_data->skip_headerline = TRUE;
	}
    }
    else
    {
	/* If we don't need to guess column types, we will simply set any
	 * new columns that are created that didn't exist before to "None"
	 * since we don't want gibberish to appear. Note:
	 * parse_data->column_types should have already been
	 * initialized, so we don't check for it being NULL. */
	int i = parse_data->column_types->len;
	g_array_set_size(parse_data->column_types, max_cols);
	for (; i < parse_data->column_types->len; i++)
	{
	    parse_data->column_types->data[i] = GNC_PRICE_NONE;
	}
    }

    if (parse_data->skip_headerline && parse_data->orig_lines->len)
    {
	GPtrArray *firstline = (GPtrArray*)parse_data->orig_lines->pdata[0];
	g_ptr_array_free(firstline, TRUE);
	g_ptr_array_remove_index(parse_data->orig_lines, 0);
    }

    return 0;
}

/** Creates a list of gnc_price_lines from parsed data. Prices that
 * could be created from rows are placed in parse_data->prices;
 * rows that fail are placed in parse_data->error_lines.
 * @param parse_data Data that is being parsed
 * @param redo_errors TRUE to convert only error data, FALSE for all data
 */
void gnc_price_parse_to_prices(GncPriceParseData* parse_data,
			       gboolean redo_errors)
{
    int i, j, max_cols = 0;
    GArray* column_types = parse_data->column_types;
    GList *error_lines = NULL, *begin_error_lines = NULL;

    /* last_price points to the last element in
     * parse_data->prices, or NULL if it's empty. */
    GList* last_price = NULL;

    /* Free parse_data->error_lines and parse_data->transactions if they
     * already exist. */
    if (redo_errors) /* If we're redoing errors, we save freeing until the end. */
    {
	begin_error_lines = error_lines = parse_data->error_lines;
    }
    else
    {
	if (parse_data->error_lines != NULL)
	{
	    g_list_free(parse_data->error_lines);
	}
	if (parse_data->prices != NULL)
	{
	    g_list_free(parse_data->prices);
	}
    }
    parse_data->error_lines = NULL;

    if (redo_errors) /* If we're looking only at error data ... */
    {
	if (parse_data->prices == NULL)
	{
	    last_price = NULL;
	}
	else
	{
	    /* Move last_transaction to the end. */
	    last_price = parse_data->prices;
	    while (g_list_next(last_price) != NULL)
	    {
		last_price = g_list_next(last_price);
	    }
	}
	/* ... we use only the lines in error_lines. */
	if (error_lines == NULL)
	    i = parse_data->orig_lines->len; /* Don't go into the for loop. */
	else
	    i = GPOINTER_TO_INT(error_lines->data);
    }
    else /* Otherwise, we look at all the data. */
    {
	/* The following while-loop effectively behaves like the following for-loop:
	 * for(i = 0; i < parse_data->orig_lines->len; i++). */
	i = 0;
	last_price = NULL;
    }
    while (i < parse_data->orig_lines->len)
    {
	GPtrArray* line = parse_data->orig_lines->pdata[i];
	/* This flag is TRUE if there are any errors in this row. */
	gboolean errors = FALSE;
	gchar* error_message = NULL;
	GncPriceLine* price_line = gnc_priceline_new(parse_data->date_format);

	for (j = 0; j < line->len; j++)
	{
	    /* We do nothing in "None" columns. */
	    if (column_types->data[j] != GNC_PRICE_NONE)
	    {
		/* Affect the transaction appropriately. */
		gboolean succeeded = gnc_priceline_set(price_line, line->pdata[j], column_types->data[j]);
		if (!succeeded)
		{
		    errors = TRUE;
		    error_message = g_strdup_printf(_("%s column could not be understood."),
						    _(gnc_price_column_type_strs[(int)column_types->data[j]]));
		    break;
		}
	    }
	}

	/* If we had success, add the price to parse_data->prices. */
	if (!errors)
	{
	    errors = !gnc_priceline_verify_essentials(price_line, &error_message);
	}

	/* If there were errors, add this line to parse_data->error_lines. */
	if (errors)
	{
	    parse_data->error_lines = g_list_append(parse_data->error_lines,
						    GINT_TO_POINTER(i));
	    /* If there's already an error message, we need to replace it. */
	    if (line->len > (int)(parse_data->orig_row_lengths->data[i]))
	    {
		g_free(line->pdata[line->len - 1]);
		line->pdata[line->len - 1] = error_message;
	    }
	    else
	    {
		/* Put the error message at the end of the line. */
		g_ptr_array_add(line, error_message);
	    }
	}
	else
	{
	    /* If all went well, add this transaction to the list. */
	    price_line->line_no = i;

	    /* We keep the prices sorted by original line number */

	    /* If we can just put it at the end, do so and increment last_transaction. */
	    if (last_price == NULL ||
		((GncPriceLine*)(last_price->data))->line_no <= price_line->line_no)
	    {
		parse_data->prices = g_list_append(parse_data->prices, price_line);
		/* If this is the first price, we need to get last_price on track. */
		if (last_price == NULL)
		    last_price = parse_data->prices;
		else /* Otherwise, we can just continue. */
		    last_price = g_list_next(last_price);
	    }
	    /* Otherwise, search backward for the correct spot. */
	    else
	    {
		GList* insertion_spot = last_price;
		while (insertion_spot != NULL &&
		       ((GncPriceLine*)(insertion_spot->data))->line_no > price_line->line_no)
		{
		    insertion_spot = g_list_previous(insertion_spot);
		}
		/* Move insertion_spot one location forward since we have to
		 * use the g_list_insert_before function. */
		if (insertion_spot == NULL) /* We need to handle the case of inserting at the beginning of the list. */
		    insertion_spot = parse_data->prices;
		else
		    insertion_spot = g_list_next(insertion_spot);

		parse_data->prices = g_list_insert_before(parse_data->prices, insertion_spot, price_line);
	    }
	}

	/* Increment to the next row. */
	if (redo_errors)
	{
	    /* Move to the next error line in the list. */
	    error_lines = g_list_next(error_lines);
	    if (error_lines == NULL)
		i = parse_data->orig_lines->len; /* Don't continue the for loop. */
	    else
		i = GPOINTER_TO_INT(error_lines->data);
	}
	else
	{
	    i++;
	}
    }

    if (redo_errors) /* Now that we're at the end, we do the freeing. */
    {
	g_list_free(begin_error_lines);
    }

    /* We need to resize parse_data->column_types since errors may have added columns. */
    for (i = 0; i < parse_data->orig_lines->len; i++)
    {
	if (max_cols < ((GPtrArray*)(parse_data->orig_lines->pdata[i]))->len)
	    max_cols = ((GPtrArray*)(parse_data->orig_lines->pdata[i]))->len;
    }
    i = parse_data->column_types->len;
    parse_data->column_types = g_array_set_size(parse_data->column_types, max_cols);
    for (; i < max_cols; i++)
    {
	parse_data->column_types->data[i] = GNC_PRICE_NONE;
    }
}

static void debug_price(GNCPrice* p)
{
    gchar datetime[40];
    gnc_timespec_to_iso8601_buff (gnc_price_get_time(p), datetime);
    DEBUG("GNCPrice: date %s, commodity %s, price %s%s, type %s, source %s",
	  datetime,
	  gnc_commodity_get_printname(gnc_price_get_commodity(p)),
	  gnc_num_dbg_to_string(gnc_price_get_value(p)),
	  gnc_commodity_get_mnemonic(gnc_price_get_currency(p)),
	  gnc_price_get_typestr(p),
	  gnc_price_get_source(p));
}

/** Create a GncPriceLine from a PricePropertyList.
 * @param prices; List of GncPriceLines to import 
 */
void gnc_price_do_import(GList *prices, GncPriceImportReport *report)
{
    QofBook* book = qof_session_get_book (gnc_get_current_session());
    gnc_commodity_table *cm_table = gnc_commodity_table_get_table (book);
    GNCPriceDB *db = gnc_pricedb_get_db (book);

    if (!report) return;
    report->nb_imported = report->nb_skipped = report->nb_securities_created = 0;

    for (; prices; prices = prices->next)
    {
	GncPriceLine* pl = (GncPriceLine*)prices->data;
	Timespec ts;
	time_t tm;
	gnc_commodity *cm, *currency;
	GNCPrice *new_price;
	gboolean skipit = FALSE;
	PriceList *existing_prices, *iterprices;
    
	cm = gnc_commodity_table_lookup(cm_table, pl->namespace, pl->mnemonic);
	if (!cm)
	{
	    PINFO("Unknown commodity %s:%s - will create", pl->namespace, pl->mnemonic);
	    cm = gnc_commodity_new(book, pl->mnemonic, pl->namespace, pl->mnemonic, "", 10000);
	    gnc_commodity_table_insert(cm_table, cm);
	    ++report->nb_securities_created;
	}
	if (pl->currency)
	    currency = gnc_commodity_table_lookup(cm_table, GNC_COMMODITY_NS_CURRENCY,
						  pl->currency);
	else
	    currency = gnc_default_currency();
	
	/* timetm should have been merged into datetm by verify_essential */
	tm = mktime(pl->datetm);
	timespecFromTime64(&ts, tm);
	if (!pl->timetm)
	    ts = timespecCanonicalDayTime(ts);

	existing_prices = gnc_pricedb_lookup_at_time(db, cm, currency, ts);
	for (iterprices = existing_prices; iterprices; iterprices = iterprices->next)
	{
	    if (gnc_numeric_equal(*pl->value, gnc_price_get_value(iterprices->data)))
	    {
		PINFO("Line %d: price already in database", pl->line_no);
		debug_price(iterprices->data);
		++report->nb_skipped;
		skipit = TRUE;
	    }
	    gnc_price_unref(iterprices->data);
	}
	g_list_free(existing_prices);
	if (skipit) continue;
	    
	new_price = gnc_price_create(book);
	gnc_price_begin_edit(new_price);
	gnc_price_set_commodity(new_price, cm);
	gnc_price_set_currency(new_price, currency);
	gnc_price_set_time(new_price, ts);
	gnc_price_set_source(new_price, "user:import");
	if (pl->type)
	    gnc_price_set_typestr(new_price, pl->type);
	gnc_price_set_value(new_price, *pl->value);
	gnc_price_commit_edit(new_price);
	if (report->nb_imported == 0)
	    gnc_pricedb_begin_edit(db);
	gnc_pricedb_add_price(db, new_price);
	PINFO("Line %d: inserted new price in database", pl->line_no);
	debug_price(new_price);
	gnc_price_unref(new_price);
	++report->nb_imported;
    }
    if (report->nb_imported > 0)
	gnc_pricedb_commit_edit(db);

    PINFO("Imported %d prices, skipped %d, created %d securities",
	  report->nb_imported, report->nb_skipped, report->nb_securities_created);
}
