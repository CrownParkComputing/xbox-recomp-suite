package org.xberecomp;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.IOException;
import java.util.ArrayList;

/** Read game files through the persisted Android document grant, in place. */
public final class NativeFiles {
    private final ContentResolver resolver;
    private final Uri tree;

    NativeFiles(Context context, Uri tree) {
        this.resolver = context.getApplicationContext().getContentResolver();
        this.tree = tree;
    }

    public String[] children(String documentId) throws IOException {
        ArrayList<String> entries = new ArrayList<>();
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_MIME_TYPE};
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot read the game folder");
            while (cursor.moveToNext()) {
                entries.add(cursor.getString(0));
                entries.add(cursor.getString(1));
                entries.add(cursor.isNull(2) ? "0" : cursor.getString(2));
                entries.add(DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(3)) ? "d" : "f");
            }
        }
        return entries.toArray(new String[0]);
    }

    public int open(String documentId) throws IOException {
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
        try (ParcelFileDescriptor file = resolver.openFileDescriptor(document, "r")) {
            if (file == null) throw new IOException("Cannot open a game file");
            return file.detachFd(); // Ownership passes to the native reader.
        }
    }

    void mount(String name) throws IOException {
        String error = mountTree(DocumentsContract.getTreeDocumentId(tree), name);
        if (error != null) throw new IOException(error);
    }

    private native String mountTree(String rootId, String name);
}
