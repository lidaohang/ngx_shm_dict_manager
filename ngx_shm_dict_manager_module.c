#include "ngx_shm_dict_manager_module.h"

#include "ngx_sys_info.h"
#include "ngx_http_string_parser.h"
#include "ngx_http_shm_dict_module.h"



static char *ngx_shm_dict_manager(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_shm_dict_manager_create_conf(ngx_conf_t *cf);
static char *ngx_shm_dict_manager_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);	
static ngx_int_t ngx_shm_dict_manager_prepare(ngx_cycle_t *cycle);
static ngx_int_t ngx_shm_dict_manager_process_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_shm_dict_manager_loop(ngx_cycle_t *cycle);
static void ngx_shm_dict_manager_process_exit(ngx_cycle_t *cycle);

static void ngx_shm_dict_manager_expire(ngx_event_t *event);

static void ngx_shm_dict_manager_accept(ngx_event_t *ev);
static void ngx_shm_dict_manager_read(ngx_event_t *ev);
static void ngx_shm_dict_manager_write(ngx_event_t *ev);

static ngx_command_t ngx_shm_dict_manager_commands[] = {

    { ngx_string("listen"),
      NGX_PROC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_PROC_CONF_OFFSET,
      offsetof(ngx_shm_dict_manager_conf_t, port),
      NULL },

    { ngx_string("ah_shm_dict_name"),
      NGX_PROC_CONF|NGX_CONF_FLAG,
      ngx_shm_dict_manager,
      NGX_PROC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("interval"),
      NGX_PROC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_msec_slot,
      NGX_PROC_CONF_OFFSET,
      offsetof(ngx_shm_dict_manager_conf_t, interval),
      NULL },
	  
	{ ngx_string("load_average"),
      NGX_PROC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_msec_slot,
      NGX_PROC_CONF_OFFSET,
      offsetof(ngx_shm_dict_manager_conf_t, load_average),
      NULL },

	{ ngx_string("mem_size"),
      NGX_PROC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_msec_slot,
      NGX_PROC_CONF_OFFSET,
      offsetof(ngx_shm_dict_manager_conf_t, mem_size),
      NULL },
	  
	  

      ngx_null_command
};


static ngx_proc_module_t ngx_shm_dict_manager_module_ctx = {
    ngx_string("ah_shm_dict_manager"),            /* name                     */
    NULL,                                    /* create main configration */
    NULL,                                    /* init main configration   */
    ngx_shm_dict_manager_create_conf,     /* create proc configration */
    ngx_shm_dict_manager_merge_conf,      /* merge proc configration  */
    ngx_shm_dict_manager_prepare,         /* prepare                  */
    ngx_shm_dict_manager_process_init,    /* process init             */
    ngx_shm_dict_manager_loop,            /* loop cycle               */
    ngx_shm_dict_manager_process_exit     /* process exit             */
};


ngx_module_t ngx_shm_dict_manager_module = {
    NGX_MODULE_V1,
    &ngx_shm_dict_manager_module_ctx,
    ngx_shm_dict_manager_commands,
    NGX_PROC_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};


static char *
ngx_shm_dict_manager(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t			*value;
	
    value = cf->args->elts;
	ngx_shm_dict_manager_conf_t    *ptmcf = conf;
	
    if (ngx_strcmp(value[1].data, "off") == 0) {
        return NGX_CONF_OK;
    }
	
	if (value->len == 0) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"[ah_shm_dict_manager] \"%V\" must have \"ah_shm_dict_name\" parameter",
						   &cmd->name);
		return NGX_CONF_ERROR;
	}
	
	
	ptmcf->shm_name = value[1];
    ptmcf->enable = 1;

    return NGX_CONF_OK;
}


static void *
ngx_shm_dict_manager_create_conf(ngx_conf_t *cf)
{
    ngx_shm_dict_manager_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_shm_dict_manager_conf_t));
    if (conf == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "[ah_shm_dict_manager] create proc conf error");
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    conf->port = NGX_CONF_UNSET_UINT;
    conf->interval = NGX_CONF_UNSET_MSEC;

    return conf;
}


