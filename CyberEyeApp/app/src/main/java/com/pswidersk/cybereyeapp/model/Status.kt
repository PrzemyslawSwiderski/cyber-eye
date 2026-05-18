package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.SerialName

enum class Status {
    @SerialName("ok")
    OK,

    @SerialName("ready")
    READY,

    @SerialName("streaming")
    STREAMING,
    UNKNOWN
}
