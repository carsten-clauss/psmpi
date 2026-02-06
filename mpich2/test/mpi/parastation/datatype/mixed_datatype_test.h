/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpi.h"
#include "mpitest.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define TYPE_BASIC double
#define TYPE_BASIC_MPI MPI_DOUBLE

const char *test_function;
int root = 0;                   /* root for collops */

/* data type properties */
int factor = 5;
int count = 3;
int blocklength = 2;
int stride = 3;
int basic_size;

MPI_Datatype type_basic = TYPE_BASIC_MPI;
typedef TYPE_BASIC type_basic_t;
MPI_Datatype type_derived, type_vector, type_indexed, type_contig;

/* send/ recv variables */
type_basic_t *sbuffer = NULL;
type_basic_t *rbuffer = NULL;
int *scounts;
int *sdispls;
int *rcounts;
int *rdispls;

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

static
void init_datatype(int align, int nprocs)
{

    MPI_Type_size(type_basic, &basic_size);

    /*
     * Create a vector data type with gaps:
     *
     *   #########   #########   #########     #########################
     *   # 0 # 1 # 2 # 3 # 4 # 5 # 6 # 7 # --> # 0 # 1 # 3 # 4 # 6 # 7 #
     *   #########   #########   #########     #########################
     */
    MPI_Type_vector(count, blocklength, stride, type_basic, &type_vector);

    /*
     * Ensure by increasing the extent that there is also a gap between
     * vector elements when they are arranged (`factor` times) in a row:
     */
    MPI_Aint lb, extent;
    MPI_Type_get_extent(type_vector, &lb, &extent);
    MPI_Type_create_resized(type_vector, 0, extent + (stride - blocklength) * basic_size,
                            &type_derived);
    MPI_Type_commit(&type_derived);

    /*
     * Now create a matching derived but contiguous data type that has an
     * offset as a gap of one basic element at the beginning and an adjusted
     * extent so that there is also a gap between elements of this type
     * when arranged (`nproc` times) in a row:
     */

    int idx_blocklengths = factor * count * blocklength;
    int idx_displacements = 1;
    MPI_Type_indexed(1, &idx_blocklengths, &idx_displacements, type_basic, &type_indexed);
    MPI_Type_get_extent(type_indexed, &lb, &extent);

    MPI_Type_create_resized(type_indexed, 0, extent + align * basic_size, &type_contig);
    MPI_Type_commit(&type_contig);

    scounts = (int *) malloc(sizeof(int) * nprocs);
    sdispls = (int *) malloc(sizeof(int) * nprocs);
    rcounts = (int *) malloc(sizeof(int) * nprocs);
    rdispls = (int *) malloc(sizeof(int) * nprocs);
}

static
void cleanup_datatype(void)
{

    free(scounts);
    free(sdispls);
    free(rcounts);
    free(rdispls);

    free(rbuffer);
    free(sbuffer);

    MPI_Type_free(&type_vector);
    MPI_Type_free(&type_derived);
    MPI_Type_free(&type_indexed);
    MPI_Type_free(&type_contig);
}
