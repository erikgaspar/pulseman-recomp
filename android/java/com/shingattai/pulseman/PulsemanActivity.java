package com.shingattai.pulseman;

import java.io.File;

import org.libsdl.app.SDLActivity;

/* O jogo: a SDLActivity do SDL2 carrega libSDL2.so e libmain.so (o mesmo código C da versão
 * do instalador) e chama o main com o caminho da ROM que a LauncherActivity guardou. */
public class PulsemanActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        return new String[] {LauncherActivity.romFile(this).getAbsolutePath()};
    }
}
