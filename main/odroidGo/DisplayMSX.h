#ifndef DISPLAYMSX_H
#define DISPLAYMSX_H

#include <stdint.h>

#ifndef STOP_DISPLAY_FUNCTION
    #define STOP_DISPLAY_FUNCTION stop_msx_display
#endif

#ifndef RESUME_DISPLAY_FUNCTION
    #define RESUME_DISPLAY_FUNCTION resume_msx_display
#endif

void display_write_frame_msx(short left, short top, short width, short height, uint8_t* buffer, uint16_t bgColor, uint16_t* palette);

void stop_msx_display();
void resume_msx_display();

#endif /*DISPLAYMSX_H*/