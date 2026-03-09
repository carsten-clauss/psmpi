/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/* info keys used for compressor handling */
#define MPIX_COMPRESSOR_INFO_KEY_STRING "compressor"
#define MPIX_COMPRESSOR_INFO_KEY_PLUGIN_STRING "compressor_plugin"
extern const char compressor_info_key[];
extern const char compressor_info_key_plugin[];

/* symbol name for plugin function called to register a compressor */
#define MPIX_COMPRESSOR_REGISTER_PLUGIN_FN_STRING "compressor_register"
extern const char compressor_register_plugin_fn[];

typedef struct MPIR_Compressor {
    char *name;
    MPIX_Compressor_req_init_function *req_init_fn;
    MPIX_Compressor_req_free_function *req_free_fn;
    MPIX_Compressor_conversion_function *deflate_fn;
    MPIX_Compressor_conversion_function *inflate_fn;
    MPIX_Compressor_deregister_function *deregister_fn;
    void *extra_state;
    struct MPIR_Compressor *next;       /* pointer to next compressor */
} MPIR_Compressor;

extern MPL_atomic_ptr_t MPIR_Compressor_head;

/* This function searches for a compressor that has already been registered by its name.
 * If a matching compressor is found, then it returns a pointer the corresponding compressor
 * struct. If the name is not found as already registered, NULL is returned instead. */
int MPIR_Compressor_lookup(const char *compressor_name, MPIR_Compressor ** compressor_pptr);

int MPIR_Compressor_lookup_list(char **compressor_name_list, int *length);
int MPIR_Compressor_lookup_nth(int n, char **compressor_name);
int MPIR_Compressor_lookup_num(int *num);

/* Attach general information about the registered compressors to a given info object */
int MPIR_Compressor_set_info(MPIR_Info *);

/* This function is to be used at the very end to release all registered compressors and
 * their allocated resources via their deregister function. */
int MPIR_Compressor_deregister_all(void);

/* Data serving as a compressor-specific section for receive requests */
struct MPIR_Compressor_request_recv {
    MPIR_Compressor *compressor;
    /* Additional compressor data required in communication requests */
    int partition;
    MPI_Count count;
    MPI_Datatype datatype;
    void *user_buf_ptr;
    void *compr_buf_ptr;
    void *extra_req_state;
};

/* Data serving as a compressor-specific section for partitioned requests */
struct MPIR_Compressor_request_part {
    MPIR_Compressor *compressor;
    /* Additional compressor data required in communication requests */
    void *compr_buffer;
    int compr_buf_free;
    MPI_Aint compr_part_size;
    void *extra_req_state;
};
