/********************************************************************
 * gnc-backend-gda.c: load and save data to SQL via libgda          *
 *                                                                  *
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
/** @file gnc-backend-gda.c
 *  @brief load and save data to SQL 
 *  @author Copyright (c) 2006 Phil Longstaff <plongstaff@rogers.com>
 *
 * This file implements the top-level QofBackend API for saving/
 * restoring data to/from an SQL db using libgda
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libgda/libgda.h>

#include "qof.h"
#include "qofquery-p.h"
#include "qofquerycore-p.h"
#include "TransLog.h"
#include "gnc-engine.h"
#include "SX-book.h"
#include "Recurrence.h"

#include "gnc-backend-util-gda.h"
#include "gnc-gconf-utils.h"

#include "gnc-account-gda.h"
#include "gnc-book-gda.h"
#include "gnc-budget-gda.h"
#include "gnc-commodity-gda.h"
#include "gnc-lots-gda.h"
#include "gnc-price-gda.h"
#include "gnc-pricedb.h"
#include "gnc-recurrence-gda.h"
#include "gnc-schedxaction-gda.h"
#include "gnc-slots-gda.h"
#include "gnc-transaction-gda.h"

#include "gnc-backend-gda.h"

static const gchar* convert_search_obj( QofIdType objType );
static void gnc_gda_init_object_handlers( void );
static void add_table_column( GdaServerProvider* server, GdaConnection* cnn,
            xmlNodePtr array_data, const gchar* arg, const gchar* dbms_type,
            gint size, gint flags );

typedef struct {
    QofIdType searchObj;
    gpointer pCompiledQuery;
} gnc_gda_query_info;

/* callback structure */
typedef struct {
    gboolean ok;
    GncGdaBackend* be;
    QofInstance* inst;
    QofQuery* pQuery;
    gpointer pCompiledQuery;
    gnc_gda_query_info* pQueryInfo;
} gda_backend;

static QofLogModule log_module = GNC_MOD_BACKEND;

#define SQLITE_PROVIDER_NAME "SQLite"

/* ================================================================= */

static void
create_tables_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );

    if( pData->create_tables != NULL ) {
        (pData->create_tables)( be_data->be );
    }
}

