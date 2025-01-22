
#include "mpioimpl.h"
#include "adio_extern.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIX_Register_compressor = PMPIX_Register_compressor
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIX_Register_compressor MPIX_Register_compressor
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIX_Register_compressor as PMPIX_Register_compressor
/* end of weak pragmas */
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPIX_Register_compressor(const char *compressor_name,
                             MPIX_Compressor_req_init_function * compressor_req_init_fn,
                             MPIX_Compressor_req_free_function * compressor_req_free_fn,
                             MPIX_Compressor_conversion_function * compressor_deflate_fn,
                             MPIX_Compressor_conversion_function * compressor_inflate_fn,
                             MPIX_Compressor_deregister_function * compressor_deregister_fn,
                             MPI_Info info, void *extra_state)
    __attribute__ ((weak, alias("PMPIX_Register_compressor")));
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
  MPIX_Register_compressor - Register functions for user-defined payload
                             compression

Input Parameters:
+ compressor_name - name of the compressor to register (string)
. req_init_fn     - function invoked to initialize a payload compressor
                    for a partitioned request
. req_free_fn     - function invoked to release resources allocated by
                    the compressor for a request
. deflate_fn      - function invoked to deflate a payload buffer before
                    sending
. inflate_fn      - function invoked to inflate a payload buffer again
                    after receiving
. deregister_fn   - function invoked when all resources are to be released
                    at the end
- extra_state     - pointer to extra state that is passed to req_init_fn
                    and deregister_fn
.N fortran

  @*/

int MPIX_Register_compressor(ROMIO_CONST char *compressor_name,
                             MPIX_Compressor_req_init_function * compressor_req_init_fn,
                             MPIX_Compressor_req_free_function * compressor_req_free_fn,
                             MPIX_Compressor_conversion_function * compressor_deflate_fn,
                             MPIX_Compressor_conversion_function * compressor_inflate_fn,
                             MPIX_Compressor_deregister_function * compressor_deregister_fn,
                             MPI_Info info, void *extra_state)
{
    return MPIOI_Register_compressor(compressor_name,
                                     compressor_req_init_fn, compressor_req_free_fn,
                                     compressor_deflate_fn, compressor_inflate_fn,
                                     compressor_deregister_fn, info, extra_state);
}

#ifdef MPIO_BUILD_PROFILING
int MPIOI_Register_compressor(const char *compressor_name,
                              MPIX_Compressor_req_init_function * compressor_req_init_fn,
                              MPIX_Compressor_req_free_function * compressor_req_free_fn,
                              MPIX_Compressor_conversion_function * compressor_deflate_fn,
                              MPIX_Compressor_conversion_function * compressor_inflate_fn,
                              MPIX_Compressor_deregister_function * compressor_deregister_fn,
                              MPI_Info info, void *extra_state)
{
    int error_code;
    ADIOI_Compressor *adio_compressor;
    static char myname[] = "MPI_REGISTER_COMPRESSOR";

    ROMIO_THREAD_CS_ENTER();

    /* --BEGIN ERROR HANDLING-- */
    /* check compressor name (use strlen instead of strnlen because
     * strnlen is not portable) */
    if (compressor_name == NULL || strlen(compressor_name) < 1 ||
        strlen(compressor_name) > MPIX_MAX_COMPRESSOR_STRING) {
        error_code =
            MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_ARG,
                                 "**compressorname", 0);
        error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPIR_MPIOInit(&error_code);
    if (error_code != MPI_SUCCESS)
        goto fn_exit;

    /* --BEGIN ERROR HANDLING-- */
    /* check compressor isn't already registered */
    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next) {
        if (!strncmp(compressor_name, adio_compressor->name, MPIX_MAX_COMPRESSOR_STRING)) {
            error_code = MPIO_Err_create_code(MPI_SUCCESS,
                                              MPIR_ERR_RECOVERABLE,
                                              myname, __LINE__,
                                              MPIX_ERR_DUP_COMPRESSOR,
                                              "**compressorused", "**compressorused %s",
                                              compressor_name);
            error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
            goto fn_exit;
        }
    }

    /* Check NULL function pointers on both sides (only on one side is okay) */
    if ((compressor_deflate_fn == NULL) && (compressor_inflate_fn == NULL)) {
        error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                          myname, __LINE__,
                                          MPIX_ERR_INVALID_COMPRESSOR, "**compressorinvalid", 0);

        error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    adio_compressor = ADIOI_Malloc(sizeof(ADIOI_Compressor));
    adio_compressor->name = ADIOI_Strdup(compressor_name);
    adio_compressor->extra_state = extra_state;
    adio_compressor->req_init_fn = compressor_req_init_fn;
    adio_compressor->req_free_fn = compressor_req_free_fn;
    adio_compressor->deflate_fn = compressor_deflate_fn;
    adio_compressor->inflate_fn = compressor_inflate_fn;
    adio_compressor->deregister_fn = compressor_deregister_fn;
    adio_compressor->next = ADIOI_Compressor_head;

    ADIOI_Compressor_head = adio_compressor;

    error_code = MPI_SUCCESS;

  fn_exit:
    ROMIO_THREAD_CS_EXIT();

    return error_code;
}
#endif

