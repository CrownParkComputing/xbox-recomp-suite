package org.xberecomp;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Process;

// Shares only the native game's :game process, never the launcher process.
public final class GameSessionService extends Service {
    @Override public int onStartCommand(Intent intent, int flags, int id) {
        return START_NOT_STICKY;
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public void onTaskRemoved(Intent root) {
        stopSelf();
        Process.killProcess(Process.myPid());
    }
}