static void
gnc_gda_session_begin(QofBackend *be_start, QofSession *session, 
                   const gchar *book_id,
                   gboolean ignore_lock,
				   gboolean create_if_nonexistent)
{
    GncGdaBackend *be = (GncGdaBackend*) be_start;
    GError* error = NULL;
    gda_backend be_data;
    gchar* book_info;
    gchar* dsn;
    gchar* username = "";
    gchar* password = "";

    ENTER (" ");

    be->pClient = gda_client_new();
	be->pConnection = NULL;

    /* Split book_id into provider and connection string.  If there's no
	provider, use "file" */
    book_info = g_strdup( book_id );
    dsn = strchr( book_info, ':' );
	if( dsn != NULL ) {
    	*dsn = '\0';
    	dsn += 3;						// Skip '://'

		// String will be one of:
		//
		//    sqlite:<filename>
		//    mysql:<dbname>
		//    pgsql:<dbname>
		//    @<gda_connectionname>

		if( dsn[0] == '@' ) {
	    	dsn++;

	    	be->pConnection = gda_client_open_connection( be->pClient,
													dsn,
													username, password,
													0,
													&error );
		}
	}

	if( dsn == NULL || dsn[0] != '@' ) {
		gchar* provider;
		GList* provider_list;
		GList* l;
		gboolean provider_found;
		
		if( dsn != NULL ) {
			provider = dsn;
	    	dsn = strchr( dsn, ':' );
			*dsn = '\0';
			dsn++;
		} else {
			provider = SQLITE_PROVIDER_NAME;
			dsn = book_info;
		}

		// Get a list of all of the providers.  If the requested provider is on the list, use it.
		// Note that we need a case insensitive comparison here
		provider_list = gda_config_get_provider_list();

		provider_found = FALSE;
		for( l = provider_list; l != NULL; l = l->next ) {
			GdaProviderInfo* provider_info = (GdaProviderInfo*)l->data;

			if( provider_info != NULL && g_ascii_strcasecmp( provider_info->id, provider ) == 0 ) {
				provider_found = TRUE;
				provider = provider_info->id;
				break;
			}
		}

		if( provider_found ) {
			gchar* cnc;

		    // If the provider is SQLite, split the file name into DB_DIR and
			// DB_NAME
			if( strcmp( provider, SQLITE_PROVIDER_NAME ) == 0 ) {
				gchar* dirname;
				gchar* basename;

				dirname = g_path_get_dirname( dsn );
				basename = g_path_get_basename( dsn );
				
				// Remove .db from the base name if it exists
				if( g_str_has_suffix( basename, ".db" ) ) {
					gchar* bn = g_strdup( basename );
					gchar* suffix = g_strrstr( bn, ".db" );
					*suffix = '\0';

					cnc = g_strdup_printf( "DB_DIR=%s;DB_NAME=%s", dirname, bn );
					g_free( bn );
				} else {
					cnc = g_strdup_printf( "DB_DIR=%s;DB_NAME=%s",
											dirname, basename );
				}
				g_free( dirname );
				g_free( basename );
			} else {
			    cnc = g_strdup( dsn );
			}

			be->pConnection = gda_client_open_connection_from_string( be->pClient,
									provider, 
									cnc,
									username, password,
									0,
									&error );

		    if( be->pConnection == NULL ) {
				GdaServerOperation* op = gda_client_prepare_create_database(
													be->pClient,
													dsn,
													provider );
				if( op != NULL ) {
					gboolean isOK;
					isOK = gda_client_perform_create_database(
													be->pClient,
													op,
													&error );
					if( isOK ) {
						be->pConnection = gda_client_open_connection_from_string(
													be->pClient,
													provider, 
													cnc,
													username, password,
													0,
													&error );
					}
				}
			}
		g_free( cnc );
		}
	}
    g_free( book_info );

    if( be->pConnection == NULL ) {
        g_critical( "SQL error: %s\n", error->message );
        qof_backend_set_error( be_start, ERR_BACKEND_NO_SUCH_DB );

        LEAVE( " " );
        return;
    }

    // Set up the dictionary
    be->pDict = gda_dict_new();
    gda_dict_set_connection( be->pDict, be->pConnection );
    gda_dict_update_dbms_meta_data( be->pDict, 0, NULL, &error );
    if( error != NULL ) {
        g_critical( "gda_dict_update_dbms_meta_data() error: %s\n", error->message );
    }

    // Call all object backends to create any required tables
    be_data.ok = FALSE;
    be_data.be = be;
    be_data.inst = NULL;
    qof_object_foreach_backend( GNC_GDA_BACKEND, create_tables_cb, &be_data );

    // Update the dictionary because new tables may exist
    gda_dict_update_dbms_meta_data( be->pDict, 0, NULL, &error );
    if( error != NULL ) {
        g_critical( "gda_dict_update_dbms_meta_data() error: %s\n", error->message );
    }

    LEAVE (" ");
}

/* ================================================================= */

static void
gnc_gda_session_end(QofBackend *be_start)
{
    GncGdaBackend *be = (GncGdaBackend*)be_start;
    ENTER (" ");

    if( be->pDict != NULL ) {
        g_object_unref( G_OBJECT(be->pDict) );
        be->pDict = NULL;
    }
    if( be->pConnection != NULL && gda_connection_is_opened( be->pConnection ) ) {
        gda_connection_close( be->pConnection );
    }
    be->pConnection = NULL;
    if( be->pClient != NULL ) {
        g_object_unref( G_OBJECT(be->pClient ) );
        be->pClient = NULL;
    }

    LEAVE (" ");
}

