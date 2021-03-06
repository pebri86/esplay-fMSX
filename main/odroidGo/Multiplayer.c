/*
 * The MIT License
 *
 * Copyright 2018 Schuemi.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LibOdroidGo.h"

#ifdef WITH_WLAN
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "MSX.h"

#include "settings.h"

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>



#define EXAMPLE_ESP_WIFI_MODE_AP   1 //CONFIG_ESP_WIFI_MODE_AP //TRUE:AP FALSE:STA
#define MP_ESP_WIFI_SSID      "fMSXMultiplayerV0.01"
#define MP_ESP_WIFI_PASS      ""
#define MP_MAX_STA_CONN       1

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/*server and client socket*/
int LSocket,SSocket;
struct sockaddr_in Addr;
socklen_t AddrLength;
fd_set FDs;
struct timeval TV;
static struct sockaddr_in PeerAddr;


unsigned char currentLocalTickNumber = 0;
unsigned char currentRemoteTickNumber = 0;
char currentLocalJoy = 0;
char lastLocalJoy = 0;


char currentremoteJoy = 0;
char lastremoteJoy = 0;
char stopNet = 0;
char gotRemoteData = 0;
char tcpSend = 0;
bool connectionLost = false;

char playFileName[1024];
void copyFile(const char* fileName, const char* destination);
char sendFile(const char* fileName);
char recievFile(const char* fileName, char toMemory, char*memory);
void sendDataBlob(const char* data, uint16_t size);
uint16_t recievDataBlob(char* data, uint16_t maxSize);
int NETSend(const char *Out,int N);
int NETRecv(char *In,int N);
int gotAck = -1;


/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;
enum MP_SERVER_STATE{
    MP_NO_CONNECTION = 0,
    MP_CLIENT_IS_CONNECTING,
    MP_CLIENT_DISCONNECTED,
    STA_GOT_IP
} ;


enum MP_SERVER_STATE server_state;
MULTIPLAYER_STATE mpState = MULTIPLAYER_NOT_CONNECTED;


void sendTask(void* arg)
{

  char sendBuf[20];

  memset(sendBuf, 0, 20);
  while(!stopNet)
  {
      if (tcpSend && gotAck != currentLocalTickNumber) {
        sendBuf[0] = currentRemoteTickNumber;
        sendBuf[1] = currentLocalTickNumber;
        sendBuf[2] = lastLocalJoy;
        sendBuf[3] = currentLocalJoy;
 if (mpState == MULTIPLAYER_CONNECTED_SERVER)       memcpy(sendBuf + 4, KeyState, 16);
        NETSend(sendBuf,20);
      }  
      
      vTaskDelay(7 / portTICK_PERIOD_MS);
      
      
  }
  
  

  vTaskDelete(NULL);

  
}

void recievTask(void* arg)
{


  char recievBuf[20];
 
  while(!stopNet)
  {
    recievBuf[0] = 0;
    if(NETRecv(recievBuf,20)==20) {
        if (! gotRemoteData) {
            currentremoteJoy = recievBuf[3];
            lastremoteJoy = recievBuf[2];
            if (currentRemoteTickNumber != recievBuf[1]){gotRemoteData = 1; }
            currentRemoteTickNumber = recievBuf[1];
            if (currentLocalTickNumber == recievBuf[0]) gotAck = recievBuf[0];
        }
if (mpState == MULTIPLAYER_CONNECTED_CLIENT)   memcpy(KeyState, recievBuf+4, 16);
    

        
    } else {
        if (recievBuf[0] != 0xff) NETRecv(recievBuf,1);
    }
      
   
  }

  

  vTaskDelete(NULL);

  
}












static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    printf("Got event: %d\n", event->event_id);
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        printf("got ip:%s\n",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        server_state = STA_GOT_IP;
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        printf("Player is connecting "MACSTR"AID=%d\n", MAC2STR(event->event_info.sta_connected.mac),  event->event_info.sta_connected.aid);
        server_state = MP_CLIENT_IS_CONNECTING;
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        printf("Player is disconnected "MACSTR"AID=%d\n", MAC2STR(event->event_info.sta_connected.mac),  event->event_info.sta_connected.aid);
        server_state = MP_CLIENT_DISCONNECTED;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init(); 
   ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = MP_ESP_WIFI_SSID,
            .ssid_len = strlen(MP_ESP_WIFI_SSID),
            .password = MP_ESP_WIFI_PASS,
            .max_connection = MP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 1
        },
    };
    if (strlen(MP_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    //ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("wifi_init_softap finished.SSID:%s password:%s",
             MP_ESP_WIFI_SSID, MP_ESP_WIFI_PASS);
}

