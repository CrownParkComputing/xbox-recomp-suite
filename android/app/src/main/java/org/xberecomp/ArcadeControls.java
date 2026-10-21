package org.xberecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.SparseIntArray;
import android.util.SparseArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import org.libsdl.app.SDLActivity;

/** Small arcade panel. Aim touches reach SDL; panel touches only press keys. */
final class ArcadeControls extends View {
    private static final int GAME = -1;
    private final String kind;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Control> buttons = new ArrayList<>();
    private final SparseIntArray owners = new SparseIntArray();
    private final SparseArray<float[]> fingers = new SparseArray<>();
    private int device;

    private static final class Control {
        final String label;
        final int key;
        final RectF rect;
        int held;
        Control(String label, int key, RectF rect) {
            this.label = label; this.key = key; this.rect = rect;
        }
    }

    ArcadeControls(Context context, String kind) {
        super(context);
        this.kind = kind;
        setFocusable(false);
        setContentDescription("Arcade controls");
    }

    private void add(String label, int key, float x, float y, float w, float h) {
        buttons.add(new Control(label, key, new RectF(x, y, x + w, y + h)));
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        releaseAll();
        buttons.clear();
        float u = Math.min(w / 13f, h / 6f), gap = u * .12f;
        float top = gap, bw = u * 1.45f, bh = u * .62f;
        if (!kind.equals("gun-vcop3")) add("Coin", KeyEvent.KEYCODE_DEL, gap, top, bw, bh);
        add("Start", KeyEvent.KEYCODE_ENTER,
            kind.equals("gun-vcop3") ? gap : gap * 2 + bw, top, bw, bh);
        add("Settings", KeyEvent.KEYCODE_F10, w - bw - gap, top, bw, bh);
        if (kind.startsWith("gun-")) {
            add("Reload", KeyEvent.KEYCODE_X, w - bw - gap, h - bh - gap, bw, bh);
            if (kind.equals("gun-vcop3"))
                add("Slow", KeyEvent.KEYCODE_A, w - bw * 2 - gap * 2, h - bh - gap, bw, bh);
        } else {
            float bottom = h - u - gap;
            add("Left", KeyEvent.KEYCODE_DPAD_LEFT, gap, bottom, u, u);
            add("Right", KeyEvent.KEYCODE_DPAD_RIGHT, gap * 2 + u, bottom, u, u);
            add("Brake", KeyEvent.KEYCODE_Q, w - u * 2 - gap * 2, bottom, u, u);
            add("Gas", KeyEvent.KEYCODE_W, w - u - gap, bottom, u, u);
        }
    }

    private int hit(float x, float y) {
        for (int i = 0; i < buttons.size(); ++i)
            if (buttons.get(i).rect.contains(x, y)) return i;
        return GAME;
    }

    private void key(int index, boolean down) {
        if (index < 0) return;
        Control button = buttons.get(index);
        if (down && button.held++ == 0) SDLActivity.onNativeKeyDown(button.key);
        if (!down && button.held > 0 && --button.held == 0) SDLActivity.onNativeKeyUp(button.key);
        invalidate();
    }

    private void touch(int id, int action, float x, float y, float pressure) {
        float nx = x / Math.max(1, getWidth() - 1), ny = y / Math.max(1, getHeight() - 1);
        SDLActivity.onNativeTouch(device, id, action, nx, ny, Math.min(1, pressure));
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE) {
            float[] position = fingers.get(id);
            if (position == null) fingers.put(id, new float[]{x, y});
            else { position[0] = x; position[1] = y; }
        } else fingers.remove(id);
    }

    void releaseAll() {
        for (Control button : buttons) {
            if (button.held > 0) SDLActivity.onNativeKeyUp(button.key);
            button.held = 0;
        }
        for (int i = 0; i < fingers.size(); ++i) {
            float[] pos = fingers.valueAt(i);
            SDLActivity.onNativeTouch(device, fingers.keyAt(i), MotionEvent.ACTION_CANCEL,
                pos[0] / Math.max(1, getWidth() - 1), pos[1] / Math.max(1, getHeight() - 1), 0);
        }
        fingers.clear(); owners.clear(); invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE) return false;
        device = event.getDeviceId();
        int action = event.getActionMasked(), at = event.getActionIndex();
        if (action == MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int id = event.getPointerId(at), owner = hit(event.getX(at), event.getY(at));
            owners.put(id, owner);
            if (owner >= 0) key(owner, true);
            else touch(id, MotionEvent.ACTION_DOWN, event.getX(at), event.getY(at), event.getPressure(at));
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                int id = event.getPointerId(i), owner = owners.get(id, -2);
                if (owner == GAME) touch(id, MotionEvent.ACTION_MOVE, event.getX(i), event.getY(i), event.getPressure(i));
                else if (owner >= 0 && !buttons.get(owner).rect.contains(event.getX(i), event.getY(i))) {
                    key(owner, false); owners.put(id, -2);
                }
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int id = event.getPointerId(at), owner = owners.get(id, -2);
            if (owner >= 0) key(owner, false);
            else if (owner == GAME) touch(id, MotionEvent.ACTION_UP, event.getX(at), event.getY(at), 0);
            owners.delete(id);
            performClick();
        }
        return true;
    }

    @Override public boolean performClick() { return super.performClick(); }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        for (Control button : buttons) {
            paint.setColor(button.held > 0 ? 0xCC397BC1 : 0x99202938);
            canvas.drawRoundRect(button.rect, 12, 12, paint);
            paint.setColor(Color.WHITE); paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(button.rect.height() * .28f);
            canvas.drawText(button.label, button.rect.centerX(),
                button.rect.centerY() - (paint.ascent() + paint.descent()) / 2, paint);
        }
    }
}