static void
gnc_gda_destroy_backend(QofBackend *be)
{
    g_free(be);
}

/* ================================================================= */

static void
initial_load_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );
    g_return_if_fail( g_ascii_strcasecmp( type, GNC_ID_BOOK ) != 0 );

    if( pData->initial_load != NULL ) {
        (pData->initial_load)( be_data->be );
    }
}

static void
gnc_gda_load(QofBackend* be_start, QofBook *book)
{
    GncGdaBackend *be = (GncGdaBackend*)be_start;
    gda_backend be_data;
    GncGdaDataType_t* pData;

    ENTER( "be=%p, book=%p", be, book );

    g_assert( be->primary_book == NULL );
    be->primary_book = book;

    /* Load any initial stuff */
    be->loading = TRUE;
    
    /* Some of this needs to happen in a certain order */
    pData = qof_object_lookup_backend( GNC_ID_BOOK, GNC_GDA_BACKEND );
    if( pData->initial_load != NULL ) {
        (pData->initial_load)( be );
    }

    be_data.ok = FALSE;
    be_data.be = be;
    be_data.inst = NULL;
    qof_object_foreach_backend( GNC_GDA_BACKEND, initial_load_cb, &be_data );

    be->loading = FALSE;

	// Mark the book as clean
	qof_instance_mark_clean( QOF_INSTANCE(book) );

    LEAVE( "" );
}

/* ================================================================= */

static gint
compare_namespaces(gconstpointer a, gconstpointer b)
{
  const gchar *sa = (const gchar *) a;
  const gchar *sb = (const gchar *) b;
  return(safe_strcmp(sa, sb));
}

static gint
compare_commodity_ids(gconstpointer a, gconstpointer b)
{
  const gnc_commodity *ca = (const gnc_commodity *) a;
  const gnc_commodity *cb = (const gnc_commodity *) b;
  return(safe_strcmp(gnc_commodity_get_mnemonic(ca),
                     gnc_commodity_get_mnemonic(cb)));
}

static void
save_commodities( GncGdaBackend* be, QofBook* book )
{
    gnc_commodity_table* tbl;
    GList* namespaces;
    GList* lp;

    tbl = gnc_book_get_commodity_table( book );
    namespaces = gnc_commodity_table_get_namespaces( tbl );
    if( namespaces != NULL ) {
        namespaces = g_list_sort( namespaces, compare_namespaces );
    }
    for( lp = namespaces; lp != NULL; lp = lp->next ) {
        GList* comms;
        GList* lp2;
        
        comms = gnc_commodity_table_get_commodities( tbl, lp->data );
        comms = g_list_sort( comms, compare_commodity_ids );

        for( lp2 = comms; lp2 != NULL; lp2 = lp2->next ) {
	    gnc_gda_save_commodity( be, GNC_COMMODITY(lp2->data) );
        }
    }
}

static void
save_account_tree( GncGdaBackend* be, Account* root )
{
    GList* descendants;
    GList* node;

    descendants = gnc_account_get_descendants( root );
    for( node = descendants; node != NULL; node = g_list_next(node) ) {
        gnc_gda_save_account( be, QOF_INSTANCE(GNC_ACCOUNT(node->data)) );
    }
    g_list_free( descendants );
}

static void
save_accounts( GncGdaBackend* be, QofBook* book )
{
    save_account_tree( be, gnc_book_get_root_account( book ) );
}

static void
write_budget( QofInstance* ent, gpointer data )
{
    GncGdaBackend* be = (GncGdaBackend*)data;

    gnc_gda_save_budget( be, ent );
}

static void
save_budgets( GncGdaBackend* be, QofBook* book )
{
    qof_collection_foreach( qof_book_get_collection( book, GNC_ID_BUDGET ),
                            write_budget, be );
}

static gboolean
save_price( GNCPrice* p, gpointer data )
{
    GncGdaBackend* be = (GncGdaBackend*)data;

    gnc_gda_save_price( be, QOF_INSTANCE(p) );

    return TRUE;
}

