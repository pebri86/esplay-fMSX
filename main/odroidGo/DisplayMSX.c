#pragma GCC optimize ("O3")
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DisplayMSX.h"
#include "display.h"
#include "disp_spi.h"

extern uint16_t *line[];
char stopDisplay = 0;

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LINE_COUNT (4)

void display_write_frame_msx(short left, short top, short width, short height, uint8_t* buffer, uint16_t bgColor, uint16_t* palette)
{
    if (stopDisplay) return;
    
    // todo: center, draw border, maybe even faster?...
    short x, y;

    if (left < 0 || top < 0) abort();
    if (width < 1 || height < 1) abort();

    //xTaskToNotify = xTaskGetCurrentTaskHandle();

    int sending_line = -1;
    int calc_line = 0;

    if (buffer == NULL)
    {
        for (y = 0; y < LCD_HEIGHT; ++y)
        {
            for (x = 0; x < LCD_WIDTH; x++)
            {
                line[calc_line][x] = bgColor;
            }
            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, 0, LCD_WIDTH, line[sending_line], 1);
        }
        send_line_finish();
    }
    else
    {
        for (y = 0; y < height; y+=LINE_COUNT) // let's send x lines with one msg
        {
            for (int x = 0; x < width*LINE_COUNT; x++) {
                line[calc_line][x] = palette[buffer[x+(y*width)]];
            }
            if (sending_line != -1)
            send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y+top, left, width, line[sending_line], LINE_COUNT);            
        }
        
        send_line_finish();
    }
}

void stop_msx_display()
{
    stopDisplay = 1;    
}

void resume_msx_display()
{
    stopDisplay = 0;
}
