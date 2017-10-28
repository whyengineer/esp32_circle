# ESP32_circle

## hardware:
<img src="https://wiki.whyengineer.com/images/c/c3/Esp32_480x480.png" width=256 height=256 />

Know More:
[WhyEngineer](https://www.whyengineer.com/esp32/)
## software:
this is a simple web serber framwokr
## how to use: 
1.copy thr www folder to sd card / directory

2.add the url handle function in the componets/webserver/webserver.c

```c
      void web_index(http_parser* a,char*url,char* body);
      void led_ctrl(http_parser* a,char*url,char* body);
      void load_logo(http_parser* a,char*url,char* body);
      void load_esp32(http_parser* a,char*url,char* body);
      void ble_scan(http_parser* a,char*url,char* body);

      const HttpHandleTypeDef http_handle[]={
        {"/",web_index},
        {"/api/led/",led_ctrl},
        {"/static/logo.png",load_logo},
        {"/static/esp32.png",load_esp32},
        {"/api/ble_scan/",ble_scan},
      };
  
```
