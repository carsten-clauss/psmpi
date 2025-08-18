/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpi.h"
#include "mpitest.h"
#include "mpitestconf.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif

const char *test_function;

#define test_handle(function)   \
    test_function = #function ; \
    do {                        \
        function;               \
    } while (0);

#define test_assert(expression, string, ...)                            \
    if (!(expression)) {                                                \
        fprintf(stderr, "ERROR: %s " string " (%s:%d)\n",               \
                test_function,                                          \
                __VA_ARGS__, __FILE__, __LINE__);                       \
        errs++;                                                         \
    }

#define test_assert_key(key, flag)			\
    test_assert(flag, "did not find key \"%s\"", key);

#define test_assert_count(count1, count2)                               \
    test_assert((count1 == count2), "returned unexpected number of"     \
                " array entries: " #count1 " = %d vs. " #count2 " ="    \
                " %d", count1, count2);

int compressor_deflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    /* just copy the buffer (effectively a no-op) */
    memcpy(compr_buf, buf, *size);
    return MPI_SUCCESS;
}

int compressor_inflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    /* just copy the buffer (effectively a no-op) */
    memcpy(buf, compr_buf, *size);
    return MPI_SUCCESS;
}

/* This test program checks the envisioned functionality of the proposed
 * extension of `MPI_Info_create_env()` to return also the names of the
 * already registered MPI payload compressors.
 *
 * For this, two compressors are registered via `MPIX_Register_compressor()`
 * and then the call of `MPI_Info_create_env()` is issued.
 * According to the proposed behavior, it is then checked (1) whether there is
 * an array-type entry with two elements for the key "compressor" (each of which
 * corresponding to one of the registered compressor names) and (2) if there is
 * also an entry for the key "compressor_list" that features a comma-separated
 * string representing a list of the compressor names, with an order that matches
 * the one of the array-type entry.
 */

int main(int argc, char *argv[])
{
    int errs = 0;
    int flag = 0;
    int nkeys = 0;
    int buflen = 0;
    MPI_Info info = MPI_INFO_NULL;
    char value[MPI_MAX_INFO_VAL];
    char *key = NULL;

    MTest_Init(&argc, &argv);

#define COMPRESSOR_NAME_A "compressor_foo"
#define COMPRESSOR_NAME_B "compressor_bar"

    MPIX_Register_compressor(COMPRESSOR_NAME_A,
                             MPIX_COMPRESSOR_REQ_INIT_FN_NULL,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate, compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    MPIX_Register_compressor(COMPRESSOR_NAME_B,
                             MPIX_COMPRESSOR_REQ_INIT_FN_NULL,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate, compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    test_handle(MPI_Info_create_env(0, NULL, &info));
    test_assert(info != MPI_INFO_NULL, "returned MPI_INFO_NULL (%p).", &info);

    test_handle(MPI_Info_get_nkeys(info, &nkeys));
    test_assert(nkeys == 2, "found wrong number of entries (%d vs. 2).", nkeys);

    key = "compressor";
    buflen = MPI_MAX_INFO_VAL;
    test_handle(MPI_Info_get_string(info, key, &buflen, value, &flag));
    test_assert_key(key, flag);
    test_assert(!strcmp(value, "mpix_info_array_type"),
                "returned value \"%s\" that is not of array type.", value);

    int count = 2;
    MPI_Info info_split[2];
    int count_expected = 2;
    MPI_Info_create(&info_split[0]);
    MPI_Info_create(&info_split[1]);
    test_handle(MPIX_Info_split_into_array(&count, info_split, info));
    test_assert_count(count, count_expected);

    key = "compressor";
    buflen = MPI_MAX_INFO_VAL;
    test_handle(MPI_Info_get_string(info_split[0], key, &buflen, value, &flag));
    test_assert_key(key, flag);
    test_assert(!strcmp(value, COMPRESSOR_NAME_A) ||
                !strcmp(value, COMPRESSOR_NAME_B),
                "returned value \"%s\" that is not a registered compressor", value);

    int fifo_order = (!strcmp(value, COMPRESSOR_NAME_A)) ? 1 : 0;

    key = "compressor";
    buflen = MPI_MAX_INFO_VAL;
    test_handle(MPI_Info_get_string(info_split[1], key, &buflen, value, &flag));
    test_assert_key(key, flag);
    test_assert(!strcmp(value, COMPRESSOR_NAME_A) ||
                !strcmp(value, COMPRESSOR_NAME_B),
                "returned value \"%s\" that is not a registered compressor", value);

    if (fifo_order) {
        test_assert(!strcmp(value, COMPRESSOR_NAME_B),
                    "returned value \"%s\" mismatches expected value \"%s\"", value,
                    COMPRESSOR_NAME_B);
    } else {
        test_assert(!strcmp(value, COMPRESSOR_NAME_A),
                    "returned value \"%s\" mismatches expected value \"%s\"", value,
                    COMPRESSOR_NAME_A);
    }

    MPI_Info_free(&info_split[0]);
    MPI_Info_free(&info_split[1]);

    key = "compressor_list";
    buflen = MPI_MAX_INFO_VAL;
    test_handle(MPI_Info_get_string(info, key, &buflen, value, &flag));
    test_assert_key(key, flag);
    test_assert(!strcmp(value, COMPRESSOR_NAME_A "," COMPRESSOR_NAME_B) ||
                !strcmp(value, COMPRESSOR_NAME_B "," COMPRESSOR_NAME_A),
                "returned unexpected compressor list (\"%s\").", value);
    test_assert((fifo_order && !strcmp(value, COMPRESSOR_NAME_A "," COMPRESSOR_NAME_B)) ||
                (!fifo_order && !strcmp(value, COMPRESSOR_NAME_B "," COMPRESSOR_NAME_A)),
                "returned unexpected order in compressor list (\"%s\").", value);

    MPI_Info_free(&info);

    MTest_Finalize(errs);
    return MTestReturnValue(errs);
}