static void
save_prices( GncGdaBackend* be, QofBook* book )
{
    GNCPriceDB* priceDB = gnc_book_get_pricedb( book );

    gnc_pricedb_foreach_price( priceDB, save_price, be, TRUE );
}

static int
save_tx( Transaction* tx, gpointer data )
{
    GncGdaBackend* be = (GncGdaBackend*)data;

    gnc_gda_save_transaction( be, QOF_INSTANCE(tx) );

    return 0;
}

static void
save_transactions( GncGdaBackend* be, QofBook* book )
{
    xaccAccountTreeForEachTransaction( gnc_book_get_root_account( book ),
                                       save_tx,
                                       (gpointer)be );
}

static void
save_template_transactions( GncGdaBackend* be, QofBook* book )
{
    Account* ra;

    ra = gnc_book_get_template_root( book );
    if( gnc_account_n_descendants( ra ) > 0 ) {
        save_account_tree( be, ra );
        xaccAccountTreeForEachTransaction( ra, save_tx, (gpointer)be );
    }
}

static void
save_schedXactions( GncGdaBackend* be, QofBook* book )
{
    GList* schedXactions;
    SchedXaction* tmpSX;

    schedXactions = gnc_book_get_schedxactions( book )->sx_list;

    for( ; schedXactions != NULL; schedXactions = schedXactions->next ) {
        tmpSX = schedXactions->data;
	gnc_gda_save_schedxaction( be, QOF_INSTANCE( tmpSX ) );
    }
}

static void
gnc_gda_sync_all( QofBackend* be, QofBook *book )
{
    GncGdaBackend *fbe = (GncGdaBackend *) be;
    GdaDataModel* tables;
    GError* error = NULL;
    gint row;
    gint numTables;
    gda_backend be_data;

    ENTER ("book=%p, primary=%p", book, fbe->primary_book);

    /* Destroy the current contents of the database */
    tables = gda_connection_get_schema( fbe->pConnection,
                                        GDA_CONNECTION_SCHEMA_TABLES,
                                        NULL,
                                        &error );
    if( error != NULL ) {
        g_critical( "SQL error: %s\n", error->message );
    }
    numTables = gda_data_model_get_n_rows( tables );
    for( row = 0; row < numTables; row++ ) {
        const GValue* row_value;
        const gchar* table_name;

        row_value = gda_data_model_get_value_at( tables, 0, row );
        table_name = g_value_get_string( row_value );
        error = NULL;
        if( !gda_drop_table( fbe->pConnection, table_name, &error ) ) {
            g_critical( "Unable to drop table %s\n", table_name );
            if( error != NULL ) {
                g_critical( "SQL error: %s\n", error->message );
            }
        }
    }

    // Update the dictionary because new tables may exist
    gda_dict_update_dbms_meta_data( fbe->pDict, 0, NULL, &error );
    if( error != NULL ) {
        g_critical( "gda_dict_update_dbms_meta_data() error: %s\n", error->message );
    }

    /* Create new tables */
    be_data.ok = FALSE;
    be_data.be = fbe;
    be_data.inst = NULL;
    qof_object_foreach_backend( GNC_GDA_BACKEND, create_tables_cb, &be_data );

    // Update the dictionary because new tables may exist
    gda_dict_update_dbms_meta_data( fbe->pDict, 0, NULL, &error );
    if( error != NULL ) {
        g_critical( "gda_dict_update_dbms_meta_data() error: %s\n", error->message );
    }

    /* Save all contents */
    //save_commodities( fbe, book );
    save_accounts( fbe, book );
    save_prices( fbe, book );
    save_transactions( fbe, book );
    save_template_transactions( fbe, book );
    save_schedXactions( fbe, book );
    save_budgets( fbe, book );

    LEAVE ("book=%p", book);
}