void wifi_init_sta()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MP_ESP_WIFI_SSID,
            .password = MP_ESP_WIFI_PASS
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    //ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    printf ("wifi_init_sta finished.\n");
    printf ("connect to ap SSID:%s password:%s\n",
             MP_ESP_WIFI_SSID, MP_ESP_WIFI_PASS);
}

MULTIPLAYER_STATE getMultiplayState(){
    return mpState;
}


void server_init()
{
    mpState = MULTIPLAYER_INIT;
    server_state = MP_NO_CONNECTION;
    wifi_init_softap();
    

    currentRemoteTickNumber = 99;

}
void client_init() {
    mpState = MULTIPLAYER_INIT;
    server_state = MP_NO_CONNECTION;
    wifi_init_sta();
   

    currentRemoteTickNumber = 98;
}
int waitKeyOrStatusChange()
{
    int key;
    int currStatus = server_state;
    do{
        vTaskDelay(1 / portTICK_PERIOD_MS);
        key = odroidFmsxGUI_getKey();
    }while (key == -1 && currStatus == server_state);
    return key;
}
const char* getMPFileName() {
    return playFileName;
}
void client_try_connect()
{
    
    odroidFmsxGUI_msgBox("Multiplayer", "Try to connect to server...\n\nPress a key to stop trying", 0);
    esplay_settings_WLAN_set(ESPLAY_WLAN_NONE);
    int key;
    playFileName[0] = 0;
    if (server_state == MP_NO_CONNECTION) key = waitKeyOrStatusChange();
    
    if (server_state == STA_GOT_IP) {
        odroidFmsxGUI_msgBox("Multiplayer", "We are connecting...", 0);
        printf("NET-CLIENT: Connecting to Server...\n");
        ip4_addr_t ipv4addr;
        IP4_ADDR(&ipv4addr, 192, 168 , 4, 1);
        memset(&Addr,0,sizeof(Addr));
        Addr.sin_addr.s_addr = ipv4addr.addr;
        Addr.sin_family = AF_INET;
        Addr.sin_port   = htons(1234);
        
        /* Create a socket */

     memcpy(&PeerAddr,&Addr,sizeof(PeerAddr));
     if((SSocket=socket(AF_INET,SOCK_DGRAM,0))<0) return;     

       
        
        printf("NET-CLIENT: Created socket...\n");

          mpState = MULTIPLAYER_CONNECTED_CLIENT;
          char buffer[6];
          memcpy(buffer, "Hello", 6);
          NETSend(buffer, 6);
          
          printf("wait rom...\n");
          
          int g = recievDataBlob(playFileName, 1024);
          printf("filename: %s\n", playFileName);
          
         /*struct timeval to;
         to.tv_sec = 1;
         to.tv_usec = 0;
         setsockopt(SSocket,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));*/
         
                        
          xTaskCreatePinnedToCore(&sendTask, "sendTask", 2048, NULL, 2, NULL, 1);
          xTaskCreatePinnedToCore(&recievTask, "recievTask", 2048, NULL, 2, NULL, 1);

        
    }
    
    
}


