package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.valueSize
import kotlin.math.roundToInt

@Composable
fun ControlSlider(
    label: String,
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    icon: String
) {
    val displayValue = when {
        valueRange.endInclusive <= 1f -> "${(value * 100).roundToInt()}%"
        valueRange.endInclusive <= 100f -> "${value.roundToInt()}"
        valueRange.endInclusive <= 1000f -> "${value.roundToInt()}"
        else -> "${(value / 1000).roundToInt()}k"
    }

    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = icon,
                fontSize = labelSize,
                modifier = Modifier.width(32.dp)
            )

            Text(
                text = "$label:",
                color = Color.White,
                fontSize = labelSize,
                modifier = Modifier.width(90.dp)
            )

            Slider(
                value = value,
                onValueChange = onValueChange,
                valueRange = valueRange,
                modifier = Modifier
                    .weight(1f)
                    .height(5.dp),
                colors = SliderDefaults.colors(
                    thumbColor = MaterialTheme.colorScheme.primary,
                    activeTrackColor = MaterialTheme.colorScheme.primary
                )
            )

            Text(
                text = displayValue,
                color = Color.White,
                fontSize = valueSize,
                modifier = Modifier.width(50.dp)
            )
        }
    }
}