/* ================================================================= */
/* Routines to deal with the creation of multiple books. */


static void
gnc_gda_begin_edit (QofBackend *be, QofInstance *inst)
{
}

static void
gnc_gda_rollback_edit (QofBackend *be, QofInstance *inst)
{
}

static void
commit_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );

    /* If this has already been handled, or is not the correct handler, return */
    g_return_if_fail( strcmp( pData->type_name, be_data->inst->e_type ) == 0 );
    g_return_if_fail( !be_data->ok );

    if( pData->commit != NULL ) {
        (pData->commit)( be_data->be, be_data->inst );
        be_data->ok = TRUE;
    }
}

/* Commit_edit handler - find the correct backend handler for this object
 * type and call its commit handler
 */
static void
gnc_gda_commit_edit (QofBackend *be_start, QofInstance *inst)
{
    GncGdaBackend *be = (GncGdaBackend*)be_start;
    gda_backend be_data;

    ENTER( " " );

    /* During initial load where objects are being created, don't commit
    anything */

    if( be->loading ) {
		LEAVE( "" );
	    return;
	}

    g_debug( "gda_commit_edit(): %s dirty = %d, do_free=%d\n",
             (inst->e_type ? inst->e_type : "(null)"),
             qof_instance_get_dirty_flag(inst), qof_instance_get_destroying(inst) );

    if( !qof_instance_get_dirty_flag(inst) && !qof_instance_get_destroying(inst) && GNC_IS_TRANS(inst) ) {
        gnc_gda_transaction_commit_splits( be, GNC_TRANS(inst) );
    }

    if( !qof_instance_get_dirty_flag(inst) && !qof_instance_get_destroying(inst) ) return;

    be_data.ok = FALSE;
    be_data.be = be;
    be_data.inst = inst;
    qof_object_foreach_backend( GNC_GDA_BACKEND, commit_cb, &be_data );

    if( !be_data.ok ) {
        g_critical( "gnc_gda_commit_edit(): Unknown object type %s\n",
                inst->e_type );
        return;
    }

    qof_instance_mark_clean(inst);
    qof_book_mark_saved( be->primary_book );

	LEAVE( "" );
}
/* ---------------------------------------------------------------------- */

/* Query processing */

static const gchar*
convert_search_obj( QofIdType objType )
{
    return (gchar*)objType;
}

