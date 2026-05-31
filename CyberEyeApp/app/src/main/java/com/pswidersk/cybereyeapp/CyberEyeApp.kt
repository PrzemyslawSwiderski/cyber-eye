package com.pswidersk.cybereyeapp

import android.app.Application

class CyberEyeApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CameraInfoManager.startFetching()
    }

    override fun onTerminate() {
        super.onTerminate()
        CameraInfoManager.stopFetching()
    }
}