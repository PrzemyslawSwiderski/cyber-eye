package com.pswidersk.cybereyeapp.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color.Companion.White

private val DarkColorScheme = darkColorScheme(
    primary = CyberGreen,
    secondary = CyberGreenLight,
    tertiary = CyberGreenDark,
    surface = White
)

@Composable
fun CyberEyeAppTheme(
    content: @Composable () -> Unit
) {
    val colorScheme = DarkColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}