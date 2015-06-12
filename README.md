# ngx_shm_dict_manager version


processes {

    process ah_shm_dict_manager {
        ah_shm_dict_name lands;
        interval 3s;
     
        delay_start 300ms;
        listen 8010;
    }
}
