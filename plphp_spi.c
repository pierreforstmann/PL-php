/**********************************************************************
 * plphp_spi.c - SPI-related functions for PL/php.
 *
 * This software is copyright (c) Command Prompt Inc.
 *
 * The author hereby grants permission to use, copy, modify,
 * distribute, and license this software and its documentation for any
 * purpose, provided that existing copyright notices are retained in
 * all copies and that this notice is included verbatim in any
 * distributions. No written agreement, license, or royalty fee is
 * required for any of the authorized uses.  Modifications to this
 * software may be copyrighted by their author and need not follow the
 * licensing terms described here, provided that the new terms are
 * clearly indicated on the first page of each file where they apply.
 *
 * IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY PARTY
 * FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY
 * DERIVATIVES THEREOF, EVEN IF THE AUTHOR HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 * NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS,
 * AND THE AUTHOR AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * IDENTIFICATION
 *		$Id$
 *********************************************************************
 */

#include "postgres.h"
#include "plphp_spi.h"
#include "plphp_io.h"

/* PHP stuff */
#include "php.h"

/* PostgreSQL stuff */
#include "access/xact.h"
#include "miscadmin.h"

#undef DEBUG_PLPHP_MEMORY

#ifdef DEBUG_PLPHP_MEMORY
#define REPORT_PHP_MEMUSAGE(where) \
	elog(NOTICE, "PHP mem usage: «%s»: %u", where, AG(allocated_memory));
#else
#define REPORT_PHP_MEMUSAGE(a) 
#endif

/* resource type Id for SPIresult */
int SPIres_rtype;

/* SPI function table */
//zend_function_entry spi_functions[] =
//{
//	ZEND_FE(spi_exec, NULL)
//	ZEND_FE(spi_fetch_row, NULL)
//	ZEND_FE(spi_processed, NULL)
//	ZEND_FE(spi_status, NULL)
//	ZEND_FE(spi_rewind, NULL)
//	ZEND_FE(pg_raise, NULL)
//	ZEND_FE(return_next, NULL)
//	{NULL, NULL, NULL}
//};

// begin - php8

ZEND_BEGIN_ARG_INFO_EX(arginfo_spi_exec, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, query, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_spi_fetch_row, 0, 0, 1)
ZEND_ARG_TYPE_INFO(0, result, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_spi_processed, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_spi_status, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_spi_rewind, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, cursor, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_pg_raise, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, message, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_return_next, 0, 0, 1)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()



// Define your functions

//const zend_function_entry plphp_functions[] = {
//    PHP_FE(spi_exec, arginfo_spi_exec)
//    PHP_FE(spi_fetch_row, arginfo_spi_fetch_row)
//    PHP_FE(spi_processed, arginfo_spi_processed)
//    PHP_FE(spi_status, arginfo_spi_status)
//    PHP_FE(spi_rewind, arginfo_spi_rewind)
//    PHP_FE(pg_raise, arginfo_pg_raise)
//    PHP_FE(return_next, arginfo_return_next)
//    PHP_FE_END
//};

const zend_function_entry plphp_functions[] = {
	ZEND_FE(spi_exec, arginfo_spi_exec)
	ZEND_FE(spi_fetch_row, arginfo_spi_fetch_row)
	ZEND_FE(spi_processed, arginfo_spi_processed)
	ZEND_FE(spi_status, arginfo_spi_status)
	ZEND_FE(spi_rewind, arginfo_spi_rewind)
	ZEND_FE(pg_raise, arginfo_pg_raise)
	ZEND_FE(return_next, arginfo_return_next)
	{NULL, NULL, NULL}
};

// end - php8

/* SRF support: */
FunctionCallInfo current_fcinfo = NULL;
TupleDesc current_tupledesc = NULL;
AttInMetadata *current_attinmeta = NULL;
MemoryContext current_memcxt = NULL;
Tuplestorestate *current_tuplestore = NULL;


/* A symbol table to save for return_next for the RETURNS TABLE case */
HashTable *saved_symbol_table;

