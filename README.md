# ngx_shm_dict_manager
##添加定时器事件，定时的清除共享内存中过期的key
##添加读事件，支持redis协议，通过redis-cli 可以get,set,del,ttl 查看对应的k/v



processes {

    process ah_shm_dict_manager {
        ah_shm_dict_name lands;
        interval 3s;
     
        delay_start 300ms;
        listen 8010;
    }
}
