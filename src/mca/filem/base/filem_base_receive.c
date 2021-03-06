/* -*- C -*-
 *
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2016-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2020      Cisco Systems, Inc.  All rights reserved
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/** @file:
 *
 */

/*
 * includes
 */
#include "prte_config.h"

#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "src/mca/mca.h"
#include "src/util/output.h"
#include "src/util/printf.h"

#include "src/dss/dss.h"
#include "constants.h"
#include "types.h"
#include "src/util/proc_info.h"
#include "src/mca/errmgr/errmgr.h"
#include "src/mca/rml/rml.h"
#include "src/mca/rml/rml_types.h"
#include "src/mca/state/state.h"
#include "src/util/name_fns.h"
#include "src/runtime/prte_globals.h"
#include "src/runtime/prte_quit.h"

#include "src/mca/filem/filem.h"
#include "src/mca/filem/base/base.h"

/*
 * Functions to process some FileM specific commands
 */
static void filem_base_process_get_proc_node_name_cmd(prte_process_name_t* sender,
                                                      prte_buffer_t* buffer);
static void filem_base_process_get_remote_path_cmd(prte_process_name_t* sender,
                                                   prte_buffer_t* buffer);

static bool recv_issued=false;

int prte_filem_base_comm_start(void)
{
    /* Only active in HNP and daemons */
    if( !PRTE_PROC_IS_MASTER && !PRTE_PROC_IS_DAEMON ) {
        return PRTE_SUCCESS;
    }
    if ( recv_issued ) {
        return PRTE_SUCCESS;
    }

    PRTE_OUTPUT_VERBOSE((5, prte_filem_base_framework.framework_output,
                         "%s filem:base: Receive: Start command recv",
                         PRTE_NAME_PRINT(PRTE_PROC_MY_NAME)));

    prte_rml.recv_buffer_nb(PRTE_NAME_WILDCARD,
                            PRTE_RML_TAG_FILEM_BASE,
                            PRTE_RML_PERSISTENT,
                            prte_filem_base_recv,
                            NULL);

    recv_issued = true;

    return PRTE_SUCCESS;
}


int prte_filem_base_comm_stop(void)
{
    /* Only active in HNP and daemons */
    if( !PRTE_PROC_IS_MASTER && !PRTE_PROC_IS_DAEMON ) {
        return PRTE_SUCCESS;
    }
    if ( recv_issued ) {
        return PRTE_SUCCESS;
    }

    PRTE_OUTPUT_VERBOSE((5, prte_filem_base_framework.framework_output,
                         "%s filem:base:receive stop comm",
                         PRTE_NAME_PRINT(PRTE_PROC_MY_NAME)));

    prte_rml.recv_cancel(PRTE_NAME_WILDCARD, PRTE_RML_TAG_FILEM_BASE);
    recv_issued = false;

    return PRTE_SUCCESS;
}


/*
 * handle message from proxies
 * NOTE: The incoming buffer "buffer" is PRTE_RELEASED by the calling program.
 * DO NOT RELEASE THIS BUFFER IN THIS CODE
 */
void prte_filem_base_recv(int status, prte_process_name_t* sender,
                        prte_buffer_t* buffer, prte_rml_tag_t tag,
                        void* cbdata)
{
    prte_filem_cmd_flag_t command;
    int32_t count;
    int rc;

    PRTE_OUTPUT_VERBOSE((5, prte_filem_base_framework.framework_output,
                         "%s filem:base: Receive a command message.",
                         PRTE_NAME_PRINT(PRTE_PROC_MY_NAME)));

    count = 1;
    if (PRTE_SUCCESS != (rc = prte_dss.unpack(buffer, &command, &count, PRTE_FILEM_CMD))) {
        PRTE_ERROR_LOG(rc);
        return;
    }

    switch (command) {
        case PRTE_FILEM_GET_PROC_NODE_NAME_CMD:
            PRTE_OUTPUT_VERBOSE((10, prte_filem_base_framework.framework_output,
                                 "%s filem:base: Command: Get Proc node name command",
                                 PRTE_NAME_PRINT(PRTE_PROC_MY_NAME)));

            filem_base_process_get_proc_node_name_cmd(sender, buffer);
            break;

        case PRTE_FILEM_GET_REMOTE_PATH_CMD:
            PRTE_OUTPUT_VERBOSE((10, prte_filem_base_framework.framework_output,
                                 "%s filem:base: Command: Get remote path command",
                                 PRTE_NAME_PRINT(PRTE_PROC_MY_NAME)));

            filem_base_process_get_remote_path_cmd(sender, buffer);
            break;

        default:
            PRTE_ERROR_LOG(PRTE_ERR_VALUE_OUT_OF_BOUNDS);
    }
}