static void
handle_and_term( QofQueryTerm* pTerm, gchar* sql )
{
    GSList* pParamPath = qof_query_term_get_param_path( pTerm );
    QofQueryPredData* pPredData = qof_query_term_get_pred_data( pTerm );
    gboolean isInverted = qof_query_term_is_inverted( pTerm );
    GSList* name;
    gchar val[GUID_ENCODING_LENGTH+1];

    strcat( sql, "(" );
    if( isInverted ) {
        strcat( sql, "!" );
    }

    for( name = pParamPath; name != NULL; name = name->next ) {
        if( name != pParamPath ) strcat( sql, "." );
        strcat( sql, name->data );
    }

    if( pPredData->how == QOF_COMPARE_LT ) {
        strcat( sql, "<" );
    } else if( pPredData->how == QOF_COMPARE_LTE ) {
        strcat( sql, "<=" );
    } else if( pPredData->how == QOF_COMPARE_EQUAL ) {
        strcat( sql, "=" );
    } else if( pPredData->how == QOF_COMPARE_GT ) {
        strcat( sql, ">" );
    } else if( pPredData->how == QOF_COMPARE_GTE ) {
        strcat( sql, ">=" );
    } else if( pPredData->how == QOF_COMPARE_NEQ ) {
        strcat( sql, "~=" );
    } else {
        strcat( sql, "??" );
    }

    if( strcmp( pPredData->type_name, "string" ) == 0 ) {
        query_string_t pData = (query_string_t)pPredData;
        strcat( sql, "'" );
        strcat( sql, pData->matchstring );
        strcat( sql, "'" );
    } else if( strcmp( pPredData->type_name, "date" ) == 0 ) {
        query_date_t pData = (query_date_t)pPredData;

        (void)gnc_timespec_to_iso8601_buff( pData->date, val );
        strcat( sql, "'" );
        strncat( sql, val, 4+1+2+1+2 );
        strcat( sql, "'" );
    } else if( strcmp( pPredData->type_name, "numeric" ) == 0 ) {
        query_numeric_t pData = (query_numeric_t)pPredData;
    
        strcat( sql, "numeric" );
    } else if( strcmp( pPredData->type_name, "guid" ) == 0 ) {
        query_guid_t pData = (query_guid_t)pPredData;
        (void)guid_to_string_buff( pData->guids->data, val );
        strcat( sql, "'" );
        strcat( sql, val );
        strcat( sql, "'" );
    } else if( strcmp( pPredData->type_name, "gint32" ) == 0 ) {
        query_int32_t pData = (query_int32_t)pPredData;

        sprintf( val, "%d", pData->val );
        strcat( sql, val );
    } else if( strcmp( pPredData->type_name, "gint64" ) == 0 ) {
        query_int64_t pData = (query_int64_t)pPredData;
    
        sprintf( val, "%" G_GINT64_FORMAT, pData->val );
        strcat( sql, val );
    } else if( strcmp( pPredData->type_name, "double" ) == 0 ) {
        query_double_t pData = (query_double_t)pPredData;

        sprintf( val, "%f", pData->val );
        strcat( sql, val );
    } else if( strcmp( pPredData->type_name, "boolean" ) == 0 ) {
        query_boolean_t pData = (query_boolean_t)pPredData;

        sprintf( val, "%d", pData->val );
        strcat( sql, val );
    } else {
        g_assert( FALSE );
    }

    strcat( sql, ")" );
}

static void
compile_query_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );

    g_return_if_fail( strcmp( type, be_data->pQueryInfo->searchObj ) == 0 );
    g_return_if_fail( !be_data->ok );

    if( pData->compile_query != NULL ) {
        be_data->pQueryInfo->pCompiledQuery = (pData->compile_query)(
                                                            be_data->be,
                                                            be_data->pQuery );
        be_data->ok = TRUE;
    }
}

static gpointer
gnc_gda_compile_query(QofBackend* pBEnd, QofQuery* pQuery)
{
    GncGdaBackend *be = (GncGdaBackend*)pBEnd;
    GList* pBookList;
    QofIdType searchObj;
    gchar sql[1000];
    gda_backend be_data;
    gnc_gda_query_info* pQueryInfo;

	ENTER( " " );

    searchObj = qof_query_get_search_for( pQuery );

    pQueryInfo = g_malloc( sizeof( gnc_gda_query_info ) );

    // Try various objects first
    be_data.ok = FALSE;
    be_data.be = be;
    be_data.pQuery = pQuery;
    pQueryInfo->searchObj = searchObj;
    be_data.pQueryInfo = pQueryInfo;

    qof_object_foreach_backend( GNC_GDA_BACKEND, compile_query_cb, &be_data );
    if( be_data.ok ) {
		LEAVE( "" );
        return be_data.pQueryInfo;
    }

    pBookList = qof_query_get_books( pQuery );

    /* Convert search object type to table name */
    sprintf( sql, "SELECT * from %s", convert_search_obj( searchObj ) );
    if( !qof_query_has_terms( pQuery ) ) {
        strcat( sql, ";" );
    } else {
        GList* pOrTerms = qof_query_get_terms( pQuery );
        GList* orTerm;

        strcat( sql, " WHERE " );

        for( orTerm = pOrTerms; orTerm != NULL; orTerm = orTerm->next ) {
            GList* pAndTerms = (GList*)orTerm->data;
            GList* andTerm;

            if( orTerm != pOrTerms ) strcat( sql, " OR " );
            strcat( sql, "(" );
            for( andTerm = pAndTerms; andTerm != NULL; andTerm = andTerm->next ) {
                if( andTerm != pAndTerms ) strcat( sql, " AND " );
                handle_and_term( (QofQueryTerm*)andTerm->data, sql );
            }
            strcat( sql, ")" );
        }
    }

    g_debug( "Compiled: %s\n", sql );
    pQueryInfo->pCompiledQuery =  g_strdup( sql );

	LEAVE( "" );

    return pQueryInfo;
}

