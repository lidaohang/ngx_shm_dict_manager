#ifndef _NGX_SHM_DICT_MANAGER_MODULE_H_INCLUDED_
#define _NGX_SHM_DICT_MANAGER_MODULE_H_INCLUDED_

#include <ngx_event.h>
#include <ngx_core.h>
#include <ngx_config.h>

#define MAX_LINE 1024
#define READ_BUFFER 81920

typedef struct {
    ngx_flag_t                       enable;
    ngx_uint_t                       port;
    ngx_msec_t                       interval;;

    ngx_int_t 						 load_average;
    ngx_int_t   					 mem_size;
	
    ngx_socket_t                     fd;
    ngx_event_t                      expire_event;
    ngx_str_t                        shm_name;
} ngx_shm_dict_manager_conf_t;





#endif /* _NGX_SHM_DICT_MANAGER_MODULE_H_INCLUDED_ */