static zval *get_table_arguments(AttInMetadata *attinmeta);

/*
 * spi_exec
 * 		PL/php equivalent to SPI_exec().
 *
 * This function creates and return a PHP resource which describes the result
 * of a user-specified query.  If the query returns tuples, it's possible to
 * retrieve them by using spi_fetch_row.
 *
 * Receives one or two arguments.  The mandatory first argument is the query
 * text.  The optional second argument is the tuple limit.
 *
 * Note that just like PL/Perl, we start a subtransaction before invoking the
 * SPI call, and automatically roll it back if the call fails.
 */
ZEND_FUNCTION(spi_exec)
{
	char	   *query;
	// PHP 8: query_len must be size_t and not int
	size_t		query_len;
	char		*query_copy;
	long		status;
	long		limit;
	php_SPIresult *SPIres;
	int			spi_id = 0;
	MemoryContext oldcontext = CurrentMemoryContext;
	ResourceOwner oldowner = CurrentResourceOwner;

	REPORT_PHP_MEMUSAGE("spi_exec called");

	if ((ZEND_NUM_ARGS() > 2) || (ZEND_NUM_ARGS() < 1))
		WRONG_PARAM_COUNT;

	/* Parse arguments */
	if (ZEND_NUM_ARGS() == 2)
	{
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl",
								  &query, &query_len, &limit) == FAILURE)
		{
			zend_error(E_WARNING, "Can not parse parameters in %s",
					   get_active_function_name(TSRMLS_C));
			RETURN_FALSE;
		}
	}
	else if (ZEND_NUM_ARGS() == 1)
	{
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
								  &query, &query_len) == FAILURE)
		{
			zend_error(E_WARNING, "Can not parse parameters in %s",
					   get_active_function_name(TSRMLS_C));
			RETURN_FALSE;
		}
		limit = 0;
	}
	else
	{
		zend_error(E_WARNING, "Incorrect number of parameters to %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}

    
    /* Copy into palloc'd memory before any PHP activity can free the zend_string */
    query_copy = palloc(query_len + 1);     /* ADD THIS */
    memcpy(query_copy, query, query_len + 1);  /* ADD THIS */

	BeginInternalSubTransaction(NULL);
	MemoryContextSwitchTo(oldcontext);

	/* Call SPI */
	PG_TRY();
	{
		status = SPI_exec(query_copy, limit);  /* Use query_copy */

		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;

		/*
		 * AtEOSubXact_SPI() should not have popped any SPI context, but just
		 * in case it did, make sure we remain connected.
		 */
		// PG 15
		//SPI_restore_connection();
	}
	PG_CATCH();
	{
		ErrorData	*edata;

		/* Save error info */
		MemoryContextSwitchTo(oldcontext);
		edata = CopyErrorData();
		FlushErrorState();

		/* Abort the inner trasaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;

		/*
		 * If AtEOSubXact_SPI() popped any SPI context of the subxact, it will
		 * have left us in a disconnected state.  We need this hack to return
		 * to connected state.
		 */
		// PG 15
		//SPI_restore_connection();

		/* bail PHP out */
		zend_error(E_ERROR, "%s", strdup(edata->message));

		/* Can't get here, but keep compiler quiet */
		return;
	}
	PG_END_TRY();

	/* This malloc'ed chunk is freed in php_SPIresult_destroy */
	// PHP 8 : use emalloc instead of malloc
	SPIres = (php_SPIresult *) emalloc(sizeof(php_SPIresult));
	if (!SPIres)
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));

	/* Prepare the return resource */
	SPIres->SPI_processed = SPI_processed;
	if (status == SPI_OK_SELECT)
		SPIres->SPI_tuptable = SPI_tuptable;
	else
		SPIres->SPI_tuptable = NULL;
	SPIres->current_row = 0;
	SPIres->status = status;

	REPORT_PHP_MEMUSAGE("spi_exec: creating resource");

	/* Register the resource to PHP so it will be able to free it */
	// PHP8
	//spi_id = ZEND_REGISTER_RESOURCE(return_value, (void *) SPIres,
	//				 				SPIres_rtype);
	
	REPORT_PHP_MEMUSAGE("spi_exec: returning");

	RETURN_RES(zend_register_resource(SPIres, spi_id));

}

