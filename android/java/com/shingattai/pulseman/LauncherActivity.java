package com.shingattai.pulseman;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/* Tela inicial: o jogo não vem com nenhum arquivo do Pulseman. Na primeira vez, o jogador
 * escolhe a ROM japonesa original (.md/.bin/.gen ou .zip) pelo seletor de arquivos do Android;
 * ela é conferida pelo SHA-1 e guardada na pasta do app. Das outras vezes vai direto ao jogo. */
public class LauncherActivity extends Activity {
    static final String SHA1 = "5fd76a5f80e4684b5f9d445ddb893679985684e6";
    private static final int PICK_ROM = 1;
    private TextView status;

    static File romFile(Context c) {
        return new File(c.getFilesDir(), "Pulseman_Japan.md");
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (romFile(this).isFile()) {
            startGame();
            return;
        }
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setBackgroundColor(Color.rgb(12, 16, 32));
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("PULSEMAN");
        title.setTextColor(Color.rgb(255, 79, 166));
        title.setTextSize(32);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        box.addView(title);

        TextView info = new TextView(this);
        info.setText("Este app não inclui nenhum arquivo do jogo original.\n"
                + "Escolha a ROM original do Pulseman (versão japonesa, sem modificações),\n"
                + "obtida a partir de um cartucho que você possua.");
        info.setTextColor(Color.rgb(200, 210, 230));
        info.setTextSize(15);
        info.setGravity(Gravity.CENTER);
        info.setPadding(0, pad / 2, 0, pad / 2);
        box.addView(info);

        Button pick = new Button(this);
        pick.setText("Escolher a ROM");
        pick.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                startActivityForResult(i, PICK_ROM);
            }
        });
        box.addView(pick);

        status = new TextView(this);
        status.setTextColor(Color.rgb(255, 110, 110));
        status.setTextSize(14);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, pad / 2, 0, 0);
        box.addView(status);

        setContentView(box);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null)
            return;
        try {
            byte[] rom = findRom(data.getData());
            if (rom == null) {
                status.setText("Esta não é a ROM esperada (Pulseman japonês, SHA-1 " + SHA1 + ").");
                return;
            }
            FileOutputStream out = new FileOutputStream(romFile(this));
            out.write(rom);
            out.close();
            startGame();
        } catch (Exception e) {
            status.setText("Não foi possível ler o arquivo: " + e.getMessage());
        }
    }

    /* o arquivo escolhido, ou o arquivo certo de dentro dele se for um .zip */
    private byte[] findRom(Uri uri) throws Exception {
        byte[] all = readAll(getContentResolver().openInputStream(uri));
        if (SHA1.equals(sha1(all)))
            return all;
        if (all.length > 4 && all[0] == 'P' && all[1] == 'K') {
            ZipInputStream zip = new ZipInputStream(new java.io.ByteArrayInputStream(all));
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                if (e.isDirectory())
                    continue;
                byte[] b = readAll(zip);
                if (SHA1.equals(sha1(b)))
                    return b;
            }
        }
        return null;
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0)
            out.write(buf, 0, n);
        return out.toByteArray();
    }

    private static String sha1(byte[] b) throws Exception {
        byte[] d = MessageDigest.getInstance("SHA-1").digest(b);
        StringBuilder s = new StringBuilder();
        for (byte x : d)
            s.append(String.format("%02x", x & 0xFF));
        return s.toString();
    }

    private void startGame() {
        startActivity(new Intent(this, PulsemanActivity.class));
        finish();
    }
}