int MPIOI_Lookup_compressor(const char *compressor_name,
                            MPIX_Compressor_req_init_function ** compressor_req_init_fn,
                            MPIX_Compressor_req_free_function ** compressor_req_free_fn,
                            MPIX_Compressor_conversion_function ** compressor_deflate_fn,
                            MPIX_Compressor_conversion_function ** compressor_inflate_fn,
                            MPIX_Compressor_deregister_function ** compressor_deregister_fn,
                            void **extra_state)
{
    int found = 0;
    ADIOI_Compressor *adio_compressor;

    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next) {
        if (!strncmp(compressor_name, adio_compressor->name, MPIX_MAX_COMPRESSOR_STRING)) {
            if (compressor_req_init_fn)
                *compressor_req_init_fn = adio_compressor->req_init_fn;
            if (compressor_req_free_fn)
                *compressor_req_free_fn = adio_compressor->req_free_fn;
            if (compressor_deflate_fn)
                *compressor_deflate_fn = adio_compressor->deflate_fn;
            if (compressor_inflate_fn)
                *compressor_inflate_fn = adio_compressor->inflate_fn;
            if (compressor_deregister_fn)
                *compressor_deregister_fn = adio_compressor->deregister_fn;
            if (extra_state)
                *extra_state = adio_compressor->extra_state;
            found = 1;
            break;
        }
    }

    return found;
}

int MPIOI_Lookup_compressor_list(char **compressor_name_list)
{
    static char *recent_name_list = NULL;
    int length = 0;

    if (recent_name_list) {
        ADIOI_Free(recent_name_list);
    }

    if (!compressor_name_list) {
        /* This path serves for freeing recent_name_list at the end. */
        goto fn_exit;
    }

    ADIOI_Compressor *adio_compressor;
    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next) {
        length += strnlen(adio_compressor->name, MPIX_MAX_COMPRESSOR_STRING) + 1;
    }
    if (length) {
        *compressor_name_list = ADIOI_Malloc(length);
        (*compressor_name_list)[0] = '\0';
    } else {
        *compressor_name_list = NULL;
        goto fn_exit;
    }
    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next) {
        strcat(*compressor_name_list, adio_compressor->name);
        if (adio_compressor->next) {
            strcat(*compressor_name_list, ",");
        }
    }

    recent_name_list = *compressor_name_list;

  fn_exit:
    return length;
}

int MPIOI_Lookup_compressor_nth(int n, char **compressor_name)
{
    int i = 0;
    ADIOI_Compressor *adio_compressor;

    if (compressor_name)
        *compressor_name = NULL;

    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next, i++) {

        if (compressor_name && (i == n)) {
            *compressor_name = adio_compressor->name;
            break;
        }
    }

    return i;
}
