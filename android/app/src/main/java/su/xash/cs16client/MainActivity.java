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
                // argv passed to the engine's XashActivity.
                //
                // -console enables the in-game console WITHOUT developer mode
                // (host.c: host.allow_console = true, developer untouched).
                // The previous `-dev 2` also enabled the console but turned on
                // developer mode, one of the two gates in GL_CheckForErrors_
                // (ref/gl/gl_opengl.c). With it on, the engine printed every
                // renderer error every frame -> GL_INVALID_ENUM spam. Freaky
                // is quiet because its launcher does not pass -dev either.
                //
                // Killfeed's Init() additionally sets developer=0 and
                // gl_check_errors=0 via Cvar_Set as belt-and-braces, but the
                // root was this flag. -console keeps the console usable while
                // leaving developer off, so no spam and the console still
                // opens. To get real renderer diagnostics, run `developer 1`
                // in console -- do not re-add -dev casually, it is loud.
                .putExtra("argv", "-console -log -dll @yapb")
                .putExtra("package", getPackageName()));
        finish();
    }
}
