#include <ngx_event.h>
#include <ngx_core.h>
#include <ngx_config.h>

#include <ctype.h>
#include "ngx_http_shm_dict_module.h"
#include "ngx_http_string_parser.h"


#define READ_BUFFER 81920
#define CHECK_BUFFER(pos,buf,len) do{if(((pos)-buf)>len)return(0);}while(0)
#define OPERATOR_GET   1
#define OPERATOR_TTL   2


static size_t get_int(char** i) {
  char* b = *i;
  size_t val = 0;
  while(*b != '\r') {
    val *= 10;
    val += (*b++ - '0');
    /*
     * the len may be not read completed now
     * if so, return a big int to make CHECK_BUFFER return 0
     */
    if(val>READ_BUFFER) return READ_BUFFER;
  }
  b += 2;
  *i = b;
  return val;
}

static int cmp_ignore_case(const char* a, const char* b, size_t s)
{
  size_t i;
  for (i=0; i<s; i++) {
    if(tolower(a[i])==tolower(b[i])) {
      continue;
    } else {
      return 1;
    }
  }
  return 0;
}

static void write_error(ngx_socket_t fd, const char* msg) {
  ngx_write_fd(fd, "-", 1);
  ngx_write_fd(fd, (u_char*)msg, strlen(msg));
  ngx_write_fd(fd, "\r\n", 2);
}

static void write_status(ngx_socket_t fd, const char* msg) {
  ngx_write_fd(fd, "+", 1);
  ngx_write_fd(fd, (u_char*)msg, strlen(msg));
  ngx_write_fd(fd, "\r\n", 2);
}

static ngx_shm_zone_t* get_zone_t(ngx_event_t *ev,ngx_socket_t fd,ngx_str_t *zone_name) {
	ngx_shm_zone_t **zone;
    size_t i;

	if (g_shm_dict_list == NULL) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] g_shm_dict_list is null\n");
		write_error(fd, "$gconf is null");
        return NULL;
	}
	
	zone = g_shm_dict_list->elts;
	if (zone == NULL) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] shm_dict_list is null\n");
		write_error(fd, "$shm_dict_list is null");
        return NULL;
	}
	
	for (i = 0; i < g_shm_dict_list->nelts; i++) {
		if( (zone_name ==NULL || zone_name->len == 0) && i ==0) {
			return zone[i];
		}
		
		if ( ngx_strcmp(zone_name->data,zone[i]->shm.name.data) == 0 ) {
			return zone[i];
		}
	}
	return NULL;
}



static int set(ngx_event_t *ev,ngx_socket_t fd,char* b,ssize_t len) {
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser set----------------------------------------------------------\n");
	
	CHECK_BUFFER(b+3,b,len);
	if(*b++ != '$') return -1;

	size_t key_size = get_int(&b);
	char* key = b;

	b += key_size;
	b += 2;

	CHECK_BUFFER(b+3,b,len);

	if(*b++ != '$') return -1;

	size_t val_size = get_int(&b);

	CHECK_BUFFER(b+val_size+1,b,len);

	char* val = b;

	key[key_size] = 0;
	val[val_size] = 0;
	
	//ngx_str_t zone_name = ngx_string("lands");
	ngx_str_t ngx_key = ngx_null_string;
	ngx_str_t ngx_value = ngx_null_string;
	
	ngx_key.data = (u_char*)key;
	ngx_key.len = key_size;
	
	ngx_value.data = (u_char*)val;
	ngx_value.len = val_size;
	
	ngx_shm_zone_t *zone_t = get_zone_t(ev,fd,NULL);
    if ( zone_t == NULL ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to get_shm_zone\n");
		write_error(fd, "$zone_t is null");
        return -1;
    }
	
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser set zone=[%s]\n",zone_t->shm.name.data);
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser set ngx_key.data=[%s] ngx_key.len=[%d]\n",ngx_key.data,ngx_key.len);
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser set ngx_value.data=[%s] ngx_value.len=[%d]\n",ngx_value.data,ngx_value.len);
	
	ngx_int_t rc = ngx_shm_dict_set(zone_t, &ngx_key,&ngx_value,SHM_STRING,0,0);
	 if ( rc != NGX_OK ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to ngx_shm_dict_handler_get\n");
		write_error(fd, "$set error");
        return -1;
	}	
	
	write_status(fd, "OK");
	return 0;
}



static int get(ngx_event_t *ev,ngx_socket_t fd,char* b,ssize_t len,int operator) {
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get----------------------------------------------------------\n");
	
	CHECK_BUFFER(b+3,b,len); /* 3: at least 1 num, fllowed by '\r\n'*/
	if(*b++ != '$') return -1;

	size_t size = get_int(&b);
	CHECK_BUFFER(b+size+1,b,len);

	b[size] = 0;
	
	//ngx_str_t zone_name = ngx_string("lands");
	ngx_str_t key = ngx_null_string;
	ngx_str_t value = ngx_null_string;
	uint32_t exptime = 0;
	
	key.data = (u_char*)b;
	key.len = size;
	
	
	ngx_shm_zone_t *zone_t = get_zone_t(ev,fd,NULL);
    if ( zone_t == NULL ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to get_shm_zone\n");
		write_error(fd, "$zone_t is null");
        return -1;
    }

	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",zone_t->shm.name.data);
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",key.data);

	uint8_t value_type = SHM_STRING;
	ngx_int_t rc = ngx_shm_dict_get(zone_t, &key,&value,&value_type,&exptime,0);
    if ( rc != NGX_OK ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to ngx_shm_dict_handler_get\n");
		ngx_write_fd(fd, "$-1\r\n", 5);
        return -1;
	}	
	
	
	char buf[256];
	buf[0] = '$';
	
	if( operator == OPERATOR_TTL ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  exptime=[%d]\n",exptime);

		char expt_buf[256];
		int expt_count = sprintf(expt_buf, "%d", exptime);

		int count = sprintf(buf + 1, "%d", expt_count);

		ngx_write_fd(fd, buf, count + 1);
		ngx_write_fd(fd, "\r\n", 2);

		ngx_write_fd(fd, expt_buf, expt_count);
		ngx_write_fd(fd, "\r\n", 2);
		return 0;
	}
	
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  value=[%s]",value.data);
	
	int count = sprintf(buf + 1, "%ld", value.len);

	ngx_write_fd(fd, buf, count + 1);
	ngx_write_fd(fd, "\r\n", 2);
	ngx_write_fd(fd, value.data, value.len);
	ngx_write_fd(fd, "\r\n", 2);
	return 0;

}