static char *
ngx_shm_dict_manager_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_shm_dict_manager_conf_t  *prev = parent;
    ngx_shm_dict_manager_conf_t  *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_uint_value(conf->port, prev->port, 0);
    ngx_conf_merge_msec_value(conf->interval, prev->interval, 3000);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_shm_dict_manager_prepare(ngx_cycle_t *cycle)
{
    ngx_shm_dict_manager_conf_t *ptmcf;

    ptmcf = ngx_proc_get_conf(cycle->conf_ctx, ngx_shm_dict_manager_module);
    if (!ptmcf->enable) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_shm_dict_manager_process_init(ngx_cycle_t *cycle)
{
    int                             reuseaddr;
    ngx_event_t                    *rev,*wev, *expire;
    ngx_socket_t                    fd;
    ngx_connection_t               *c;
    struct sockaddr_in              sin;
    ngx_shm_dict_manager_conf_t *conf;

    conf = ngx_proc_get_conf(cycle->conf_ctx, ngx_shm_dict_manager_module);

    fd = ngx_socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init ngx_socket error");
        return NGX_ERROR;
    }

    reuseaddr = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *) &reuseaddr, sizeof(int))
        == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                      "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init setsockopt(SO_REUSEADDR) failed");

        ngx_close_socket(fd);
        return NGX_ERROR;
    }

    if (ngx_nonblocking(fd) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                      "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init ngx_nonblocking failed");

        ngx_close_socket(fd);
        return NGX_ERROR;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(conf->port);

    if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init bind error");
        return NGX_ERROR;
    }

    if (listen(fd, 20) == -1) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init listen error");
        return NGX_ERROR;
    }

    c = ngx_get_connection(fd, cycle->log);
    if (c == NULL) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[ah_shm_dict_manager] ngx_shm_dict_manager_process_init ngx_get_connection no connection");
        return NGX_ERROR;
    }

    c->log = cycle->log;
    rev = c->read;
    rev->log = c->log;
    rev->accept = 1;
    rev->handler = ngx_shm_dict_manager_accept;

    if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
        return NGX_ERROR;
    }

	
	c->log = cycle->log;
	wev = c->write;
	wev->log = c->log;
	wev->ready = 1;
	wev->handler = ngx_shm_dict_manager_write;

	if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
		return NGX_ERROR;
	}

    conf->fd = fd;

    expire = &conf->expire_event;

    expire->handler = ngx_shm_dict_manager_expire;
    expire->log = cycle->log;
    expire->data = conf;
    expire->timer_set = 0;
    
    ngx_add_timer(expire, conf->interval);

    return NGX_OK;
}



