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
                gameHeight = Math.max(1, Math.round(height * 0.76f));
            } else {
                int side = Math.round(width * 0.16f);
                int maxSide = Math.round(width * 0.19f);
                int minSide = Math.min(Math.round(width * 0.11f), dp(132));
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
                float textSize = Math.max(dp(12), Math.min(zone.rect.height() * 0.30f, dp(24)));
                textPaint.setTextSize(textSize);
                float maxWidth = zone.rect.width() * 0.78f;
                while (textSize > dp(11) && textPaint.measureText(zone.label) > maxWidth) {
                    textSize -= dp(1);
                    textPaint.setTextSize(textSize);
                }
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
                float margin = 0.025f * width;
                float gap = 0.018f * width;

                float row1Top = top + 0.06f * controlHeight;
                float row1Bottom = top + 0.24f * controlHeight;
                float utilityW = (width - margin * 2.0f - gap * 2.0f) / 3.0f;
                addZone(margin, row1Top, margin + utilityW, row1Bottom, "HOLD", HOLD);
                addZone(margin + utilityW + gap, row1Top, margin + utilityW * 2.0f + gap, row1Bottom, "II", PAUSE);
                addZone(margin + utilityW * 2.0f + gap * 2.0f, row1Top, width - margin, row1Bottom, "MENU", BACK);

                float moveTop = top + 0.33f * controlHeight;
                float moveBottom = top + 0.94f * controlHeight;
                float leftAreaRight = width * 0.56f;
                float rightAreaLeft = width * 0.60f;

                float mW = leftAreaRight - margin;
                float leftW = mW * 0.34f;
                float centerW = mW * 0.28f;
                addZone(margin, moveTop + 0.18f * (moveBottom - moveTop), margin + leftW, moveTop + 0.62f * (moveBottom - moveTop), "<", LEFT);
                addZone(margin + leftW + gap, moveTop + 0.38f * (moveBottom - moveTop), margin + leftW + gap + centerW, moveBottom, "V", DOWN);
                addZone(leftAreaRight - leftW, moveTop + 0.18f * (moveBottom - moveTop), leftAreaRight, moveTop + 0.62f * (moveBottom - moveTop), ">", RIGHT);

                float rW = width - margin - rightAreaLeft;
                float colW = (rW - gap) / 2.0f;
                float rTop1 = moveTop;
                float rBot1 = moveTop + 0.34f * (moveBottom - moveTop);
                float rTop2 = moveTop + 0.42f * (moveBottom - moveTop);
                float rBot2 = moveBottom;
                addZone(rightAreaLeft, rTop1, rightAreaLeft + colW, rBot1, "CCW", ROTATE_CCW);
                addZone(rightAreaLeft + colW + gap, rTop1, width - margin, rBot1, "CW", ROTATE_CW);
                addZone(rightAreaLeft, rTop2, rightAreaLeft + colW, rBot2, "180", ROTATE_180);
                addZone(rightAreaLeft + colW + gap, rTop2, width - margin, rBot2, "HD", HARD_DROP);
            } else {
                float leftWidth = gameRect.left;
                float rightStart = gameRect.right;
                float rightWidth = width - rightStart;
                float marginL = leftWidth * 0.08f;
                float marginR = rightWidth * 0.08f;
                float gapL = leftWidth * 0.06f;
                float gapR = rightWidth * 0.06f;

                addZone(marginL, 0.10f * height, leftWidth - marginL, 0.26f * height, "HOLD", HOLD);
                float leftHalfW = (leftWidth - marginL * 2.0f - gapL) / 2.0f;
                addZone(marginL, 0.46f * height, marginL + leftHalfW, 0.66f * height, "<", LEFT);
                addZone(marginL + leftHalfW + gapL, 0.46f * height, leftWidth - marginL, 0.66f * height, ">", RIGHT);
                addZone(marginL + leftWidth * 0.18f, 0.74f * height, leftWidth - marginL - leftWidth * 0.18f, 0.94f * height, "V", DOWN);

                float topRowTop = 0.08f * height;
                float topRowBottom = 0.24f * height;
                float topW = (rightWidth - marginR * 2.0f - gapR) / 2.0f;
                addZone(rightStart + marginR, topRowTop, rightStart + marginR + topW, topRowBottom, "II", PAUSE);
                addZone(rightStart + marginR + topW + gapR, topRowTop, width - marginR, topRowBottom, "MENU", BACK);

                float cellW = (rightWidth - marginR * 2.0f - gapR) / 2.0f;
                addZone(rightStart + marginR, 0.42f * height, rightStart + marginR + cellW, 0.60f * height, "CCW", ROTATE_CCW);
                addZone(rightStart + marginR + cellW + gapR, 0.42f * height, width - marginR, 0.60f * height, "CW", ROTATE_CW);
                addZone(rightStart + marginR, 0.68f * height, rightStart + marginR + cellW, 0.86f * height, "180", ROTATE_180);
                addZone(rightStart + marginR + cellW + gapR, 0.68f * height, width - marginR, 0.86f * height, "HD", HARD_DROP);
            }
        }

        private void buildMenuZones(int width, int height) {
            if (height >= width) {
                float top = gameRect.bottom;
                float controlHeight = Math.max(1.0f, height - top);
                float left = width * 0.04f;
                float gap = width * 0.03f;
                float right = width * 0.96f;
                float dpadRight = width * 0.46f;
                addZone(left + width * 0.13f, top + 0.06f * controlHeight, left + width * 0.33f, top + 0.25f * controlHeight, "^", UP);
                addZone(left, top + 0.31f * controlHeight, left + width * 0.20f, top + 0.53f * controlHeight, "<", LEFT);
                addZone(left + width * 0.26f, top + 0.31f * controlHeight, dpadRight, top + 0.53f * controlHeight, ">", RIGHT);
                addZone(left + width * 0.13f, top + 0.60f * controlHeight, left + width * 0.33f, top + 0.82f * controlHeight, "V", DOWN);
                addZone(width * 0.58f, top + 0.16f * controlHeight, width * 0.83f, top + 0.46f * controlHeight, "OK", CONFIRM);
                addZone(width * 0.67f, top + 0.56f * controlHeight, right, top + 0.83f * controlHeight, "BACK", BACK);
            } else {
                float leftWidth = gameRect.left;
                float rightStart = gameRect.right;
                float rightWidth = width - rightStart;
                float marginL = leftWidth * 0.08f;
                float gapL = leftWidth * 0.06f;
                addZone(marginL + leftWidth * 0.18f, 0.10f * height, leftWidth - marginL - leftWidth * 0.18f, 0.27f * height, "^", UP);
                float halfW = (leftWidth - marginL * 2.0f - gapL) / 2.0f;
                addZone(marginL, 0.38f * height, marginL + halfW, 0.56f * height, "<", LEFT);
                addZone(marginL + halfW + gapL, 0.38f * height, leftWidth - marginL, 0.56f * height, ">", RIGHT);
                addZone(marginL + leftWidth * 0.18f, 0.68f * height, leftWidth - marginL - leftWidth * 0.18f, 0.86f * height, "V", DOWN);
                addZone(rightStart + rightWidth * 0.12f, 0.24f * height, rightStart + rightWidth * 0.88f, 0.50f * height, "OK", CONFIRM);
                addZone(rightStart + rightWidth * 0.12f, 0.62f * height, rightStart + rightWidth * 0.88f, 0.84f * height, "BACK", BACK);
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