void server_wait_for_player()
{
    server_state = MP_NO_CONNECTION;
    odroidFmsxGUI_msgBox("Multiplayer", "Waiting for player...\n\nPress a key to stop waiting", 0);
    esplay_settings_WLAN_set(ESPLAY_WLAN_NONE);
   playFileName[0] = 0;
   
    if (waitKeyOrStatusChange() == -1 && server_state == MP_CLIENT_IS_CONNECTING) {
        odroidFmsxGUI_msgBox("Multiplayer", "Player is connecting...", 0);
        
        memset(&Addr,0,sizeof(Addr));
        Addr.sin_addr.s_addr = htonl(INADDR_ANY);
        Addr.sin_family      = AF_INET;
        Addr.sin_port        = htons(1234);
        memcpy(&PeerAddr,&Addr,sizeof(PeerAddr));
        if((SSocket=socket(AF_INET,SOCK_DGRAM,0))<0) return;
         if(bind(SSocket,(struct sockaddr *)&Addr,sizeof(Addr))<0)
        { close(SSocket);return; }

       
        
        mpState = MULTIPLAYER_CONNECTED_SERVER;
        
        char buffer[1024];
        printf("wait message...\n");
        int s = NETRecv(buffer+s, 6);
        printf("!!!!!!!%d got message: %s\n", s, buffer);
        
        
        /*
        to.tv_sec = 0;
        to.tv_usec = 10000;
        setsockopt(SSocket,SOL_SOCKET,SO_SNDTIMEO,&to,sizeof(to));  */  
        /*struct timeval to;
        to.tv_sec = 1;
        to.tv_usec = 0;
        setsockopt(SSocket,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));*/
        
        char* rom = get_rom_name_settings();
        sendDataBlob(rom, strlen(rom) + 1);
        memcpy(playFileName, rom, strlen(rom) + 1);
        free(rom);
        
        
        xTaskCreatePinnedToCore(&sendTask, "sendTask", 2048, NULL, 5, NULL, 1);
        xTaskCreatePinnedToCore(&recievTask, "recievTask", 2048, NULL, 5, NULL, 1);        
        
        

    }
    
    
}


/** NETSend() ************************************************/
/** Send N bytes. Returns number of bytes sent or 0.        **/
/*************************************************************/
int NETSend(const char *Out,int N)
{
  int J,I;

  if (mpState != MULTIPLAYER_CONNECTED_CLIENT && mpState != MULTIPLAYER_CONNECTED_SERVER) return 0;

  /* Send data */
  for(I=J=N;(J>=0)&&I;)
  {

    J = sendto(SSocket,Out,I,0,(struct sockaddr *)&PeerAddr,sizeof(PeerAddr));
    
    if(J>0) { Out+=J;I-=J; }
  }

  /* Return number of bytes sent */
  return(N-I);
}

/** NETRecv() ************************************************/
/** Receive N bytes. Returns number of bytes received or 0. **/
/*************************************************************/
int NETRecv(char *In,int N)
{
  int J,I;
  socklen_t AddrLen = sizeof(PeerAddr);
  /* Have to have a socket */
  if (mpState != MULTIPLAYER_CONNECTED_CLIENT && mpState != MULTIPLAYER_CONNECTED_SERVER) return 0;



  /* Receive data */
  for(I=J=N;(J>=0)&&I;)
  {

    J = recvfrom(SSocket,In,I,0,(struct sockaddr *)&PeerAddr,&AddrLen);

    if(J>0) { In+=J;I-=J; }
  }
  
 
  /* Return number of bytes received */
  return(N-I);
}
uint16_t crc16(const unsigned char* data_p, int length){
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while (length--){
        x = crc >> 8 ^ *data_p++;
        x ^= x>>4;
        crc = (crc << 8) ^ ((unsigned short)(x << 12)) ^ ((unsigned short)(x <<5)) ^ ((unsigned short)x);
    }
    return crc;
}
#define FILEBUFFER_SIZE 1024
void copyFile(const char* fileName, const char* destination) {
    
    FILE* source = _fopen(fileName, "rb");
    FILE* dest = _fopen(destination, "wb");
    if (! source || ! dest) return;
    char* buffer;
    buffer = (char*)malloc(FILEBUFFER_SIZE);
    size_t s;
    do {
       
        s = _fread(buffer,1,FILEBUFFER_SIZE,source);
        _fwrite(buffer, 1, s, dest);   
    } while (s == FILEBUFFER_SIZE);
    
   _fclose(source);
   _fclose(dest);
   free(buffer);

   return;
   
}

/////// sendFile and recievFile are for future use, not working yet