/*
 * spi_fetch_row
 * 		Grab a row from a SPI result (from spi_exec).
 *
 * This function receives a resource Id and returns a PHP hash representing the
 * next tuple in the result, or false if no tuples remain.
 *
 * XXX Apparently this is leaking memory.  How do we tell PHP to free the tuple
 * once the user is done with it?
 */
ZEND_FUNCTION(spi_fetch_row)
{
	zval	   *row = NULL;
	// PHP 8
	// zval	  **z_spi = NULL;
	zval	  *z_spi = NULL;
	php_SPIresult	*SPIres;

	REPORT_PHP_MEMUSAGE("spi_fetch_row: called");

	if (ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;

	// PHP 8
	// if (zend_get_parameters_ex(1, &z_spi) == FAILURE)
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "r", &z_spi) == FAILURE)
	{
		zend_error(E_WARNING, "Can not parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}
	if (z_spi == NULL)
	{
		zend_error(E_WARNING, "Could not get SPI resource in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}

	// PHP 8
	// ZEND_FETCH_RESOURCE(SPIres, php_SPIresult *, z_spi, -1, "SPI result",
	//					SPIres_rtype);
	SPIres = (php_SPIresult *) zend_fetch_resource(Z_RES_P(z_spi),
                                                "SPI result",
                                                SPIres_rtype);
	if (SPIres == NULL)
    RETURN_FALSE;

	if (SPIres->status != SPI_OK_SELECT)
	{
		zend_error(E_WARNING, "SPI status is not good");
		RETURN_FALSE;
	}

	if (SPIres->current_row < SPIres->SPI_processed)
	{
		row = plphp_zval_from_tuple(SPIres->SPI_tuptable->vals[SPIres->current_row],
			  						SPIres->SPI_tuptable->tupdesc);
		SPIres->current_row++;

		// PHP 8
		//*return_value = *row;

		//zval_copy_ctor(return_value);
		//zval_dtor(row);
		//FREE_ZVAL(row);

		 ZVAL_COPY_VALUE(return_value, row);
 	    efree(row);
	}
	else
		RETURN_FALSE;

	REPORT_PHP_MEMUSAGE("spi_fetch_row: finish");
}

/*
 * spi_processed
 * 		Return the number of tuples returned in a spi_exec call.
 */
ZEND_FUNCTION(spi_processed)
{
	// PHP 8
    //	zval	   **z_spi = NULL;
	zval	   *z_spi = NULL;
	php_SPIresult	*SPIres;

	REPORT_PHP_MEMUSAGE("spi_processed: start");

	if (ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;

	// PHP8
	//if (zend_get_parameters_ex(1, &z_spi) == FAILURE)
   if (zend_parse_parameters(ZEND_NUM_ARGS(), "r", &z_spi) == FAILURE)
	{
		zend_error(E_WARNING, "Cannot parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}
	if (z_spi == NULL)
	{
		zend_error(E_WARNING, "Could not get SPI resource in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}

	// PHP 8
	//ZEND_FETCH_RESOURCE(SPIres, php_SPIresult *, z_spi, -1, "SPI result",
	//					SPIres_rtype);
	//
	SPIres = (php_SPIresult *) zend_fetch_resource(Z_RES_P(z_spi),
                                                "SPI result",
                                                SPIres_rtype);
	if (SPIres == NULL)
    	RETURN_FALSE;

	REPORT_PHP_MEMUSAGE("spi_processed: finish");

	RETURN_LONG(SPIres->SPI_processed);
}

/*
 * spi_status
 * 		Return the status returned by a previous spi_exec call, as a string.
 */
ZEND_FUNCTION(spi_status)
{
	zval	   *z_spi = NULL;
	// zval	   **z_spi = NULL;
	php_SPIresult	*SPIres;

	REPORT_PHP_MEMUSAGE("spi_status: start");

	if (ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;

	// PHP 8
	// if (zend_get_parameters_ex(1, &z_spi) == FAILURE)
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "r", &z_spi) == FAILURE)
	{
		zend_error(E_WARNING, "Cannot parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}
	if (z_spi == NULL)
	{
		zend_error(E_WARNING, "Could not get SPI resource in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}
	// PHP 8
	//ZEND_FETCH_RESOURCE(SPIres, php_SPIresult *, z_spi, -1, "SPI result",
	//					SPIres_rtype);
	SPIres = (php_SPIresult *) zend_fetch_resource(Z_RES_P(z_spi),
                                                "SPI result",
                                                SPIres_rtype);
	if (SPIres == NULL)
        RETURN_FALSE;

	REPORT_PHP_MEMUSAGE("spi_status: finish");

	/*
	 * XXX The cast is wrong, but we use it to prevent a compiler warning.
	 * Note that the second parameter to RETURN_STRING is "duplicate", so
	 * we are returning a copy of the string anyway.
	 */
	// PHP 8
	// RETURN_STRING((char *) SPI_result_code_string(SPIres->status), true);
	RETURN_STRING(SPI_result_code_string(SPIres->status));
}

/*
 * spi_rewind
 * 		Resets the internal counter for spi_fetch_row, so the next
 * 		spi_fetch_row call will start fetching from the beginning.
 */
ZEND_FUNCTION(spi_rewind)
{
	// PHP 8
	// zval	   **z_spi = NULL;
	zval *z_spi = NULL;
	php_SPIresult	*SPIres;

	if (ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;

	// PHP 8
	//if (zend_get_parameters_ex(1, &z_spi) == FAILURE)
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "r", &z_spi) == FAILURE)
	{
		zend_error(E_WARNING, "Cannot parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}
	if (z_spi == NULL)
	{
		zend_error(E_WARNING, "Could not get SPI resource in %s",
				   get_active_function_name(TSRMLS_C));
		RETURN_FALSE;
	}

	// PHP 8
	//ZEND_FETCH_RESOURCE(SPIres, php_SPIresult *, z_spi, -1, "SPI result",
	//					SPIres_rtype);
	SPIres = (php_SPIresult *) zend_fetch_resource(Z_RES_P(z_spi),
                                                "SPI result",
                                                SPIres_rtype);
	if (SPIres == NULL)
    	RETURN_FALSE;

	SPIres->current_row = 0;

	RETURN_NULL();
}
/*
 * pg_raise
 *      User-callable function for sending messages to the Postgres log.
 */
ZEND_FUNCTION(pg_raise)
{
	char       *level = NULL,
			   *message = NULL,
			   *message_copy;
	int         level_len,
				elevel = 0;
	size_t		message_len;

	if (ZEND_NUM_ARGS() != 2)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("wrong number of arguments to %s", "pg_raise")));
	}

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss",
							  &level, &level_len,
							  &message, &message_len) == FAILURE)
	{
		zend_error(E_WARNING, "cannot parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
	}

     /* Copy into palloc'd memory before any PHP activity can free the zend_string */
    message_copy = palloc(message_len + 1);     /* ADD THIS */
    memcpy(message_copy, message, message_len + 1);  /* ADD THIS */


	if (strcasecmp(level, "ERROR") == 0)
		elevel = E_ERROR;
	else if (strcasecmp(level, "WARNING") == 0)
		elevel = E_WARNING;
	else if (strcasecmp(level, "NOTICE") == 0)
		elevel = E_NOTICE;
	else
		zend_error(E_ERROR, "incorrect log level");

	zend_error(elevel, "%s", message_copy);
}

/*
 * return_next
 * 		Add a tuple to the current tuplestore
 */
ZEND_FUNCTION(return_next)
{
	MemoryContext	oldcxt;
	zval	   *param;
	HeapTuple	tup;
	ReturnSetInfo *rsi;
	
	/*
	 * Disallow use of return_next inside non-SRF functions
	 */
	if (current_fcinfo == NULL || current_fcinfo->flinfo == NULL || 
		!current_fcinfo->flinfo->fn_retset)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use return_next in functions not declared to "
						"return a set")));

	rsi = (ReturnSetInfo *) current_fcinfo->resultinfo;

	Assert(current_tupledesc != NULL);
	Assert(rsi != NULL);
	
	if (ZEND_NUM_ARGS() > 1)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("wrong number of arguments to %s", "return_next")));

	if (ZEND_NUM_ARGS() == 0)
	{
		/* 
		 * Called from the function declared with RETURNS TABLE 
	     */
		param = get_table_arguments(current_attinmeta);
	}
	else if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z",
							  &param) == FAILURE)
	{
		zend_error(E_WARNING, "cannot parse parameters in %s",
				   get_active_function_name(TSRMLS_C));
	}

	/* Use the per-query context so that the tuplestore survives */
	oldcxt = MemoryContextSwitchTo(rsi->econtext->ecxt_per_query_memory);

	/* Form the tuple */
	tup = plphp_srf_htup_from_zval(param, current_attinmeta, current_memcxt);

	/* First call?  Create the tuplestore. */
	if (!current_tuplestore)
		current_tuplestore = tuplestore_begin_heap(true, false, work_mem);

	/* Save the tuple and clean up */
	tuplestore_puttuple(current_tuplestore, tup);
	heap_freetuple(tup);

	MemoryContextSwitchTo(oldcxt);
}

/*
 * php_SPIresult_destroy
 * 		Free the resources allocated by a spi_exec call.
 *
 * This is automatically called when the resource goes out of scope
 * or is overwritten by another resource.
 */
void
// php_SPIresult_destroy(zend_rsrc_list_entry *rsrc TSRMLS_DC)
 php_SPIresult_destroy(zend_resource *rsrc)
{
	php_SPIresult *res = (php_SPIresult *) rsrc->ptr;

	 if (res->SPI_tuptable != NULL)
    {
        /*
         * Only free the tuptable if we have an active SPI connection.
         * The destructor may be called after SPI_finish(), e.g. during
         * PHP exception unwinding.
         */
        if (SPI_connect() == SPI_OK_CONNECT)
        {
            SPI_freetuptable(res->SPI_tuptable);
            SPI_finish();
        }
        /*
         * If we can't reconnect, just leak the tuptable memory.
         * It will be reclaimed when the memory context is destroyed.
         */
        res->SPI_tuptable = NULL;
    }

	// PHP 8: use efree
	efree(res);
}

/* Return an array of TABLE argument values for return_next */
static
zval *get_table_arguments(AttInMetadata *attinmeta)
{
	zval   *retval = emalloc(sizeof(zval));
	int		i;
	// PHP 8
	zval *element;
	zend_string *zkey;
	
	// MAKE_STD_ZVAL(retval);
	ZVAL_UNDEF(retval);
	array_init(retval);

	Assert(attinmeta->tupdesc);
	Assert(saved_symbol_table != NULL);
	/* Extract OUT argument names */
	for (i = 0; i < attinmeta->tupdesc->natts; i++)
	{
		char 	*attname;

		// PHP 8
		// Assert(!attinmeta->tupdesc->attrs[i]->attisdropped);
		Assert(!TupleDescAttr(attinmeta->tupdesc, i)->attisdropped);

		// attname = NameStr(attinmeta->tupdesc->attrs[i]->attname);
		attname = NameStr(TupleDescAttr(attinmeta->tupdesc, i)->attname);

		// PHP 8
		// if (zend_hash_find(saved_symbol_table, 
		//				   attname, strlen(attname) + 1,
		//				   (void **)&val) == SUCCESS)

		zkey = zend_string_init(attname, strlen(attname), 0);
		element = zend_hash_find(saved_symbol_table, zkey);
		zend_string_release(zkey);

		if (element != NULL)
			add_next_index_zval(retval, element);
		else
			// add_next_index_unset(retval);
			add_next_index_null(retval);
	} 
	return retval;
}


/*
 * vim:ts=4:sw=4:cino=(0
 */