static void
free_query_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );
    g_return_if_fail( strcmp( type, be_data->pQueryInfo->searchObj ) == 0 );

    g_return_if_fail( !be_data->ok );

    if( pData->free_query != NULL ) {
        (pData->free_query)( be_data->be, be_data->pCompiledQuery );
        be_data->ok = TRUE;
    }
}

static void
gnc_gda_free_query(QofBackend* pBEnd, gpointer pQuery)
{
    GncGdaBackend *be = (GncGdaBackend*)pBEnd;
    gnc_gda_query_info* pQueryInfo = (gnc_gda_query_info*)pQuery;
    gda_backend be_data;

	ENTER( " " );

    // Try various objects first
    be_data.ok = FALSE;
    be_data.be = be;
    be_data.pCompiledQuery = pQuery;
    be_data.pQueryInfo = pQueryInfo;

    qof_object_foreach_backend( GNC_GDA_BACKEND, free_query_cb, &be_data );
    if( be_data.ok ) {
		LEAVE( "" );
        return;
    }

    g_debug( "gda_free_query(): %s\n", (gchar*)pQueryInfo->pCompiledQuery );
    g_free( pQueryInfo->pCompiledQuery );
    g_free( pQueryInfo );

	LEAVE( "" );
}

static void
run_query_cb( const gchar* type, gpointer data_p, gpointer be_data_p )
{
    GncGdaDataType_t* pData = data_p;
    gda_backend* be_data = be_data_p;

    g_return_if_fail( type != NULL && pData != NULL && be_data != NULL );
    g_return_if_fail( pData->version == GNC_GDA_BACKEND_VERSION );
    g_return_if_fail( strcmp( type, be_data->pQueryInfo->searchObj ) == 0 );

    g_return_if_fail( !be_data->ok );

    if( pData->run_query != NULL ) {
        (pData->run_query)( be_data->be, be_data->pCompiledQuery );
        be_data->ok = TRUE;
    }
}

static void
gnc_gda_run_query(QofBackend* pBEnd, gpointer pQuery)
{
    GncGdaBackend *be = (GncGdaBackend*)pBEnd;
    gnc_gda_query_info* pQueryInfo = (gnc_gda_query_info*)pQuery;
    gda_backend be_data;

    g_return_if_fail( !be->in_query );

	ENTER( " " );

    be->loading = TRUE;
    be->in_query = TRUE;

    qof_event_suspend();

    // Try various objects first
    be_data.ok = FALSE;
    be_data.be = be;
    be_data.pCompiledQuery = pQueryInfo->pCompiledQuery;
    be_data.pQueryInfo = pQueryInfo;

    qof_object_foreach_backend( GNC_GDA_BACKEND, run_query_cb, &be_data );
    be->loading = FALSE;
    be->in_query = FALSE;
    qof_event_resume();
//    if( be_data.ok ) {
//		LEAVE( "" );
//       	return;
//    }

	// Mark the book as clean
	qof_instance_mark_clean( QOF_INSTANCE(be->primary_book) );

//    g_debug( "gda_run_query(): %s\n", (gchar*)pQueryInfo->pCompiledQuery );

	LEAVE( "" );
}

/* ================================================================= */
static void
gnc_gda_init_object_handlers( void )
{
    gnc_gda_init_book_handler();
    gnc_gda_init_commodity_handler();
    gnc_gda_init_account_handler();
    gnc_gda_init_budget_handler();
    gnc_gda_init_price_handler();
    gnc_gda_init_transaction_handler();
    gnc_gda_init_slots_handler();
	gnc_gda_init_recurrence_handler();
    gnc_gda_init_schedxaction_handler();
    gnc_gda_init_lot_handler();
}

