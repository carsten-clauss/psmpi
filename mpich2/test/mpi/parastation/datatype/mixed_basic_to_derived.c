/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mixed_datatype_test.h"

int main(int argc, char **argv)
{
    int errs = 0;

    MTest_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    /* Init global vars */
    init_datatype(0, nprocs);

#if defined(ALLGATHER)
    sbuffer = malloc(factor * count * blocklength * basic_size);
    rbuffer = malloc(factor * count * stride * basic_size * nprocs);

    for (int i = 0; i < factor * count * blocklength; i++) {
        sbuffer[i] = (type_basic_t) i + rank;
    }

    test_handle(MPI_Allgather
                (sbuffer, factor * count * blocklength, type_basic, rbuffer, factor,
                 type_derived, MPI_COMM_WORLD));

    for (int p = 0; p < nprocs; p++) {
        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            int pos = i + factor * count * stride * p;
            test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(ALLGATHERV)
    sbuffer = malloc(factor * count * blocklength * basic_size);
    rbuffer = malloc((factor * count * stride * nprocs) * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        rcounts[i] = factor;
        rdispls[i] = factor * i + i;
    }

    for (int i = 0; i < factor * count * blocklength; i++) {
        sbuffer[i] = (type_basic_t) i + rank;
    }

    test_handle(MPI_Allgatherv
                (sbuffer, factor * count * blocklength, type_basic, rbuffer, rcounts, rdispls,
                 type_derived, MPI_COMM_WORLD));

    for (int p = 0; p < nprocs; p++) {
        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            int pos = (i + factor * count * stride * p) + (p * count * stride);
            test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(ALLTOALL)
    sbuffer = malloc(factor * count * blocklength * basic_size * nprocs);
    rbuffer = malloc(factor * count * stride * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = i * (factor * count * blocklength) + j;
            sbuffer[pos] = (type_basic_t) j + rank;
        }
    }

    test_handle(MPI_Alltoall
                (sbuffer, factor * count * blocklength, type_basic, rbuffer, factor,
                 type_derived, MPI_COMM_WORLD));

    for (int p = 0; p < nprocs; p++) {
        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            int pos = i + factor * count * stride * p;
            test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(ALLTOALLV)
    sbuffer = malloc(factor * count * blocklength * basic_size * nprocs);
    rbuffer = malloc((factor * count * stride * nprocs) * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        scounts[i] = factor * count * blocklength;
        sdispls[i] = (factor * count * blocklength) * i;
        rcounts[i] = factor;
        rdispls[i] = factor * i + i;
    }

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = i * (factor * count * blocklength) + j;
            sbuffer[pos] = (type_basic_t) j + rank;
        }
    }

    test_handle(MPI_Alltoallv
                (sbuffer, scounts, sdispls, type_basic, rbuffer, rcounts, rdispls,
                 type_derived, MPI_COMM_WORLD));

    for (int p = 0; p < nprocs; p++) {
        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            int pos = (i + factor * count * stride * p) + (p * count * stride);
            test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(BCAST)
    if (rank == root) {

        sbuffer = malloc(factor * count * blocklength * basic_size);

        for (int i = 0; i < factor * count * blocklength; i++) {
            sbuffer[i] = (type_basic_t) i;
        }

        MPI_Bcast(sbuffer, factor * count * blocklength, type_basic, root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc(factor * count * stride * basic_size);

        test_handle(MPI_Bcast(rbuffer, factor, type_derived, root, MPI_COMM_WORLD));

        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            test_assert(rbuffer[i] == 1.0 * k, "mismatch at position %d", i);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(GATHER)
    if (rank == root) {

        sbuffer = malloc(factor * count * blocklength * basic_size);
        rbuffer = malloc(factor * count * stride * basic_size * nprocs);

        for (int i = 0; i < factor * count * blocklength; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        test_handle(MPI_Gather
                    (sbuffer, factor * count * blocklength, type_basic, rbuffer, factor,
                     type_derived, root, MPI_COMM_WORLD));

        for (int p = 0; p < nprocs; p++) {
            for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
                int pos = i + factor * count * stride * p;
                test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
                if (j + 1 == blocklength) {
                    i += (stride - blocklength);
                    j = 0;
                } else {
                    j++;
                }
            }
        }

    } else {

        sbuffer = malloc(factor * count * blocklength * basic_size);

        for (int i = 0; i < factor * count * blocklength; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        MPI_Gather(sbuffer, factor * count * blocklength, type_basic, NULL, 0, MPI_DATATYPE_NULL,
                   root, MPI_COMM_WORLD);
    }
#elif defined(GATHERV)
    if (rank == root) {

        sbuffer = malloc(factor * count * blocklength * basic_size);
        rbuffer = malloc((factor * count * stride * nprocs) * basic_size * nprocs);

        for (int i = 0; i < nprocs; i++) {
            rcounts[i] = factor;
            rdispls[i] = factor * i + i;
        }

        for (int i = 0; i < factor * count * blocklength; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        test_handle(MPI_Gatherv
                    (sbuffer, factor * count * blocklength, type_basic, rbuffer, rcounts,
                     rdispls, type_derived, root, MPI_COMM_WORLD));

        for (int p = 0; p < nprocs; p++) {
            for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
                int pos = (i + factor * count * stride * p) + (p * count * stride);
                test_assert(rbuffer[pos] == 1.0 * k + p, "mismatch at position %d", pos);
                if (j + 1 == blocklength) {
                    i += (stride - blocklength);
                    j = 0;
                } else {
                    j++;
                }
            }
        }

    } else {

        sbuffer = malloc(factor * count * blocklength * basic_size);

        for (int i = 0; i < factor * count * blocklength; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        MPI_Gatherv(sbuffer, factor * count * blocklength, type_basic, NULL, NULL, NULL,
                    MPI_DATATYPE_NULL, root, MPI_COMM_WORLD);
    }
#elif defined(SCATTER)
    if (rank == root) {

        sbuffer = malloc(factor * count * blocklength * basic_size * nprocs);
        rbuffer = malloc(factor * count * stride * basic_size);

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * blocklength; j++) {
                int pos = i * (factor * count * blocklength) + j;
                sbuffer[pos] = (type_basic_t) j + i;
            }
        }

        MPI_Scatter(sbuffer, factor * count * blocklength, type_basic, rbuffer, factor,
                    type_derived, root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc(factor * count * stride * basic_size);

        test_handle(MPI_Scatter
                    (NULL, 0, MPI_DATATYPE_NULL, rbuffer, factor, type_derived, root,
                     MPI_COMM_WORLD));

        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            test_assert(rbuffer[i] == 1.0 * k + rank, "mismatch at position %d", i);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#elif defined(SCATTERV)
    if (rank == root) {

        sbuffer = malloc((factor * count * blocklength + 1) * basic_size * nprocs);
        rbuffer = malloc(factor * count * stride * basic_size);

        for (int i = 0; i < nprocs; i++) {
            scounts[i] = factor * count * blocklength;
            sdispls[i] = (factor * count * blocklength + 1) * i;
        }

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * blocklength + 1; j++) {
                int pos = i * (factor * count * blocklength + 1) + j;
                sbuffer[pos] = (type_basic_t) j + i;
            }
        }

        MPI_Scatterv(sbuffer, scounts, sdispls, type_basic, rbuffer, factor, type_derived,
                     root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc(factor * count * stride * basic_size);

        test_handle(MPI_Scatterv
                    (NULL, NULL, NULL, MPI_DATATYPE_NULL, rbuffer, factor, type_derived,
                     root, MPI_COMM_WORLD));

        for (int i = 0, j = 0, k = 0; i < factor * count * stride; i++, k++) {
            test_assert(rbuffer[i] == 1.0 * k + rank, "mismatch at position %d", i);
            if (j + 1 == blocklength) {
                i += (stride - blocklength);
                j = 0;
            } else {
                j++;
            }
        }
    }
#else
#error "No collop selected!"
#endif

    /* Clean up global vars */
    cleanup_datatype();

    MTest_Finalize(errs);
    return MTestReturnValue(errs);
}
