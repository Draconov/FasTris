package com.draconov.fastris;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Bundle;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.List;

public class FasTrisActivity extends SDLActivity {
    private static native void nativeTouchInput(int code, boolean down);
    private static native int nativeScreen();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final View decor = getWindow().getDecorView();
        decor.post(new Runnable() {
            @Override
            public void run() {
                installAdaptiveLayout();
            }
        });
    }

    private void installAdaptiveLayout() {
        View contentView = findViewById(android.R.id.content);
        if (!(contentView instanceof ViewGroup)) return;
        ViewGroup content = (ViewGroup) contentView;
        if (content.getChildCount() == 0) {
            content.postDelayed(new Runnable() {
                @Override
                public void run() {
                    installAdaptiveLayout();
                }
            }, 50);
            return;
        }
        if (content.getChildAt(0) instanceof AdaptiveRoot) return;

        View sdlRoot = content.getChildAt(0);
        content.removeView(sdlRoot);
        AdaptiveRoot adaptive = new AdaptiveRoot(this, sdlRoot);
        content.addView(adaptive, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private static final class AdaptiveRoot extends ViewGroup {
        private final View gameView;
        private final ControlOverlay controls;

        AdaptiveRoot(Context context, View gameView) {
            super(context);
            this.gameView = gameView;
            this.controls = new ControlOverlay(context);
            addView(gameView);
            addView(controls);
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            int width = MeasureSpec.getSize(widthMeasureSpec);
            int height = MeasureSpec.getSize(heightMeasureSpec);
            setMeasuredDimension(width, height);

            boolean portrait = height >= width;
            int gameWidth;
            int gameHeight;
            if (portrait) {
                gameWidth = width;
                gameHeight = Math.max(1, Math.round(height * 0.70f));
            } else {
                int side = Math.round(width * 0.18f);
                int maxSide = Math.round(width * 0.22f);
                int minSide = Math.min(Math.round(width * 0.12f), dp(150));
                side = Math.max(minSide, Math.min(maxSide, side));
                gameWidth = Math.max(1, width - side * 2);
                gameHeight = height;
            }

            gameView.measure(
                    MeasureSpec.makeMeasureSpec(gameWidth, MeasureSpec.EXACTLY),
                    MeasureSpec.makeMeasureSpec(gameHeight, MeasureSpec.EXACTLY));
            controls.measure(
                    MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
                    MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
        }

        @Override
        protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
            int width = right - left;
            int height = bottom - top;
            boolean portrait = height >= width;
            int gameLeft = 0;
            int gameTop = 0;
            int gameRight = width;
            int gameBottom;

            if (portrait) {
                gameBottom = gameView.getMeasuredHeight();
            } else {
                int side = (width - gameView.getMeasuredWidth()) / 2;
                gameLeft = side;
                gameRight = width - side;
                gameBottom = height;
            }

            gameView.layout(gameLeft, gameTop, gameRight, gameBottom);
            controls.layout(0, 0, width, height);
            controls.setGameRect(gameLeft, gameTop, gameRight, gameBottom);
        }

        private int dp(int value) {
            return Math.round(value * getResources().getDisplayMetrics().density);
        }
    }

    private static final class ControlOverlay extends View {
        private static final int SCREEN_GAME = 1;

        private static final int LEFT = 0;
        private static final int RIGHT = 1;
        private static final int DOWN = 2;
        private static final int UP = 3;
        private static final int ROTATE_CW = 4;
        private static final int ROTATE_CCW = 5;
        private static final int ROTATE_180 = 6;
        private static final int HARD_DROP = 7;
        private static final int HOLD = 8;
        private static final int PAUSE = 9;
        private static final int CONFIRM = 10;
        private static final int BACK = 11;

        private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint areaPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final List<Zone> zones = new ArrayList<>();
        private final SparseIntArray pointerCodes = new SparseIntArray();
        private final RectF gameRect = new RectF();
        private int lastScreen = -1;
        private int lastWidth = -1;
        private int lastHeight = -1;

        ControlOverlay(Context context) {
            super(context);
            setFocusable(false);
            setClickable(true);
            fillPaint.setStyle(Paint.Style.FILL);
            fillPaint.setColor(Color.argb(68, 90, 210, 235));
            borderPaint.setStyle(Paint.Style.STROKE);
            borderPaint.setStrokeWidth(dp(2));
            borderPaint.setColor(Color.argb(190, 135, 225, 245));
            textPaint.setColor(Color.argb(240, 225, 245, 250));
            textPaint.setTextAlign(Paint.Align.CENTER);
            textPaint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            areaPaint.setColor(Color.rgb(8, 12, 18));
        }

        void setGameRect(int left, int top, int right, int bottom) {
            if (gameRect.left != left || gameRect.top != top || gameRect.right != right || gameRect.bottom != bottom) {
                gameRect.set(left, top, right, bottom);
                lastWidth = -1;
                invalidate();
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            int screen = safeNativeScreen();
            ensureZones(screen);

            if (getHeight() >= getWidth()) {
                canvas.drawRect(0, gameRect.bottom, getWidth(), getHeight(), areaPaint);
            } else {
                canvas.drawRect(0, 0, gameRect.left, getHeight(), areaPaint);
                canvas.drawRect(gameRect.right, 0, getWidth(), getHeight(), areaPaint);
            }

            float radius = dp(12);
            for (Zone zone : zones) {
                canvas.drawRoundRect(zone.rect, radius, radius, fillPaint);
                canvas.drawRoundRect(zone.rect, radius, radius, borderPaint);
                textPaint.setTextSize(Math.max(dp(12), Math.min(zone.rect.height() * 0.28f, dp(22))));
                Paint.FontMetrics metrics = textPaint.getFontMetrics();
                float baseline = zone.rect.centerY() - (metrics.ascent + metrics.descent) * 0.5f;
                canvas.drawText(zone.label, zone.rect.centerX(), baseline, textPaint);
            }
            postInvalidateDelayed(80);
        }

        private int safeNativeScreen() {
            try {
                return nativeScreen();
            } catch (UnsatisfiedLinkError ignored) {
                return 0;
            }
        }

        private void ensureZones(int screen) {
            int width = getWidth();
            int height = getHeight();
            if (screen == lastScreen && width == lastWidth && height == lastHeight) return;
            lastScreen = screen;
            lastWidth = width;
            lastHeight = height;
            zones.clear();
            if (screen == SCREEN_GAME) buildGameZones(width, height);
            else buildMenuZones(width, height);
        }

        private void buildGameZones(int width, int height) {
            if (height >= width) {
                float top = gameRect.bottom;
                float controlHeight = Math.max(1.0f, height - top);

                addZone(0.05f * width, top + 0.45f * controlHeight, 0.24f * width, top + 0.72f * controlHeight, "LEFT", LEFT);
                addZone(0.27f * width, top + 0.63f * controlHeight, 0.46f * width, top + 0.91f * controlHeight, "DOWN", DOWN);
                addZone(0.49f * width, top + 0.45f * controlHeight, 0.68f * width, top + 0.72f * controlHeight, "RIGHT", RIGHT);

                addZone(0.71f * width, top + 0.35f * controlHeight, 0.84f * width, top + 0.58f * controlHeight, "CCW", ROTATE_CCW);
                addZone(0.85f * width, top + 0.35f * controlHeight, 0.98f * width, top + 0.58f * controlHeight, "CW", ROTATE_CW);
                addZone(0.71f * width, top + 0.61f * controlHeight, 0.84f * width, top + 0.84f * controlHeight, "180", ROTATE_180);
                addZone(0.85f * width, top + 0.61f * controlHeight, 0.98f * width, top + 0.91f * controlHeight, "DROP", HARD_DROP);

                addZone(0.04f * width, top + 0.08f * controlHeight, 0.22f * width, top + 0.32f * controlHeight, "HOLD", HOLD);
                addZone(0.39f * width, top + 0.08f * controlHeight, 0.57f * width, top + 0.32f * controlHeight, "PAUSE", PAUSE);
                addZone(0.61f * width, top + 0.08f * controlHeight, 0.79f * width, top + 0.32f * controlHeight, "MENU", BACK);
            } else {
                float leftWidth = gameRect.left;
                float rightStart = gameRect.right;
                float rightWidth = width - rightStart;

                addZone(0.07f * leftWidth, 0.46f * height, 0.47f * leftWidth, 0.69f * height, "LEFT", LEFT);
                addZone(0.53f * leftWidth, 0.46f * height, 0.93f * leftWidth, 0.69f * height, "RIGHT", RIGHT);
                addZone(0.30f * leftWidth, 0.72f * height, 0.70f * leftWidth, 0.95f * height, "DOWN", DOWN);
                addZone(0.18f * leftWidth, 0.12f * height, 0.82f * leftWidth, 0.34f * height, "HOLD", HOLD);

                addZone(rightStart + 0.06f * rightWidth, 0.46f * height, rightStart + 0.47f * rightWidth, 0.68f * height, "CCW", ROTATE_CCW);
                addZone(rightStart + 0.53f * rightWidth, 0.46f * height, rightStart + 0.94f * rightWidth, 0.68f * height, "CW", ROTATE_CW);
                addZone(rightStart + 0.06f * rightWidth, 0.72f * height, rightStart + 0.47f * rightWidth, 0.94f * height, "180", ROTATE_180);
                addZone(rightStart + 0.53f * rightWidth, 0.72f * height, rightStart + 0.94f * rightWidth, 0.94f * height, "DROP", HARD_DROP);
                addZone(rightStart + 0.06f * rightWidth, 0.10f * height, rightStart + 0.47f * rightWidth, 0.31f * height, "PAUSE", PAUSE);
                addZone(rightStart + 0.53f * rightWidth, 0.10f * height, rightStart + 0.94f * rightWidth, 0.31f * height, "MENU", BACK);
            }
        }

        private void buildMenuZones(int width, int height) {
            if (height >= width) {
                float top = gameRect.bottom;
                float controlHeight = Math.max(1.0f, height - top);
                addZone(0.16f * width, top + 0.08f * controlHeight, 0.36f * width, top + 0.36f * controlHeight, "UP", UP);
                addZone(0.04f * width, top + 0.38f * controlHeight, 0.24f * width, top + 0.68f * controlHeight, "LEFT", LEFT);
                addZone(0.28f * width, top + 0.38f * controlHeight, 0.48f * width, top + 0.68f * controlHeight, "RIGHT", RIGHT);
                addZone(0.16f * width, top + 0.70f * controlHeight, 0.36f * width, top + 0.97f * controlHeight, "DOWN", DOWN);
                addZone(0.59f * width, top + 0.24f * controlHeight, 0.79f * width, top + 0.61f * controlHeight, "OK", CONFIRM);
                addZone(0.80f * width, top + 0.62f * controlHeight, 0.98f * width, top + 0.94f * controlHeight, "BACK", BACK);
            } else {
                float leftWidth = gameRect.left;
                float rightStart = gameRect.right;
                float rightWidth = width - rightStart;
                addZone(0.28f * leftWidth, 0.08f * height, 0.72f * leftWidth, 0.31f * height, "UP", UP);
                addZone(0.04f * leftWidth, 0.36f * height, 0.47f * leftWidth, 0.61f * height, "LEFT", LEFT);
                addZone(0.53f * leftWidth, 0.36f * height, 0.96f * leftWidth, 0.61f * height, "RIGHT", RIGHT);
                addZone(0.28f * leftWidth, 0.67f * height, 0.72f * leftWidth, 0.92f * height, "DOWN", DOWN);
                addZone(rightStart + 0.12f * rightWidth, 0.24f * height, rightStart + 0.88f * rightWidth, 0.55f * height, "OK", CONFIRM);
                addZone(rightStart + 0.12f * rightWidth, 0.63f * height, rightStart + 0.88f * rightWidth, 0.91f * height, "BACK", BACK);
            }
        }

        private void addZone(float left, float top, float right, float bottom, String label, int code) {
            zones.add(new Zone(new RectF(left, top, right, bottom), label, code));
        }

        private int hit(float x, float y) {
            for (int i = zones.size() - 1; i >= 0; --i) {
                if (zones.get(i).rect.contains(x, y)) return zones.get(i).code;
            }
            return -1;
        }

        private void press(int pointerId, int code) {
            if (code < 0) return;
            int old = pointerCodes.get(pointerId, -1);
            if (old == code) return;
            if (old >= 0) send(old, false);
            pointerCodes.put(pointerId, code);
            send(code, true);
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        }

        private void release(int pointerId) {
            int code = pointerCodes.get(pointerId, -1);
            if (code >= 0) send(code, false);
            pointerCodes.delete(pointerId);
        }

        private void send(int code, boolean down) {
            try {
                nativeTouchInput(code, down);
            } catch (UnsatisfiedLinkError ignored) {
            }
        }

        private void releaseAll() {
            for (int i = 0; i < pointerCodes.size(); ++i) send(pointerCodes.valueAt(i), false);
            pointerCodes.clear();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            ensureZones(safeNativeScreen());
            int action = event.getActionMasked();
            int index = event.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                int pointerId = event.getPointerId(index);
                press(pointerId, hit(event.getX(index), event.getY(index)));
            } else if (action == MotionEvent.ACTION_MOVE) {
                for (int i = 0; i < event.getPointerCount(); ++i) {
                    int pointerId = event.getPointerId(i);
                    int next = hit(event.getX(i), event.getY(i));
                    int old = pointerCodes.get(pointerId, -1);
                    if (next != old) {
                        release(pointerId);
                        press(pointerId, next);
                    }
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                release(event.getPointerId(index));
            } else if (action == MotionEvent.ACTION_CANCEL) {
                releaseAll();
            }
            return true;
        }

        @Override
        protected void onDetachedFromWindow() {
            releaseAll();
            super.onDetachedFromWindow();
        }

        private float dp(float value) {
            return value * getResources().getDisplayMetrics().density;
        }

        private static final class Zone {
            final RectF rect;
            final String label;
            final int code;

            Zone(RectF rect, String label, int code) {
                this.rect = rect;
                this.label = label;
                this.code = code;
            }
        }
    }
}
