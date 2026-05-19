package com.pswidersk.cybereyeapp

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.ui.Modifier
import androidx.lifecycle.lifecycleScope
import com.pswidersk.cybereyeapp.AppState.cameraStatus
import com.pswidersk.cybereyeapp.model.StatusResponse
import com.pswidersk.cybereyeapp.ui.screens.HomeScreen
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            CyberEyeAppTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
                    HomeScreen(
                        modifier = Modifier.padding(padding),
                        onShowVideoClick = {
                            startActivity(Intent(this, VideoActivity::class.java))
                        },
                    )
                }
            }
        }
    }
}
