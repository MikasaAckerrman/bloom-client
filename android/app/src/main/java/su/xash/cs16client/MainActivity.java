package su.xash.cs16client;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String pkg = "su.xash.engine.test";

        try {
            getPackageManager().getPackageInfo(pkg, 0);
        } catch (PackageManager.NameNotFoundException e) {
            try {
                pkg = "su.xash.engine";
                getPackageManager().getPackageInfo(pkg, 0);
            } catch (PackageManager.NameNotFoundException ex) {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous")).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
                finish();
                return;
            }
        }

        startActivity(new Intent().setComponent(new ComponentName(pkg, "su.xash.engine.XashActivity"))
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK)
                .putExtra("gamedir", "cstrike")
                .putExtra("gamelibdir", getApplicationInfo().nativeLibraryDir)
                // argv passed to the engine's XashActivity. NO -dev here.
                //
                // `-dev 2` (the previous value) turned on engine developer mode,
                // one of the two gates in GL_CheckForErrors_ (ref/gl/gl_opengl.c:
                // `!gl_check_errors.value || !gpGlobals->developer`). With it on,
                // the engine printed every renderer error to the console every
                // frame, and the user's screen filled with GL_INVALID_ENUM lines.
                // Freaky is quiet because its launcher does not pass -dev either.
                //
                // Killfeed's Init() additionally sets developer=0 and
                // gl_check_errors=0 via Cvar_Set as belt-and-braces, but the
                // root was this flag. To get real renderer diagnostics back,
                // pass -dev here or run `developer 1` in console -- do not
                // re-add it casually, it is loud by design.
                .putExtra("argv", "-log -dll @yapb")
                .putExtra("package", getPackageName()));
        finish();
    }
}
