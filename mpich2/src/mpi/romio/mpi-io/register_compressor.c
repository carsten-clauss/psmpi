
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
                             MPIX_Compressor_function * compressor_init_fn,
                             MPIX_Compressor_function * compressor_deflate_fn,
                             MPIX_Compressor_function * compressor_inflate_fn, void *extra_state)
    __attribute__ ((weak, alias("PMPIX_Register_compressor")));
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
  MPIX_Register_compressor - Register functions for user-defined data
                         representations

Input Parameters:
+ compressor_name - name of the compressor to register (string)
. read_conversion_fn - function invoked to convert from file representation to
                 native representation (function)
. write_conversion_fn - function invoked to convert from native representation to
                  file representation (function)
. dtype_file_extent_fn - function invoked to get the exted of a datatype as represented
                  in the file (function)
- extra_state - pointer to extra state that is passed to each of the
                three functions
.N fortran

  @*/

int MPIX_Register_compressor(ROMIO_CONST char *compressor_name,
                             MPIX_Compressor_function * compressor_init_fn,
                             MPIX_Compressor_function * compressor_deflate_fn,
                             MPIX_Compressor_function * compressor_inflate_fn, void *extra_state)
{
    return MPIOI_Register_compressor(compressor_name, compressor_init_fn,
                                     compressor_deflate_fn, compressor_inflate_fn, extra_state);
}

#ifdef MPIO_BUILD_PROFILING
int MPIOI_Register_compressor(const char *compressor_name,
                              MPIX_Compressor_function * compressor_init_fn,
                              MPIX_Compressor_function * compressor_deflate_fn,
                              MPIX_Compressor_function * compressor_inflate_fn, void *extra_state)
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

    /* Check NULL function pointers */
    if ((compressor_init_fn == NULL) ||
        (compressor_deflate_fn == NULL) || (compressor_inflate_fn == NULL)) {
        error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                          myname, __LINE__,
                                          MPIX_ERR_INVALID_COMPRESSOR, "**compressorinvalid", 0);

        error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    adio_compressor = ADIOI_Malloc(sizeof(ADIOI_Compressor));
    adio_compressor->name = ADIOI_Strdup(compressor_name);
    adio_compressor->state = extra_state;
    adio_compressor->init_fn = compressor_init_fn;
    adio_compressor->deflate_fn = compressor_deflate_fn;
    adio_compressor->inflate_fn = compressor_inflate_fn;
    adio_compressor->next = ADIOI_Compressor_head;

    ADIOI_Compressor_head = adio_compressor;

    error_code = MPI_SUCCESS;

  fn_exit:
    ROMIO_THREAD_CS_EXIT();

    return error_code;
}
#endif

int MPIOI_Lookup_compressor(const char *compressor_name,
                            MPIX_Compressor_function ** compressor_init_fn,
                            MPIX_Compressor_function ** compressor_deflate_fn,
                            MPIX_Compressor_function ** compressor_inflate_fn, void **extra_state)
{
    int error_code;
    ADIOI_Compressor *adio_compressor;

    for (adio_compressor = ADIOI_Compressor_head; adio_compressor;
         adio_compressor = adio_compressor->next) {
        if (!strncmp(compressor_name, adio_compressor->name, MPIX_MAX_COMPRESSOR_STRING)) {
            *compressor_init_fn = adio_compressor->init_fn;
            *compressor_deflate_fn = adio_compressor->deflate_fn;
            *compressor_inflate_fn = adio_compressor->inflate_fn;
            *extra_state = adio_compressor->state;
            break;
        }
    }

  fn_exit:
    return error_code;
}