static int incr(ngx_event_t *ev,ngx_socket_t fd,char* b,ssize_t len) {
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser incr----------------------------------------------------------\n");
	
	CHECK_BUFFER(b+3,b,len); /* 3: at least 1 num, fllowed by '\r\n'*/
	if(*b++ != '$') return -1;

	size_t size = get_int(&b);
	CHECK_BUFFER(b+size+1,b,len);

	b[size] = 0;
	
	//ngx_str_t zone_name = ngx_string("lands");
	ngx_str_t key = ngx_null_string;
	int64_t res;
	
	key.data = (u_char*)b;
	key.len = size;
	
	ngx_shm_zone_t *zone_t = get_zone_t(ev,fd,NULL);
    if ( zone_t == NULL ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to get_shm_zone\n");
		write_error(fd, "$zone_t is null");
        return -1;
    }

	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",zone_t->shm.name.data);
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",key.data);

	ngx_int_t rc = ngx_shm_dict_inc_int(zone_t, &key, 1,0, &res);
    if ( rc != NGX_OK ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to ngx_shm_dict_handler_get\n");
		ngx_write_fd(fd, "$-1\r\n", 5);
        return -1;
	}	
	
	
	char buf[256];
	buf[0] = '$';
	
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  exptime=[%ld]\n",res);

	char res_buf[256];
	int res_count = sprintf(res_buf, "%ld", res);

	int count = sprintf(buf + 1, "%d", res_count);
	
	ngx_write_fd(fd, buf, count + 1);
	ngx_write_fd(fd, "\r\n", 2);
	ngx_write_fd(fd, res_buf, res_count);
	ngx_write_fd(fd, "\r\n", 2);
	return 0;

}


static int del(ngx_event_t *ev,ngx_socket_t fd,char* b,ssize_t len) {
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser del----------------------------------------------------------\n");
	
	CHECK_BUFFER(b+3,b,len); /* 3: at least 1 num, fllowed by '\r\n'*/
	if(*b++ != '$') return -1;

	size_t size = get_int(&b);
	CHECK_BUFFER(b+size+1,b,len);
	b[size] = 0;
	
	ngx_str_t key = ngx_null_string;
	
	key.data = (u_char*)b;
	key.len = size;
	
	ngx_shm_zone_t *zone_t = get_zone_t(ev,fd,NULL);
    if ( zone_t == NULL ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to get_shm_zone\n");
		write_error(fd, "$zone_t is null");
        return -1;
    }

	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",zone_t->shm.name.data);
	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser get  key=[%s]\n",key.data);

	ngx_int_t rc = ngx_shm_dict_delete(zone_t,&key);
    if ( rc != NGX_OK ) {
		ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] failed to incr\n");
		ngx_write_fd(fd, "$-error\r\n", 9);
        return -1;
	}
	write_status(fd, "OK");
	return 0;
}	



int ngx_shm_string_parser(ngx_event_t *ev,ngx_socket_t fd,char *b,ssize_t len) {

	ngx_log_error(NGX_LOG_EMERG, ev->log, 0,"[ah_shm_dict_manager] ngx_shm_string_parser ------------------------------------------------------");

	CHECK_BUFFER(b+2,b,len);
	
	if(*b++ != '*') return -1;

	size_t count = get_int(&b);

	//check command elements
	CHECK_BUFFER(b+count,b,len);

	switch(count) {
		case 2: {
			if(*b++ != '$') return -1;

			size_t size = get_int(&b);
			CHECK_BUFFER(b+size+2,b,len);

			if(size == 3) {
				if(cmp_ignore_case(b, "get", 3) == 0) {
					return get(ev,fd,b + 5,len,OPERATOR_GET);
				}
				else if (cmp_ignore_case(b, "ttl", 3) == 0) {
					return get(ev,fd,b + 5,len,OPERATOR_TTL);
				}
				else if (cmp_ignore_case(b, "del", 3) == 0) {
					return del(ev,fd,b + 5,len);
				}
			
			} else if(size == 4) {
				if(cmp_ignore_case(b, "incr", 4) == 0) {
					return incr(ev,fd,b + 6,len);
				}
			}
			
			write_error(fd, "unknown command");
			break;
		}

		case 3: {
			if(*b++ != '$') return -1;

			size_t size = get_int(&b);
			CHECK_BUFFER(b+size+2,b,len);

			if(size == 3 && cmp_ignore_case(b, "set", 3) == 0) {
				b += size;
				b += 2;
				
			    return set(ev,fd,b,len);
			}else {
			    write_error(fd, "unknown command");
			}
			break;
		}
		  
		default: {
			write_error(fd, "unknown command");
			break;
		}
	}

	return 1;
}
