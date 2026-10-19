package org.xberecomp;

import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.widget.Toast;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        startService(new android.content.Intent(this, GameSessionService.class));
    }

    @Override protected void onStop() {
        super.onStop();
        // A new foreground session must boot from clean native state. SDL and
        // the recompiled guest cannot safely reuse a lost Vulkan surface.
        if (!isChangingConfigurations()) finishAndRemoveTask();
    }

    @Override protected void onDestroy() {
        try {
            stopService(new android.content.Intent(this, GameSessionService.class));
            super.onDestroy();
        } finally {
            // This Activity owns a dedicated process. Ending it releases every
            // guest thread, GPU allocation, audio stream and open document.
            android.util.Log.i("XbeSession", "Releasing game process " + android.os.Process.myPid());
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
    @Override protected void main() {
        String uri = getSharedPreferences("game", MODE_PRIVATE).getString("uri", "");
        String kind = getSharedPreferences("game", MODE_PRIVATE).getString("kind", "iso");
        if (!uri.isEmpty() && kind.equals("folder")) {
            try {
                new NativeFiles(this, Uri.parse(uri)).mount(getSharedPreferences("game", MODE_PRIVATE).getString("name", "Game folder"));
                super.main();
            } catch (Exception error) {
                runOnUiThread(() -> Toast.makeText(this, "Cannot read game folder: " + error.getMessage(), Toast.LENGTH_LONG).show());
            }
            return;
        }
        if (uri.isEmpty()) {
            nativeSetenv("XBE_DISC_FD", "");
            nativeSetenv("XBE_DISC_IMAGE", "");
            super.main();
            return;
        }
        // Keep the document grant open while native code runs. The native disc
        // reader duplicates this descriptor and performs pread on the SD card.
        try (ParcelFileDescriptor image = getContentResolver().openFileDescriptor(Uri.parse(uri), "r")) {
            if (image == null) throw new java.io.IOException("Cannot open the selected game image");
            nativeSetenv("XBE_DISC_FD", Integer.toString(image.getFd()));
            nativeSetenv("XBE_DISC_IMAGE", getSharedPreferences("game", MODE_PRIVATE).getString("name", "Selected SD card image"));
            super.main();
        } catch (Exception error) {
            runOnUiThread(() -> Toast.makeText(this, "Choose your game image again: " + error.getMessage(), Toast.LENGTH_LONG).show());
        } finally {
            nativeSetenv("XBE_DISC_FD", "");
            nativeSetenv("XBE_DISC_IMAGE", "");
        }
    }
}
