package racing.fatal.roller;

import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class RollerActivity extends SDLActivity {
    private static final String TAG = "RollerActivity";
    private static final int MIDI_ASSET_VERSION = 1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        syncMidiAssets();
        super.onCreate(savedInstanceState);
        enterFullscreen();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main",
        };
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enterFullscreen();
        }
    }

    private void enterFullscreen() {
        Window window = getWindow();
        window.setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams attrs = window.getAttributes();
            attrs.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(attrs);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private void syncMidiAssets() {
        File externalFilesDir = getExternalFilesDir(null);
        if (externalFilesDir == null) {
            Log.w(TAG, "External files dir unavailable; MIDI assets were not copied.");
            return;
        }

        File midiDir = new File(externalFilesDir, "midi");
        File marker = new File(midiDir, ".roller-midi-assets-version");
        String expectedVersion = "midi:" + MIDI_ASSET_VERSION;

        if (marker.isFile() && expectedVersion.equals(readMarker(marker))) {
            return;
        }

        try {
            copyAssetTree(getAssets(), "midi", midiDir);
            writeMarker(marker, expectedVersion);
            Log.i(TAG, "MIDI assets synced to " + midiDir.getAbsolutePath());
        } catch (IOException e) {
            Log.w(TAG, "Failed to sync MIDI assets", e);
        }
    }

    private static String readMarker(File marker) {
        try (InputStream in = java.nio.file.Files.newInputStream(marker.toPath())) {
            byte[] data = new byte[(int)marker.length()];
            int read = in.read(data);
            if (read <= 0) {
                return "";
            }
            return new String(data, 0, read, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeMarker(File marker, String value) throws IOException {
        File parent = marker.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }

        try (FileOutputStream out = new FileOutputStream(marker, false)) {
            out.write(value.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File outPath)
            throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!outPath.isDirectory() && !outPath.mkdirs()) {
                throw new IOException("Could not create " + outPath);
            }

            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, new File(outPath, child));
            }
            return;
        }

        File parent = outPath.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }

        try (InputStream in = assets.open(assetPath);
             FileOutputStream out = new FileOutputStream(outPath, false)) {
            byte[] buffer = new byte[64 * 1024];
            int bytesRead;
            while ((bytesRead = in.read(buffer)) != -1) {
                out.write(buffer, 0, bytesRead);
            }
        }
    }
}
