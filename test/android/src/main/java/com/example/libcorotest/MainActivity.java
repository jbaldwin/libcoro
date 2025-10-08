package com.example.libcorotest;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Color;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

public class MainActivity extends Activity {
    static { System.loadLibrary("coroTest"); }

    private TextView textView;
    private TextView statusView;
    private ProgressBar progress;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

    // Root vertical layout
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);

    // Status row
    statusView = new TextView(this);
    statusView.setTextColor(Color.BLACK);
    int pad = (int) (8 * getResources().getDisplayMetrics().density);
    statusView.setPadding(pad, pad, pad, pad);
    statusView.setText("Running…");

    progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    progress.setIndeterminate(true);
    progress.setPadding(pad, 0, pad, pad);

    // Log scroller
    ScrollView sv = new ScrollView(this);
    textView = new TextView(this);
        textView.setPadding(pad, pad, pad, pad);
    sv.addView(textView, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

    // Compose
    root.addView(statusView, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    root.addView(progress, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    root.addView(sv, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    setContentView(root);

        // Run tests on a worker thread; when finished, set result for CI and close.
        new Thread(() -> {
            int rc = runTests(getFilesDir().getAbsolutePath());
            ui.post(() -> {
                appendLine("Finished with code=" + rc);
                // Update status
                statusView.setText(rc == 0 ? "Done: PASS" : "Done: FAIL (code=" + rc + ")");
                statusView.setTextColor(rc == 0 ? Color.parseColor("#2e7d32") : Color.parseColor("#c62828"));
                progress.setIndeterminate(false);
                progress.setVisibility(ProgressBar.GONE);
                setResult(rc == 0 ? RESULT_OK : 1234);
                // Keep UI open on real devices for inspection; auto-finish on emulator for CI.
                if (isRunningOnEmulator()) finish();
            });
        }, "tests").start();
    }

    // Called from native
    public void appendLine(String s) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            textView.append(s + "\n");
            // Auto-scroll to bottom
            ((ScrollView) textView.getParent()).post(() -> ((ScrollView) textView.getParent()).fullScroll(ScrollView.FOCUS_DOWN));
        } else {
            ui.post(() -> appendLine(s));
        }
    }

    public native int runTests(String filesDir);

    private boolean isRunningOnEmulator() {
        // heuristics based on Build fields
        final String fp = android.os.Build.FINGERPRINT;
        final String model = android.os.Build.MODEL;
        final String product = android.os.Build.PRODUCT;
        final String manufacturer = android.os.Build.MANUFACTURER;
        if (fp != null && (fp.startsWith("generic") || fp.contains("emulator") || fp.contains("sdk_gphone"))) return true;
        if (model != null && (model.contains("Android SDK built for") || model.contains("sdk_gphone"))) return true;
        if (product != null && (product.contains("sdk") || product.contains("emulator") || product.contains("sdk_gphone"))) return true;
        if (manufacturer != null && manufacturer.contains("Genymotion")) return true;
        return false;
    }
}
