package org.dexr.jfgxr;

import org.libsdl.app.SDLActivity;
import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;

/** Activity hosts audio/lifecycle only; Vulkan presentation belongs to OpenXR. */
public final class MainActivity extends SDLActivity {
    private static final int PICK_ROM=44;
    private static final String ROM_SHA1="493ced9008dbe932d6e91179b68e8630cf23a023";
    private static native void nativePrepare(Activity activity,String folder);
    private static native void nativeExit();
    @Override protected String[] getLibraries() {
        return new String[]{"c++_shared","SDL2","openxr_loader","JetForceGeminiRecompiled"};
    }
    @Override protected String[] getArguments() {
        return new String[]{"--rom",new File(getFilesDir(),"baserom.us.z64").getAbsolutePath()};
    }
    private void copyAsset(String name) throws Exception {
        File output=new File(getFilesDir(),"assets/"+name);
        output.getParentFile().mkdirs();
        try(InputStream source=getAssets().open(name);FileOutputStream sink=new FileOutputStream(output)) {
            byte[] buffer=new byte[65536];int n;
            while((n=source.read(buffer))>0)sink.write(buffer,0,n);
        }
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            copyAsset("xr/menu.bdf");
            copyAsset("xr/FONT-LICENSE.txt");
            nativePrepare(this,getFilesDir().getAbsolutePath());
            if(!new File(getFilesDir(),"baserom.us.z64").isFile()) {
                Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT);
                picker.addCategory(Intent.CATEGORY_OPENABLE);
                picker.setType("*/*");
                picker.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                    DocumentsContract.buildDocumentUri("com.android.externalstorage.documents","primary:Download"));
                picker.putExtra(Intent.EXTRA_TITLE,"Downloads: select JetForceGemini.z64 (your US dump)");
                startActivityForResult(picker,PICK_ROM);
            }
        } catch(Exception error) {
            Log.e("JFGXR","Cannot prepare game data",error);nativeExit();finish();
        }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request!=PICK_ROM)return;
        if(result!=RESULT_OK || data==null) {nativeExit();finish();return;}
        new Thread(()->{
            File temporary=new File(getFilesDir(),"baserom.importing");
            try {
                MessageDigest digest=MessageDigest.getInstance("SHA-1");
                long size=0;
                try(InputStream source=getContentResolver().openInputStream(data.getData());
                    FileOutputStream sink=new FileOutputStream(temporary)) {
                    byte[] buffer=new byte[65536];int n;
                    while((n=source.read(buffer))>0) {
                        size+=n;if(size>33554432)throw new Exception("Cartridge exceeds 32 MiB");
                        digest.update(buffer,0,n);sink.write(buffer,0,n);
                    }
                }
                StringBuilder sha=new StringBuilder();
                for(byte b:digest.digest())sha.append(String.format("%02x",b&255));
                if(size!=33554432 || !ROM_SHA1.equals(sha.toString()))
                    throw new Exception("Expected the unmodified US big-endian .z64 cartridge dump");
                if(!temporary.renameTo(new File(getFilesDir(),"baserom.us.z64")))
                    throw new Exception("Cannot store imported cartridge");
            } catch(Exception error) {
                temporary.delete();Log.e("JFGXR","ROM import failed",error);
                runOnUiThread(()->{nativeExit();finish();});
            }
        },"ROM import").start();
    }
    @Override protected void onDestroy() {
        nativeExit();super.onDestroy();
        // Engine globals are process-scoped; a later launch starts a fresh session.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
