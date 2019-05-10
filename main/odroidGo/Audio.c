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


#include "MSX.h"
#include "EMULib.h"
#include <esp_heap_caps.h>
#include "audio.h"
#include "settings.h"
#include "LibOdroidGo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "minIni.h"

#include "Sound.h"

#include <string.h>




//#define NO_SOUND


sample *playData;
unsigned int playLength;

uint16_t toPlayLength = 0;
QueueHandle_t audioQueue;
//ESPLAY_AUDIO_SINK sink = ESPLAY_AUDIO_SINK_NONE;

int volLevel;
char stop = 0;
void audioTask(void* arg)
{
  // sound
  uint16_t* param;

  while(!stop)
  {
    xQueuePeek(audioQueue, &param, portMAX_DELAY);
#ifndef NO_SOUND
   if (! stop) {
       RenderAndPlayAudio(toPlayLength);
       audio_submit(playData, playLength/2);
   }
#endif
    xQueueReceive(audioQueue, &param, portMAX_DELAY);
  }

  printf("audioTask: exiting.\n");
  audio_terminate();

  vTaskDelete(NULL);

  
}


unsigned int InitAudio(unsigned int Rate,unsigned int Latency) {
    
    stop = 0;
    char buf[3];
    
    ini_gets("FMSX", "DAC", "0", buf, 3, FMSX_CONFIG_FILE);    
    
    audioQueue = xQueueCreate(1, sizeof(uint16_t*));
    
    audio_init(Rate);
    volLevel = ini_getl("FMSX", "VOLUME", ESPLAY_VOLUME_LEVEL1, FMSX_CONFIG_FILE);
    
    if (volLevel >= ESPLAY_VOLUME_LEVEL_COUNT || volLevel < ESPLAY_VOLUME_LEVEL0) volLevel = ESPLAY_VOLUME_LEVEL1;
    audio_volume_set(volLevel);
    
    xTaskCreatePinnedToCore(&audioTask, "audioTask", 4096, NULL, 5, NULL, 1);
    
    return Rate;
}
void TrashAudio(void){

}

void audio_volume_set_change() {
    ets_delay_us(100000); // have to wait a little (other transactions have to finish?)
    volLevel = (volLevel + 1) % ESPLAY_VOLUME_LEVEL_COUNT;
    audio_volume_set(volLevel);
    ini_putl("FMSX", "VOLUME", volLevel, FMSX_CONFIG_FILE);
    
}

void pause_audio() {
    void* tempPtr = (void*)0x1234;
    stop=1;
    xQueueSend(audioQueue, &tempPtr, portMAX_DELAY); // to wait until sound was send
    
    TrashAudio();
}

void restart_audio() {
    InitAudio(AUDIO_SAMPLE_RATE, 0);
}

/** TrashAudio() *********************************************/
/** Free resources allocated by InitAudio().                **/
/*************************************************************/

/** PlayAllSound() *******************************************/
/** Render and play given number of microseconds of sound.  **/
/************************************ TO BE WRITTEN BY USER **/
void PlayAllSound(int uSec) {
#ifdef NO_SOUND
    return;
#endif
//#ifdef WITH_WLAN
//    if (getMultiplayState() == MULTIPLAYER_CONNECTED_CLIENT) return;
//#endif
    void* tempPtr = (void*)0x1234;
    toPlayLength = 2*uSec*AUDIO_SAMPLE_RATE/1000000;
    xQueueSend(audioQueue, &tempPtr, portMAX_DELAY);
        
}

unsigned int WriteAudio(sample *Data,unsigned int Length)
{
    playData = Data;
    playLength = Length;
    return Length;
}
unsigned int GetFreeAudio(void)
{
    return 1024;
    
}
