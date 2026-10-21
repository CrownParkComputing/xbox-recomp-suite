package org.xberecomp;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;

/** Per-title media and control choices supplied by the APK manifest. */
final class PortConfig {
    private static Bundle cached;
    private static synchronized Bundle metadata(Context context) {
        if (cached != null) return cached;
        try {
            Bundle values = context.getPackageManager().getApplicationInfo(
                context.getPackageName(), PackageManager.GET_META_DATA).metaData;
            cached = values == null ? Bundle.EMPTY : values;
            return cached;
        } catch (PackageManager.NameNotFoundException error) {
            throw new IllegalStateException("Application metadata unavailable", error);
        }
    }
    static String executable(Context context) {
        return metadata(context).getString("org.xberecomp.GAME_XBE", "default.xbe");
    }
    static boolean folderOnly(Context context) {
        return metadata(context).getBoolean("org.xberecomp.FOLDER_ONLY", false);
    }
    static String controls(Context context) {
        return metadata(context).getString("org.xberecomp.CONTROLS", "");
    }
}
