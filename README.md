# ngx_shm_dict_manager
##添加定时器事件，定时的清除共享内存中过期的key
##添加读事件，支持redis协议，通过redis-cli get,set,del,ttl 



processes {

    process ah_shm_dict_manager {
        ah_shm_dict_name test;
        interval 3s;
     
        delay_start 300ms;
        listen 8010;
    }
}
