package racing.fatal.roller;

import android.app.Dialog;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.text.InputFilter;
import android.text.InputType;
import android.text.Spanned;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class RollerActivity extends SDLActivity {
    private static final String TAG = "RollerActivity";
    private static final int MIDI_ASSET_VERSION = 1;
    private static final int NAME_ENTRY_TARGET_CONFIG = 1;
    private static final int NAME_ENTRY_TARGET_REPLAY = 2;
    private Dialog nameEntryDialog;
    private int nameEntryDialogTarget;

    private static native void nativeNameEntryComplete(String value, boolean accepted);
    private static native void nativeReplayNameEntryComplete(String value, boolean accepted);

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

    public void showNameEntryDialog(String currentName) {
        runOnUiThread(() -> showNameEntryDialogOnUiThread(
                "ENTER NAME", currentName, NAME_ENTRY_TARGET_CONFIG));
    }

    public void showReplayNameEntryDialog(String currentName) {
        runOnUiThread(() -> showNameEntryDialogOnUiThread(
                "SAVE REPLAY", currentName, NAME_ENTRY_TARGET_REPLAY));
    }

    private void showNameEntryDialogOnUiThread(String titleText, String currentName,
            int target) {
        if (nameEntryDialog != null) {
            nameEntryDialog.dismiss();
            nameEntryDialog = null;
        }

        Dialog dialog = new Dialog(this);
        nameEntryDialog = dialog;
        nameEntryDialogTarget = target;
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setCanceledOnTouchOutside(false);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(dp(32), dp(32), dp(32), dp(32));
        root.setBackgroundColor(Color.rgb(8, 8, 8));

        TextView title = new TextView(this);
        title.setText(titleText);
        title.setTextColor(Color.WHITE);
        title.setTextSize(20.0f);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        EditText edit = new EditText(this);
        boolean allowSpaces = target == NAME_ENTRY_TARGET_CONFIG;
        edit.setSingleLine(true);
        edit.setGravity(Gravity.CENTER);
        edit.setTextColor(Color.WHITE);
        edit.setTextSize(34.0f);
        edit.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        edit.setSelectAllOnFocus(false);
        edit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        edit.setImeOptions(EditorInfo.IME_ACTION_DONE);
        edit.setFilters(new InputFilter[] {
                new NameInputFilter(allowSpaces),
                new InputFilter.LengthFilter(8),
        });
        edit.setText(sanitizeName(currentName, allowSpaces));
        edit.setSelection(edit.getText().length());

        LinearLayout.LayoutParams editParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        editParams.setMargins(0, dp(24), 0, dp(24));
        root.addView(edit, editParams);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);

        Button cancelButton = new Button(this);
        cancelButton.setText("CANCEL");
        cancelButton.setOnClickListener(v -> finishNameEntry(dialog, "", false));
        buttons.addView(cancelButton, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

        Button okButton = new Button(this);
        okButton.setText("OK");
        okButton.setOnClickListener(v -> finishNameEntry(dialog, edit.getText().toString(), true));
        LinearLayout.LayoutParams okParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        okParams.setMargins(dp(12), 0, 0, 0);
        buttons.addView(okButton, okParams);

        root.addView(buttons, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        edit.setOnEditorActionListener((v, actionId, event) -> {
            boolean enterReleased = event != null
                    && event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                    && event.getAction() == KeyEvent.ACTION_UP;
            if (actionId == EditorInfo.IME_ACTION_DONE || enterReleased) {
                finishNameEntry(dialog, edit.getText().toString(), true);
                return true;
            }
            return false;
        });

        dialog.setOnCancelListener(d -> finishNameEntry(dialog, "", false));
        dialog.setContentView(root);
        dialog.show();

        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.BLACK));
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT);
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        }

        edit.requestFocus();
        edit.post(() -> {
            InputMethodManager imm =
                    (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT);
            }
        });
    }

    private void finishNameEntry(Dialog dialog, String value, boolean accepted) {
        if (nameEntryDialog != dialog) {
            return;
        }

        nameEntryDialog = null;
        int target = nameEntryDialogTarget;
        nameEntryDialogTarget = 0;
        dialog.dismiss();
        enterFullscreen();
        String sanitizedValue = accepted
                ? sanitizeName(value, target == NAME_ENTRY_TARGET_CONFIG)
                : "";
        if (target == NAME_ENTRY_TARGET_REPLAY) {
            nativeReplayNameEntryComplete(sanitizedValue, accepted);
        } else {
            nativeNameEntryComplete(sanitizedValue, accepted);
        }
    }

    private int dp(int value) {
        float density = getResources().getDisplayMetrics().density;
        return Math.round((float)value * density);
    }

    private static String sanitizeName(String value, boolean allowSpaces) {
        if (value == null) {
            return "";
        }

        StringBuilder builder = new StringBuilder(8);
        for (int i = 0; i < value.length() && builder.length() < 8; ++i) {
            char ch = sanitizeNameChar(value.charAt(i), allowSpaces);
            if (ch != 0) {
                builder.append(ch);
            }
        }
        return builder.toString();
    }

    private static char sanitizeNameChar(char ch, boolean allowSpaces) {
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - ('a' - 'A'));
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                || (allowSpaces && ch == ' ')) {
            return ch;
        }
        return 0;
    }

    private static final class NameInputFilter implements InputFilter {
        private final boolean allowSpaces;

        NameInputFilter(boolean allowSpaces) {
            this.allowSpaces = allowSpaces;
        }

        @Override
        public CharSequence filter(CharSequence source, int start, int end,
                Spanned dest, int dstart, int dend) {
            StringBuilder builder = new StringBuilder(end - start);
            boolean changed = false;

            for (int i = start; i < end; ++i) {
                char original = source.charAt(i);
                char ch = sanitizeNameChar(original, allowSpaces);
                if (ch == 0) {
                    changed = true;
                    continue;
                }
                if (ch != original) {
                    changed = true;
                }
                builder.append(ch);
            }

            return changed ? builder.toString() : null;
        }
    }

    public String stageContentUri(String uriText, String destDirText) {
        if (uriText == null || destDirText == null) {
            return null;
        }

        Uri uri;
        try {
            uri = Uri.parse(uriText);
        } catch (Exception e) {
            Log.w(TAG, "Invalid CD image URI: " + uriText, e);
            return null;
        }

        File destDir = new File(destDirText);
        if (!destDir.isDirectory() && !destDir.mkdirs()) {
            Log.w(TAG, "Could not create CD image staging dir " + destDir);
            return null;
        }

        String fileName = sanitizeImportFileName(queryDisplayName(uri));
        if (fileName.isEmpty()) {
            fileName = sanitizeImportFileName(importFallbackName(uri));
        }
        if (fileName.isEmpty()) {
            Log.w(TAG, "CD image URI has no usable display name: " + uriText);
            return null;
        }

        File outFile = new File(destDir, fileName);
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IOException("ContentResolver returned null stream");
            }
            try (FileOutputStream out = new FileOutputStream(outFile, false)) {
                copyStream(in, out);
            }
            Log.i(TAG, "Staged CD image URI to " + outFile.getAbsolutePath());
            return outFile.getAbsolutePath();
        } catch (Exception e) {
            Log.w(TAG, "Failed to stage CD image URI " + uriText, e);
            return null;
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri, new String[] { OpenableColumns.DISPLAY_NAME },
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (nameIndex >= 0) {
                    return cursor.getString(nameIndex);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to query display name for " + uri, e);
        }
        return null;
    }

    private static String importFallbackName(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) {
            return "";
        }

        int colon = name.lastIndexOf(':');
        if (colon >= 0 && colon + 1 < name.length()) {
            return name.substring(colon + 1);
        }
        return name;
    }

    private static String sanitizeImportFileName(String value) {
        if (value == null) {
            return "";
        }

        String name = value.trim();
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0 && slash + 1 < name.length()) {
            name = name.substring(slash + 1);
        }

        StringBuilder builder = new StringBuilder(name.length());
        for (int i = 0; i < name.length(); ++i) {
            char ch = name.charAt(i);
            if (ch == '/' || ch == '\\' || Character.isISOControl(ch)) {
                continue;
            }
            builder.append(ch);
        }

        String sanitized = builder.toString().trim();
        if (sanitized.equals(".") || sanitized.equals("..")) {
            return "";
        }
        return sanitized;
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
            copyStream(in, out);
        }
    }

    private static void copyStream(InputStream in, FileOutputStream out) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        int bytesRead;
        while ((bytesRead = in.read(buffer)) != -1) {
            out.write(buffer, 0, bytesRead);
        }
    }
}
