#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "rotate.h"
int main(void) {
    const uint16_t input[] = {1,2,3,4,5,6};
    const uint16_t clockwise[] = {4,1,5,2,6,3};
    uint16_t out[8] = {0};
    adsb_rotate565(out + 1, input, 3, 2);
    for (int n = 0; n < 6; n++) assert(out[n+1] == clockwise[n]);
    assert(out[0] == 0 && out[7] == 0);
    // Reconstruct the physical panel from partial strips, including the final short strip.
    static uint16_t logical[800*480], panel[480*800], strip[800*24];
    for (int n = 0; n < 800*480; n++) logical[n] = (uint16_t)(n * 17 + n / 800);
    for (int y = 0; y < 480; y += 23) {
        int h = 480-y < 23 ? 480-y : 23;
        adsb_rotate565(strip, logical + y*800, 800, h);
        for (int py = 0; py < 800; py++)
            for (int px = 0; px < h; px++) panel[py*480 + 480-y-h+px] = strip[py*h+px];
    }
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 800; x++) assert(panel[x*480+479-y] == logical[y*800+x]);
    puts("RGB565 rotation, buffer guards and full panel partial-strip mapping passed");
}