static void filem_base_process_get_proc_node_name_cmd(prte_process_name_t* sender,
                                                      prte_buffer_t* buffer)
{
    prte_buffer_t *answer;
    int32_t count;
    prte_job_t *jdata = NULL;
    prte_proc_t *proc = NULL;
    prte_process_name_t name;
    int rc;

    /*
     * Unpack the data
     */
    count = 1;
    if (PRTE_SUCCESS != (rc = prte_dss.unpack(buffer, &name, &count, PRTE_NAME))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        return;
    }

    /*
     * Process the data
     */
    /* get the job data object for this proc */
    if (NULL == (jdata = prte_get_job_data_object(name.jobid))) {
        PRTE_ERROR_LOG(PRTE_ERR_NOT_FOUND);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        return;
    }
    /* get the proc object for it */
    proc = (prte_proc_t*)prte_pointer_array_get_item(jdata->procs, name.vpid);
    if (NULL == proc || NULL == proc->node) {
        PRTE_ERROR_LOG(PRTE_ERR_NOT_FOUND);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        return;
    }

    /*
     * Send back the answer
     */
    answer = PRTE_NEW(prte_buffer_t);
    if (PRTE_SUCCESS != (rc = prte_dss.pack(answer, &(proc->node->name), 1, PRTE_STRING))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        PRTE_RELEASE(answer);
        return;
    }

    if (0 > (rc = prte_rml.send_buffer_nb(sender, answer,
                                          PRTE_RML_TAG_FILEM_BASE_RESP,
                                          prte_rml_send_callback, NULL))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        PRTE_RELEASE(answer);
        return;
    }
}

/*
 * This function is responsible for:
 * - Constructing the remote absolute path for the specified file/dir
 * - Verify the existence of the file/dir
 * - Determine if the specified file/dir is in fact a file or dir or unknown if not found.
 */
static void filem_base_process_get_remote_path_cmd(prte_process_name_t* sender,
                                                   prte_buffer_t* buffer)
{
    prte_buffer_t *answer;
    int32_t count;
    char *filename = NULL;
    char *tmp_name = NULL;
    char cwd[PRTE_PATH_MAX];
    int file_type = PRTE_FILEM_TYPE_UNKNOWN;
    struct stat file_status;
    int rc;

    count = 1;
    if (PRTE_SUCCESS != (rc = prte_dss.unpack(buffer, &filename, &count, PRTE_STRING))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        goto CLEANUP;
    }

    /*
     * Determine the absolute path of the file
     */
    if (filename[0] != '/') { /* if it is not an absolute path already */
        if (NULL == getcwd(cwd, sizeof(cwd))) {
            return;
        }
        prte_asprintf(&tmp_name, "%s/%s", cwd, filename);
    }
    else {
        tmp_name = strdup(filename);
    }

    prte_output_verbose(10, prte_filem_base_framework.framework_output,
                        "filem:base: process_get_remote_path_cmd: %s -> %s: Filename Requested (%s) translated to (%s)",
                        PRTE_NAME_PRINT(PRTE_PROC_MY_NAME),
                        PRTE_NAME_PRINT(sender),
                        filename, tmp_name);

    /*
     * Determine if the file/dir exists at that absolute path
     * Determine if the file/dir is a file or a directory
     */
    if (0 != (rc = stat(tmp_name, &file_status) ) ){
        file_type = PRTE_FILEM_TYPE_UNKNOWN;
    }
    else {
        /* Is it a directory? */
        if(S_ISDIR(file_status.st_mode)) {
            file_type = PRTE_FILEM_TYPE_DIR;
        }
        else if(S_ISREG(file_status.st_mode)) {
            file_type = PRTE_FILEM_TYPE_FILE;
        }
    }

    /*
     * Pack up the response
     * Send back the reference type
     * - PRTE_FILEM_TYPE_FILE    = File
     * - PRTE_FILEM_TYPE_DIR     = Directory
     * - PRTE_FILEM_TYPE_UNKNOWN = Could not be determined, or does not exist
     */
    answer = PRTE_NEW(prte_buffer_t);
    if (PRTE_SUCCESS != (rc = prte_dss.pack(answer, &tmp_name, 1, PRTE_STRING))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        PRTE_RELEASE(answer);
        goto CLEANUP;
    }
    if (PRTE_SUCCESS != (rc = prte_dss.pack(answer, &file_type, 1, PRTE_INT))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        PRTE_RELEASE(answer);
        goto CLEANUP;
    }

    if (0 > (rc = prte_rml.send_buffer_nb(sender, answer,
                                          PRTE_RML_TAG_FILEM_BASE_RESP,
                                          prte_rml_send_callback, NULL))) {
        PRTE_ERROR_LOG(rc);
        PRTE_FORCED_TERMINATE(PRTE_ERROR_DEFAULT_EXIT_CODE);
        PRTE_RELEASE(answer);
    }

 CLEANUP:
    if( NULL != filename) {
        free(filename);
        filename = NULL;
    }
    if( NULL != tmp_name) {
        free(tmp_name);
        tmp_name = NULL;
    }
}
