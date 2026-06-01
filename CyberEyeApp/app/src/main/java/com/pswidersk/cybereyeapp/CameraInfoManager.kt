package com.pswidersk.cybereyeapp

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

private const val FETCH_PERIOD_MS = 1000L

object CameraInfoManager {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var fetchJob: Job? = null

    fun startFetching() {
        if (fetchJob?.isActive == true) return

        fetchJob = scope.launch {
            while (isActive) {
                try {
                    CameraClient.fetchCameraInfo()
                } catch (e: Exception) {
                    Log.e("StatsManager", "Failed to fetch stats", e)
                }
                delay(FETCH_PERIOD_MS)
            }
        }
    }

    fun stopFetching() {
        fetchJob?.cancel()
        fetchJob = null
    }
}
