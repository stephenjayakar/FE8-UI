#include "extended_presentation.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    Fe8ExtendedPresentation presentation = {0};
    assert(fe8_presentation_update(&presentation, true, true) ==
        FE8_PRESENTATION_INACTIVE);
    assert(fe8_presentation_update(&presentation, true, true) ==
        FE8_PRESENTATION_LIVE);
    assert(fe8_presentation_update(&presentation, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true) ==
        FE8_PRESENTATION_LIVE);
    assert(fe8_presentation_update(&presentation, false, true) ==
        FE8_PRESENTATION_INACTIVE);
    puts("extended presentation tests passed");
    return 0;
}