char sendFile(const char* fileName) {
    char* buffer;
    buffer = (char*)malloc(FILEBUFFER_SIZE);
    FILE* f = _fopen(fileName, "rb");
    if (! f) {printf("could not open file! %s\n", fileName);return 0;}
    _fseek(f,0,SEEK_END);
    size_t size=_ftell(f);
    _rewind(f);
    
    NETSend((char*)&size, 4);
   
    uint16_t parnum = 0;
    uint16_t parts = (size / FILEBUFFER_SIZE) + 1;
    while (parnum < parts) {
        _fseek(f,0,parnum*FILEBUFFER_SIZE);
        size_t s = _fread(buffer + 2,1,FILEBUFFER_SIZE-4,f);
        memcpy(buffer, &parnum, 2);
        
        uint16_t crc = crc16(buffer, s+2);
        memcpy(buffer + s + 2, (char*)&crc , 2);
        sendDataBlob(buffer, s + 4);
        NETRecv((char*)&parnum, 2);
        
    }
    _fclose(f);
    
    free(buffer);
    
    return 1;
}


char recievFile(const char* fileName, char toMemory, char*memory) {
    char* buffer;
    char* memPosition;
    buffer = (char*)malloc(FILEBUFFER_SIZE);
    FILE* f = NULL;
    if (! toMemory) {
        f = _fopen(fileName, "wb");
        if (f==NULL) return 0;
    }
    size_t size = 0;
    size_t recievedSize = 0;
    NETRecv((char*)&size, 4);
    uint16_t gettingPartnum = 0;
    uint16_t partnum = 0;
    uint16_t parts = (size / FILEBUFFER_SIZE) + 1;
    if (toMemory) memPosition = memory = malloc(size);
    
    while(gettingPartnum < parts) {
        uint16_t r = recievDataBlob(buffer, FILEBUFFER_SIZE);
        memcpy((char*)&partnum, buffer, 2);
        if (r < 4)  { NETSend((char*)&gettingPartnum, 2); continue;} 
        if (partnum != gettingPartnum) { NETSend((char*)&gettingPartnum, 2); continue;} // what part should this be?
        
        uint16_t crc = crc16(buffer, r-2);
        uint16_t recrc;
        memcpy((char*)&recrc, buffer + (r - 2), 2);
        if (crc != recrc) { NETSend((char*)&gettingPartnum, 2); continue;} 
        
        gettingPartnum++;
        NETSend((char*)&gettingPartnum, 2);
        
        recievedSize += r;
        if (toMemory){
            memcpy(memPosition, buffer + 2, r - 4);
            memPosition += (r-4);
        }
        else
            _fwrite(buffer + 2, 1, r - 4, f);   
        
        
    }
     
    if (! toMemory)_fclose(f);
    
    free(buffer);
    
    return 1;
}

void sendDataBlob(const char* data, uint16_t size) {
    NETSend((char*)&size, 2);
    NETSend(data, size);

}
uint16_t recievDataBlob(char* data, uint16_t maxSize) {
    
    uint16_t datalength;
    NETRecv((char*)&datalength, 2);
    //printf("Got datalength: %d\n", datalength);
    uint16_t tooMuch = 0;
    
    if (datalength > maxSize) {tooMuch = datalength - maxSize; datalength = maxSize;}
    uint16_t r = NETRecv(data, datalength);
    //printf("got data; %d\n", r );
   
    return r;
}

inline void copyKeyState(byte* a, byte* b) {
    memcpy(a,b,16);
}
bool isConnectionLost() {
    return connectionLost;
}
void exchangeJoystickState(uint16_t* state)
{

   if (mpState == MULTIPLAYER_NOT_CONNECTED) return;
   
   tcpSend = 1;
   
   static int running = 0;
   if (running < 30) {
       running++;
       while (! gotRemoteData){vTaskDelay(1);}
   } else {
       int max = 0;
       while (! gotRemoteData && ++max < 400){vTaskDelay(1);}
       if (currentLocalTickNumber && max == 400) { connectionLost = true; }
   }
   
   
   gotRemoteData = 0;
   

   char rJoy = 0;
   
   if (currentRemoteTickNumber > currentLocalTickNumber || (currentRemoteTickNumber == 0 && currentLocalTickNumber == 255)) 
       rJoy = lastremoteJoy;
   else rJoy = currentremoteJoy;
   
   lastLocalJoy = currentLocalJoy;
   currentLocalJoy = *state & 0xff;
   if (mpState == MULTIPLAYER_CONNECTED_CLIENT) *state = (lastLocalJoy << 8) + rJoy;
   if (mpState == MULTIPLAYER_CONNECTED_SERVER) *state = (rJoy << 8) + lastLocalJoy;
   
   currentLocalTickNumber++;
   
   
   return;
}

#endif
