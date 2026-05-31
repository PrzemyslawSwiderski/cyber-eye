package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.SerialName

enum class CameraStatus {
    @SerialName("ok")
    OK,

    @SerialName("ready")
    READY,

    @SerialName("streaming")
    STREAMING,
    PENDING,
    UNKNOWN
}
