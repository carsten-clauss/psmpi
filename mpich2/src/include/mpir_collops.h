/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/* info keys used for collops handling */
#define MPIX_COLLOPS_INFO_KEY_STRING "collops"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_STRING "collops_plugin"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_LIST_STRING "compressor_plugins"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_SEPARATOR ","
extern const char collops_info_key[];
extern const char collops_info_key_plugin[];
extern const char collops_info_key_plugin_list[];
extern const char collops_info_key_plugin_separator[];

/* symbol name for plugin function called to register a collops */
#define MPIX_COLLOPS_REGISTER_PLUGIN_FN_STRING "collops_register"
extern const char collops_register_plugin_fn[];

typedef struct MPIR_Collops {
    char *name;
    int collops_mask;
    MPIX_Collops_algorithm_function *algorithm_fn;
    MPIX_Collops_progress_function *progress_fn;
    MPIX_Collops_comm_init_function *comm_init_fn;
    MPIX_Collops_comm_free_function *comm_free_fn;
    MPIX_Collops_deregister_function *deregister_fn;
    void *extra_state;
    struct MPIR_Collops *next;  /* pointer to next collops */
} MPIR_Collops;

extern MPL_atomic_ptr_t MPIR_Collops_head;

/* This function searches for a collops that has already been registered by its name.
 * If a matching collops is found, the it returns a pointer the corresponding collops
 * struct. If the name is not found as already registered, NULL is returned instead. */
int MPIR_Collops_lookup(const char *collops_name, MPIR_Collops ** collops_pptr);

int MPIR_Collops_lookup_list(char **collops_name_list, int *length);
int MPIR_Collops_lookup_nth(int n, char **collops_name);
int MPIR_Collops_lookup_num(int *num);

/* Attach general information about the registered collopss to a given info object */
int MPIR_Collops_set_info(MPIR_Info *);

/* This function is to be used at the very end to release all registered collopss and
 * their allocated resources via their deregister function. */
int MPIR_Collops_deregister_all(void);

/* Check the info object attached to the communicator for collops-related info keys.
 * If any are found, they are evaluated, and any additional information is appended
 * to the communicator's `collops` sub-struct. */
int MPIR_Collops_check_info(MPIR_Comm * comm);