static void
ngx_shm_dict_manager_expire(ngx_event_t *event)
{
    ngx_int_t  rc;
    ngx_shm_dict_manager_conf_t *conf;
	ngx_shm_zone_t **zone;
    size_t i;
	ngx_int_t  load_average;

    conf = event->data;
	
	ngx_log_error(NGX_LOG_EMERG, event->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_expire-------------------------------------------------------------------------------\n");

    if (conf == NULL || g_shm_dict_list == NULL) {
    	ngx_log_error(NGX_LOG_EMERG, event->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_conf_t is null \n");
        return;
    }

    zone =g_shm_dict_list->elts;
	if( zone == NULL ) {
		ngx_log_error(NGX_LOG_EMERG, event->log, 0,"[ah_shm_dict_manager] shm_dict_list is null \n");
		return;
	}
	
	rc = ngx_get_loadavg(&load_average, 1, event->log);
    if (rc == NGX_ERROR) {
        load_average = 0;
    }
	
	//todo
	ngx_meminfo_t  m;
    rc = ngx_get_meminfo(&m, event->log);
    if (rc == NGX_ERROR) {
        //mem_size = 0;
    }

	for (i = 0; i < g_shm_dict_list->nelts; i++) {
		//load_arerage
		if( conf->load_average < load_average ) {
			break;
		}		
		
		//todo mem_size
		
		//todo connections

		//flush expired
		ngx_shm_dict_flush_expired(zone[i],1);

		ngx_log_error(NGX_LOG_EMERG, event->log, 0,"[ah_shm_dict_manager] process=[%d] ngx_shm_dict_manager_expire name=[%s] count=[%d] \n",ngx_getpid(),zone[i]->shm.name.data,1);
		
    }

    ngx_add_timer(event, conf->interval);
}


static ngx_int_t
ngx_shm_dict_manager_loop(ngx_cycle_t *cycle)
{
    return NGX_OK;
}


static void
ngx_shm_dict_manager_process_exit(ngx_cycle_t *cycle)
{
    ngx_shm_dict_manager_conf_t *conf;

    conf = ngx_proc_get_conf(cycle->conf_ctx, ngx_shm_dict_manager_module);

    if (conf->fd) {
        ngx_close_socket(conf->fd);
    }

    if (conf->expire_event.timer_set) {
        ngx_del_timer(&conf->expire_event);
    }
}



static void
ngx_shm_dict_manager_accept(ngx_event_t *ev)
{
    u_char                sa[NGX_SOCKADDRLEN];
    socklen_t             socklen;
    ngx_connection_t     *lc,*c;
    ngx_event_t                    *rev;
	ngx_socket_t          s;
	
    lc = ev->data;
	
    ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_accept-------------------------------------------------------------------------------\n");
	
	s = accept(lc->fd, (struct sockaddr *) sa, &socklen);
	if (s == -1) {
		return;
	}
	
	if (ngx_nonblocking(s) == -1) {
		ngx_close_socket(s);
	}
	
	c = ngx_get_connection(s, ev->log);
	if (c == NULL) {
		ngx_close_socket(s);
		return;
	}

	c->log = ev->log;
	rev = c->read;
	rev->log = c->log;
	//rev->accept = 1;
	rev->handler = ngx_shm_dict_manager_read;

	if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
		ngx_close_socket(s);
		return;
	}

}




static void
ngx_shm_dict_manager_read(ngx_event_t *ev)
{
    ngx_connection_t      *lc;
    lc = ev->data;
	
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_read-------------------------------------------------------------------------------\n");
	
	int done = 0;
	ssize_t count = 0;

	char buf[READ_BUFFER];
	ssize_t readline = 0;

	bzero(buf, MAX_LINE);

	while(1){
		count = ngx_read_fd(lc->fd,buf,MAX_LINE);
		if(count == -1){
			if(errno == EAGAIN){
				// �����Ƿ������ģʽ,���Ե�errnoΪEAGAINʱ,��ʾ��ǰ������������ݿɶ�
				// ������͵����Ǹô��¼��Ѵ��?.
				done = 1;
				break;
			}
			else if (errno == ECONNRESET){
				// �Է�������RST
				 ngx_close_socket(lc->fd);
				 lc->fd = -1;
				 ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_read counterpart send out RST\n");
				 break;
			}
			else if(errno == EINTR){
				// ���ź��ж�
				continue;
			}
			else{
				//������ֲ��Ĵ���
				ngx_close_socket(lc->fd);
				lc->fd = -1;
				ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_read unrecovable error\n");
				break;
			}

		}
		else if(count == 0){
			 // �����ʾ�Զ˵�socket����ر�.���͹�FIN�ˡ�
			ngx_close_socket(lc->fd);
			lc->fd = -1;
			ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_read ounterpart has shut off\n");
			break;
		}
		if(count >0)
			readline += count;
	}
	
	if(!done) {
		return;
	}
	
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_read buf=[%s]\n",buf);
	
	ngx_shm_string_parser(ev,lc->fd,buf,readline);
}


static void
ngx_shm_dict_manager_write(ngx_event_t *ev)
{
    ngx_str_t             output = ngx_string("test");
	ngx_connection_t      *lc;
    lc = ev->data;
   
   ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_dict_manager_write-------------------------------------------------------------------------------\n");
   
   //todo
    ngx_write_fd(lc->fd, output.data, output.len);

    ngx_close_socket(lc->fd);
}