/* ================================================================= */

static QofBackend*
gnc_gda_backend_new(void)
{
    GncGdaBackend *gnc_be;
    QofBackend *be;
    static gboolean initialized = FALSE;

    gnc_be = g_new0(GncGdaBackend, 1);
    be = (QofBackend*) gnc_be;
    qof_backend_init(be);

    be->session_begin = gnc_gda_session_begin;
    be->session_end = gnc_gda_session_end;
    be->destroy_backend = gnc_gda_destroy_backend;

    be->load = gnc_gda_load;
    be->save_may_clobber_data = NULL;

    /* The gda backend treats accounting periods transactionally. */
    be->begin = gnc_gda_begin_edit;
    be->commit = gnc_gda_commit_edit;
    be->rollback = gnc_gda_rollback_edit;

    /* The gda backend uses queries to load data ... */
    be->compile_query = gnc_gda_compile_query;
    be->free_query = gnc_gda_free_query;
    be->run_query = gnc_gda_run_query;

    be->counter = NULL;

    /* The gda backend will not be multi-user (for now)... */
    be->events_pending = NULL;
    be->process_events = NULL;

    be->sync = gnc_gda_sync_all;
    be->load_config = NULL;
    be->get_config = NULL;

    be->export = NULL;

    gnc_be->primary_book = NULL;

    if( !initialized ) {
        gda_init( "gnucash", "2.0", 0, NULL );
        gnc_gda_init_object_handlers();
		gnc_gda_register_standard_col_type_handlers();
        initialized = TRUE;
    }

    return be;
}

static void
gnc_gda_provider_free (QofBackendProvider *prov)
{
    prov->provider_name = NULL;
    prov->access_method = NULL;
    g_free (prov);
}

/*
 * Checks to see whether the file is an sqlite file or not
 *
 */
static gboolean
gnc_gda_check_sqlite_file(const gchar *path)
{
	FILE* f;
	gchar buf[50];

	// BAD if the path is null
	if( path == NULL ) {
		return FALSE;
	}

	if( g_str_has_suffix( path, ".db" ) ) {
		f = g_fopen( path, "r" );

		// OK if the file doesn't exist - new file
		if( f == NULL ) {
			return TRUE;
		}

		// OK if file has the correct header
		fread( buf, sizeof(buf), 1, f );
		fclose( f );
		if( g_str_has_prefix( buf, "SQLite format" ) ) {
			return TRUE;
		}
	} else {
		f = g_fopen( path, "r" );

		// BAD if the file exists - not ours
		if( f != NULL ) {
			fclose( f );
			return FALSE;
		}

		// OK - new file
		return TRUE;
	}

	// Otherwise, BAD
	return FALSE;
}

G_MODULE_EXPORT void
qof_backend_module_init(void)
{
    QofBackendProvider *prov;

    prov = g_new0 (QofBackendProvider, 1);
    prov->provider_name = "GnuCash LibGDA Backend";
    prov->access_method = "gda";
    prov->partial_book_supported = FALSE;
    prov->backend_new = gnc_gda_backend_new;
    prov->provider_free = gnc_gda_provider_free;
    prov->check_data_type = NULL;
    qof_backend_register_provider (prov);

    prov = g_new0 (QofBackendProvider, 1);
    prov->provider_name = "GnuCash LibGDA Backend";
    prov->access_method = "file";
    prov->partial_book_supported = FALSE;
    prov->backend_new = gnc_gda_backend_new;
    prov->provider_free = gnc_gda_provider_free;
    prov->check_data_type = gnc_gda_check_sqlite_file;
    qof_backend_register_provider (prov);
}

/* ========================== END OF FILE ===================== */