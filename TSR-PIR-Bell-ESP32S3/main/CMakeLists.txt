idf_component_register(SRCS 
                        "main.c" 
                        "app_config.c"
                        "app_chirp.c"
                        "app_ip_announce.c"
                        "bsp/bsp_led.c"
                        "bsp/bsp_key.c" 
                        "bsp/bsp_ntp.c" 
                        "bsp/bsp_wifiap.c" 
                        "bsp/bsp_wifistation.c"                        
                        "bsp/bsp_webserver.c"
                        "bsp/bsp_web_auth.c"
                        "bsp/bsp_web_system.c"
                        "bsp/bsp_web_wifi.c"
                        "bsp/bsp_web_audio.c"
                        "bsp/bsp_web_doorbell.c"
                        "bsp/bsp_littlefs.c"
                        "bsp/bsp_es8311.c"
                        "bsp/bsp_player.cpp"
                        "bsp/bsp_player_pcm.cpp"
                        "bsp/bsp_player_mp3.cpp"
                        "bsp/bsp_chirp.c"
                        "bsp/bsp_ws2812.c"
                        "bsp/bsp_led_status.c"
                        "bsp/bsp_battery.c"
                        
                    INCLUDE_DIRS "." "./bsp"
                    EMBED_FILES
                        "web_content/index.html"
                        "web_content/login.html"
                        "web_content/style.css"
                        "web_content/logo.png"
                        "web_content/common.js"
                        "web_content/doorbell.js"
                        "web_content/audio.js"
                        "web_content/wifi.js"
                        "web_content/web-main.js")

set_source_files_properties(bsp/bsp_webserver.c PROPERTIES
    COMPILE_OPTIONS "-Wno-error=format-truncation")

littlefs_create_partition_image(audio audio_content FLASH_IN_PROJECT)