#ifndef _BSP_WEBSERVER_H_
#define _BSP_WEBSERVER_H_

#include "esp_http_server.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEBSERVER_PORT      80

/* ===== 外壳 ===== */
void WebServerInit(void);
httpd_handle_t WebServerGetHandle(void);

/* ===== 各功能域注册函数（外壳调） ===== */
esp_err_t BspWebLoginRegister(httpd_handle_t server);
esp_err_t BspWebSystemRegister(httpd_handle_t server);
esp_err_t BspWebWifiRegister(httpd_handle_t server);
esp_err_t BspWebAudioRegister(httpd_handle_t server);
esp_err_t BspWebDoorbellRegister(httpd_handle_t server);

/* ===== 统一鉴权校验（读 Cookie tsr_token，匹配且未过期返回 true，并滑动刷新） ===== */
bool BspWebCheckAuth(httpd_req_t *req);

/* ===== WiFi 辅助（wifi 模块实现） ===== */
bool GetStaActiveIpInfo(esp_netif_ip_info_t *ip_info,
                        esp_ip4_addr_t *dns1,
                        esp_ip4_addr_t *dns2);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_WEBSERVER_H_ */