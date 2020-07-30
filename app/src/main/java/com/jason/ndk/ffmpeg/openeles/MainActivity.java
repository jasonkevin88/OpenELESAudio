package com.jason.ndk.ffmpeg.openeles;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v4.app.ActivityCompat;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import java.io.File;

public class MainActivity extends AppCompatActivity {

  private static int REQ_PERMISSION_CODE = 1001;
  private static final String[] PERMISSIONS = { Manifest.permission.READ_EXTERNAL_STORAGE,
      Manifest.permission.WRITE_EXTERNAL_STORAGE};

  // Used to load the 'native-lib' library on application startup.
  static {
    System.loadLibrary("avcodec-58");
    System.loadLibrary("avdevice-58");
    System.loadLibrary("avfilter-7");
    System.loadLibrary("avformat-58");
    System.loadLibrary("avutil-56");
    System.loadLibrary("avresample-4");
    System.loadLibrary("swresample-3");
    System.loadLibrary("swscale-5");
    System.loadLibrary("native-lib");
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);
    checkAndRequestPermissions();
  }

  /**
   * 权限检测以及申请
   */
  private void checkAndRequestPermissions() {
    // Manifest.permission.WRITE_EXTERNAL_STORAGE 和  Manifest.permission.READ_PHONE_STATE是必须权限，允许这两个权限才会显示广告。

    if (hasPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
        && hasPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)) {

    } else {
      ActivityCompat.requestPermissions(this, PERMISSIONS, REQ_PERMISSION_CODE);
    }

  }

  /**
   * 权限判断
   * @param permissionName
   * @return
   */
  private boolean hasPermission(String permissionName) {
    return ActivityCompat.checkSelfPermission(this, permissionName)
        == PackageManager.PERMISSION_GRANTED;
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {

    if (requestCode == REQ_PERMISSION_CODE) {
      checkAndRequestPermissions();
    }
    super.onRequestPermissionsResult(requestCode, permissions, grantResults);
  }

  public void onPlay(View view) {
    new Thread(new Runnable() {
      @Override
      public void run() {
        File inputFile =  new File(Environment.getExternalStorageDirectory(), "input.mp3");
        String audioPath = inputFile.getAbsolutePath();
        sound(audioPath);
      }
    }).start();

  }

  public void onStop(View view) {
    stop();
  }


  public native void sound(String input);

  public native void stop();

  public void onPlay2(View view) {
    new Thread(new Runnable() {
      @Override
      public void run() {
        File inputFile =  new File(Environment.getExternalStorageDirectory(), "input.mp3");
        String audioPath = inputFile.getAbsolutePath();
        play2(audioPath);
      }
    }).start();
  }

  public void onStop2(View view) {
    stop2();
  }

  public native void play2(String input);

  public native void stop2();
}
