SET(SOURCES ui_TrumaMainScreen.c
    ui_ErrorScreen.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    fonts/ui_font_Roboto.c
    fonts/ui_font_Roboto38.c)

add_library(ui ${SOURCES})
