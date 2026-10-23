# Fontes do jogo (runtime) comuns ao Windows (CMakeLists.txt) e ao Android (android/CMakeLists.txt),
# a partir da raiz do projeto (PM_ROOT)
set(RUNTIME_SOURCES)
foreach(f
        main.c cpu.c bus.c vdp.c io.c sha1.c musashi_glue.c lockstep.c sound.c ym_shim.cpp menu.c
        filters.c savestate.c png.c intro_logos.c border.c translate.c opening.c bossrush.c gamepad.c
        touch.c touch_icons.c game/map_stream.c game/hud.c game/objects.c)
    list(APPEND RUNTIME_SOURCES "${PM_ROOT}/runtime/${f}")
endforeach()
