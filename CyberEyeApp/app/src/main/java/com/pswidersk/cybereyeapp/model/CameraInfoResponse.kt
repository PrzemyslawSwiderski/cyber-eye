package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class CameraInfoResponse(
    val status: CameraStatus = CameraStatus.UNKNOWN,
    val time: Long = 0,
    val temp: Float = Float.NaN,
    val signal: Int = 0,
    @SerialName("last_error") val lastError: String = "",
    @SerialName("free_heap") val freeHeap: Int = 0,
    @SerialName("free_block") val freeBlock: Int = 0
)