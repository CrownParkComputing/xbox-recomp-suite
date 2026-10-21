package org.xberecomp;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;

public class LauncherActivity extends Activity {
    private static final int PICK_IMAGE = 1;
    private static final int PICK_FOLDER = 2;
    private TextView status;
    private Button start, choose;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!getIntent().getBooleanExtra("show_picker", false) && hasGame()) {
            launchGame();
            return;
        }
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        layout.setBackgroundColor(0xFF063A59);
        TextView title = new TextView(this);
        title.setText(getApplicationInfo().loadLabel(getPackageManager())); title.setTextSize(32); title.setTextColor(0xFFFFFFFF);
        layout.addView(title);
        TextView instructions = new TextView(this);
        instructions.setText("Connect your controller, then choose your game ISO or extracted game folder.\nGame files stay on your SD card. Your selection is remembered.");
        instructions.setGravity(Gravity.CENTER); instructions.setTextSize(17); instructions.setTextColor(0xFFDBEFF5);
        instructions.setPadding(0, padding/2, 0, padding/2); layout.addView(instructions);
        status = new TextView(this); status.setTextColor(0xFFFFFFFF); layout.addView(status);
        start = new Button(this); start.setText("Start game");
        start.setOnClickListener(v -> launchGame());
        layout.addView(start);
        choose = new Button(this); choose.setText("Choose game image");
        choose.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("*/*");
            startActivityForResult(intent, PICK_IMAGE);
        });
        layout.addView(choose);
        Button folder = new Button(this); folder.setText("Choose game folder");
        folder.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, PICK_FOLDER);
        });
        layout.addView(folder);
        if (PortConfig.folderOnly(this)) choose.setVisibility(android.view.View.GONE);
        setContentView(layout);
        updateStatus();
    }

    @Override protected void onResume() {
        super.onResume();
        if (status != null) updateStatus();
    }

    private void launchGame() {
        try {
            Class<?> game = Class.forName(getPackageName() + ".GameActivity");
            Intent intent = new Intent(this, game);
            startActivity(intent);
            finish();
        } catch (ClassNotFoundException error) {
            throw new IllegalStateException("Game activity is missing", error);
        }
    }

    private boolean hasGame() {
        String selected = getSharedPreferences("game", MODE_PRIVATE).getString("uri", "");
        if (!selected.isEmpty()) {
            if (getSharedPreferences("game", MODE_PRIVATE).getString("kind", "iso").equals("folder")) {
                try { return folderHasGame(Uri.parse(selected)); }
                catch (Exception unavailable) { return false; }
            }
            try (android.os.ParcelFileDescriptor file = getContentResolver().openFileDescriptor(Uri.parse(selected), "r")) {
                if (file == null) return false;
                android.system.Os.lseek(file.getFileDescriptor(), 0, android.system.OsConstants.SEEK_SET);
                return true;
            } catch (Exception unavailable) { return false; }
        }
        File[] data = assets().listFiles((dir, name) -> isGameFile(name));
        return data != null && data.length > 0;
    }

    private File assets() { return new File(getFilesDir(), "assets"); }

    private boolean isGameFile(String name) {
        return name.equalsIgnoreCase(PortConfig.executable(this)) ||
            (!PortConfig.folderOnly(this) && name.toLowerCase(java.util.Locale.ROOT).endsWith(".iso"));
    }

    private boolean folderHasGame(Uri tree) throws Exception {
        String id = DocumentsContract.getTreeDocumentId(tree);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        try (Cursor cursor = getContentResolver().query(children,
                new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor == null) return false;
            while (cursor.moveToNext()) {
                if (!PortConfig.executable(this).equalsIgnoreCase(cursor.getString(1))) continue;
                Uri xbe = DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
                try (android.os.ParcelFileDescriptor file = getContentResolver().openFileDescriptor(xbe, "r")) {
                    return file != null;
                }
            }
        }
        return false;
    }

    private void updateStatus() {
        File[] data = assets().listFiles((dir, name) -> isGameFile(name));
        boolean local = data != null && data.length > 0;
        String selected = getSharedPreferences("game", MODE_PRIVATE).getString("uri", "");
        String name = getSharedPreferences("game", MODE_PRIVATE).getString("name", "Selected game image");
        boolean ready = hasGame();
        status.setText(!selected.isEmpty() ? (ready ? name + " — opens directly" : "Saved game unavailable. Reconnect the SD card or choose the game again.") : local ? "Game data ready" : PortConfig.folderOnly(this) ? "Choose the game folder containing " + PortConfig.executable(this) : "Choose your game image or folder");
        start.setEnabled(ready);
        if (ready) start.requestFocus(); else choose.requestFocus();
    }

    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if ((request != PICK_IMAGE && request != PICK_FOLDER) || result != RESULT_OK || intent == null || intent.getData() == null) return;
        final Uri uri = intent.getData();
        if (request == PICK_FOLDER) {
            try {
                if (!folderHasGame(uri)) {
                    status.setText("Choose the extracted game folder containing " + PortConfig.executable(this) + "."); return;
                }
                String id = DocumentsContract.getTreeDocumentId(uri);
                remember(uri, id.substring(Math.max(id.lastIndexOf('/'), id.lastIndexOf(':')) + 1), "folder", intent);
                launchGame();
            } catch (Exception error) { status.setText("Cannot read this game folder: " + error.getMessage()); }
            return;
        }
        String name = "";
        try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (Exception error) { status.setText("Could not read this file: " + error.getMessage()); return; }
        if (name == null || !name.toLowerCase(java.util.Locale.ROOT).endsWith(".iso")) {
            status.setText("Choose an extracted .iso disc image."); return;
        }
        try (android.os.ParcelFileDescriptor file = getContentResolver().openFileDescriptor(uri, "r")) {
            if (file == null) throw new java.io.IOException("Cannot open the selected image");
            // The disc reader uses random access; cloud streams and pipes cannot serve a DVD.
            android.system.Os.lseek(file.getFileDescriptor(), 0, android.system.OsConstants.SEEK_SET);
            remember(uri, name, "iso", intent);
            launchGame();
        } catch (Exception error) {
            status.setText("Cannot open this image directly. Choose a local ISO on your SD card. " + error.getMessage());
        }
    }

    private void remember(Uri uri, String name, String kind, Intent result) throws Exception {
        int flags = result.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        getContentResolver().takePersistableUriPermission(uri, flags);
        String old = getSharedPreferences("game", MODE_PRIVATE).getString("uri", "");
        if (!getSharedPreferences("game", MODE_PRIVATE).edit().putString("uri", uri.toString())
                .putString("name", name).putString("kind", kind).commit())
            throw new java.io.IOException("Could not save the game location");
        if (!old.isEmpty() && !old.equals(uri.toString())) {
            try { getContentResolver().releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (SecurityException ignored) { }
        }
    }
}
